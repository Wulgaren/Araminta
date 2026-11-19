// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//! emu_test: Felucca's firmware in the fm1-emu emulator, device-timed (fm1-live's FM1_STRICT: each
//! instruction its cost measured on an FM-1, the 32 KB XIP cache), playing a heavy patch with punch-ins;
//! prints the firmware's own load counters (audio.c felucca_dbg, shed_count, voice_kills) as one JSON line per scenario.
//!
//!   emu_test --fwsc build/felucca.fwsc --elf build/felucca.elf --scenario dry|keys|stack|churn --ms N
//!   emu_test --fwsc build/felucca.fwsc --elf build/felucca.elf --scenario idle --ms N
//!   emu_test --fwsc build/felucca.fwsc --elf build/felucca.elf --scenario project --ms N --project FILE [--notes N]
//!            [--punch PAGE:KEY[,PAGE:KEY..]] [--punch-latch]
//!                                (a punch-in, or a stack of one page, over the project's patch, e.g. 1:C4)
//!   emu_test --fwsc FILE --elf FILE --scenarios 'dry:3000 keys:9000 churn:6000 stack:6000'
//!            [--clear] [--wav OUT.wav] [--mhz 240] [--fast] [--seed N] [--profile N] [--footprint N]
//!
//! The run: boot, USB-MIDI configured, the heavy patch set up through the editor's SysEx (editor.c) and
//! played with MIDI notes on channels 1..4, the FX layer's PUNCH-IN 1/2 page opened with the FX button,
//! PLAY; then the scenario's N ms (--ms counts from there, not from power-on). --project instead puts a project's
//! raw bytes in as backup object 0 (the running project) and plays it as it is. The counters' maxima and
//! late counts are zeroed when those N ms start, so they cover the scenario only; `halves` counts its
//! halves; `cpu_pct` is the highest the firmware's running average (cpu_q8) reached in it; a `stages:` line breaks
//! the render down per part and for bus_run, from the firmware's own smoothed per-block costs. --clear releases
//! keys, stops the transport and restores the punch page before each scenario, so one boot can measure
//! several; --wav writes one scenario's audio. --fast: no cost model, no XIP cache, the JIT (quick, not
//! device-timed).
//! Exit 1 with a message on stderr: boot, a CPU fault, a bad DBG magic, a setup the device did not take,
//! or a punch-in that never engaged / did not let go.

use std::collections::{HashMap, HashSet};
use std::process::ExitCode;

use fm1_core::{Bus, Cpu};
use fm1_soc::{machine, Action, Soc, RAM_BASE};

const RAM_SIZE: usize = 1 << 20;
/// Instructions a `run` call: about 50 us of the device, the granularity of the host's MIDI and RAM probes.
const CHUNK: u32 = 10_000;
/// One audio half: 128 frames at 44.1 kHz, 2902 us; the render runs it as HALF_FRAMES / CTL blocks.
const HALF_US: f64 = 2902.0;
const HALF_FRAMES: u32 = 128;
const USAGE: &str = "usage: emu_test --fwsc FELUCCA.fwsc --elf FELUCCA.elf \
(--scenario dry|keys|stack|churn --ms N | --scenario idle --ms N | --scenario project --ms N --project FILE | \
--scenarios 'dry:3000 keys:9000 churn:6000 stack:6000') \
[--clear] [--wav OUT.wav] [--mhz 240] [--fast] [--seed N] [--notes N] \
[--punch PAGE:KEY[,PAGE:KEY..]] [--punch-latch] [--profile N] [--footprint N]";

// The firmware's internal ids (parameters, PF_* effects, the punch pages' key maps), generated from its sources by
// tests/emu/ids.c into build/emu/ids.rs before this builds (tests/run_tests.sh, FM1_EMU=1)
include!(concat!(env!("CARGO_MANIFEST_DIR"), "/../../build/emu/ids.rs"));

/// The white keys F3 .. G5 by the emulator's names: the index of PF_PAGE1 / PF_PAGE2.
const WHITE: [&str; 16] = ["F3", "G3", "A3", "B3", "C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5", "D5", "E5", "F5", "G5"];

/// The audio punch-ins of PUNCH-IN page 1 or 2: the white key, its effect (the MIDI ones and empty keys left out).
fn page_keys(page: u32) -> Vec<(&'static str, u32)> {
    let map = if page == 1 { PF_PAGE1 } else { PF_PAGE2 };
    WHITE.iter().zip(map).filter(|&(_, e)| e < PF_N && !(PF_OCTD..=PF_X2).contains(&e)).map(|(&k, e)| (k, e)).collect()
}

/// Every punch-in of PUNCH-IN page 1 or 2, audio and MIDI (empty keys left out).
fn page_keys_all(page: u32) -> Vec<(&'static str, u32)> {
    let map = if page == 1 { PF_PAGE1 } else { PF_PAGE2 };
    WHITE.iter().zip(map).filter(|&(_, e)| e < PF_N).map(|(&k, e)| (k, e)).collect()
}

// editor.c's SysEx commands (F0 7D 46 4C cmd args.. F7) and editor_menu.c's FX LATCH id: protocol constants,
// append-only by their own rules (web/EDITOR_PROTOCOL.md; "an id keeps its meaning for good")
const ED_PING: u8 = 25;
const ED_PRESET: u8 = 8;
const ED_SET: u8 = 3;
const ED_TRACK: u8 = 27;
const ED_TRACK_PARAM: u8 = 31;
const ED_MENU_SET: u8 = 73;
const ED_SONG: u8 = 33;
const MENU_FX_LATCH: u8 = 7;

/// tests/punch_crackle_test.c synth_heavy(): per part its engine, preset and how many chord notes it holds
/// (FM6, PHASE, VOICE at full poly; part 4 DRUM-X, hit on 16ths)
const PARTS: [(u8, u8, usize); 4] = [(12, 0, 8), (2, 0, 8), (5, 0, 4), (10, 0, 0)];
const NOTES: [u8; 8] = [48, 52, 55, 59, 60, 64, 67, 71];
const DRUM_STEP_MS: u64 = 125; // 1/16 at 120 BPM

#[derive(Clone, Copy, PartialEq)]
enum Scenario {
    Dry,
    Keys,
    Stack,
    Churn,
    /// power-on default project, nothing played, transport stopped
    Idle,
    /// --project's own patch, played from its four parts and its sequencer; no heavy patch, no punch page
    Project,
}

impl Scenario {
    fn parse(s: &str) -> Option<Self> {
        Some(match s {
            "dry" => Scenario::Dry,
            "keys" => Scenario::Keys,
            "stack" => Scenario::Stack,
            "churn" => Scenario::Churn,
            "idle" => Scenario::Idle,
            "project" => Scenario::Project,
            _ => return None,
        })
    }
    fn name(self) -> &'static str {
        match self {
            Scenario::Dry => "dry",
            Scenario::Keys => "keys",
            Scenario::Stack => "stack",
            Scenario::Churn => "churn",
            Scenario::Idle => "idle",
            Scenario::Project => "project",
        }
    }
}

struct Args {
    fwsc: String,
    elf: String,
    scenarios: Vec<(Scenario, u64)>,
    clear: bool,
    wav: Option<String>,
    mhz: u64,
    fast: bool,
    seed: u32,
    /// --project FILE: a project's raw FUN* bytes, PUT as backup id 0 (the running project) before the scenario
    project: Option<String>,
    /// --notes N: held notes on each part for the project scenario (0..8, default 4)
    notes: usize,
    /// --punch PAGE:KEY[,PAGE:KEY..]: with --scenario project, hold one or more PUNCH-IN keys of one page
    /// (1:C4 or 1:C4,1:D4), so a project's own patch is measured with those effects (each probe must show engaged)
    punch: Option<String>,
    /// --punch-latch: hold the --punch keys with FX LATCH (a tap turns each effect on and leaves it on, so the
    /// buffer effects of a page stack instead of the last one playing); G5 CLR at the end
    punch_latch: bool,
    /// --profile N: the N functions each core spent the most device time in during the scenario (0: off)
    profile: usize,
    /// --footprint N: the N symbols that fetch the most 64-byte flash lines a half, the cache flushed at each
    /// half (0: off)
    footprint: usize,
}

fn parse_args() -> Result<Args, String> {
    let a: Vec<String> = std::env::args().skip(1).collect();
    let (mut fwsc, mut elf, mut scenario, mut ms) = (None, None, None, None);
    let mut scenarios = None;
    let mut clear = false;
    let mut project = None;
    let mut notes = 4usize;
    let mut punch = None;
    let mut punch_latch = false;
    let (mut wav, mut mhz, mut fast, mut seed, mut profile, mut footprint) = (None, 240u64, false, 1u32, 0usize, 0usize);
    let mut i = 0;
    while i < a.len() {
        let val = |i: usize| a.get(i + 1).cloned().ok_or_else(|| format!("{} needs a value", a[i]));
        let num = |i: usize| val(i)?.parse::<u64>().map_err(|_| format!("{}: not a number", a[i]));
        match a[i].as_str() {
            "--fwsc" => fwsc = Some(val(i)?),
            "--elf" => elf = Some(val(i)?),
            "--scenario" => scenario = Some(Scenario::parse(&val(i)?).ok_or("--scenario: dry, keys, stack, churn, idle or project")?),
            "--ms" => ms = Some(num(i)?),
            "--project" => project = Some(val(i)?),
            "--notes" => notes = num(i)? as usize,
            "--punch" => punch = Some(val(i)?),
            "--punch-latch" => {
                punch_latch = true;
                i += 1;
                continue;
            }
            "--scenarios" => {
                let mut cases = Vec::new();
                for case in val(i)?.split_whitespace() {
                    let (name, duration) = case.split_once(':').ok_or("--scenarios: expected name:ms")?;
                    let scene = Scenario::parse(name).ok_or("--scenarios: dry, keys, stack, churn, idle or project")?;
                    let duration = duration.parse::<u64>().map_err(|_| "--scenarios: invalid duration")?;
                    cases.push((scene, duration));
                }
                scenarios = Some(cases);
            }
            "--wav" => wav = Some(val(i)?),
            "--mhz" => mhz = num(i)?,
            "--seed" => seed = u32::try_from(num(i)?).map_err(|_| "--seed: at most 32 bits")?,
            "--profile" => profile = num(i)? as usize,
            "--footprint" => footprint = num(i)? as usize,
            "--fast" => {
                fast = true;
                i += 1;
                continue;
            }
            "--clear" => {
                clear = true;
                i += 1;
                continue;
            }
            other => return Err(format!("unknown argument {other}")),
        }
        i += 2;
    }
    let cases = match (scenarios, scenario, ms) {
        (Some(cases), None, None) => cases,
        (None, Some(scene), Some(duration)) => vec![(scene, duration)],
        _ => return Err(USAGE.into()),
    };
    match (fwsc, elf) {
        (Some(fwsc), Some(elf)) if mhz > 0 && !cases.is_empty() && cases.iter().all(|(_, ms)| *ms > 0) => {
            if fast && footprint > 0 {
                return Err("--footprint needs strict mode (no --fast)".into());
            }
            if cases.len() > 1 && wav.is_some() {
                return Err("--wav requires a single scenario".into());
            }
            if cases.len() > 1 && footprint > 0 {
                // --footprint flushes the flash cache at every half boundary: a later scenario in the same
                // boot would start cold, so it measures one scenario only
                return Err("--footprint requires a single scenario".into());
            }
            let has_project = cases.iter().any(|(s, _)| *s == Scenario::Project);
            if has_project && project.is_none() {
                return Err("--scenario project needs --project FILE (a project's raw FUN* bytes)".into());
            }
            if project.is_some() && !has_project {
                return Err("--project needs --scenario project".into());
            }
            if has_project && cases.len() > 1 {
                return Err("--project runs alone: one project scenario per boot".into());
            }
            if cases.iter().any(|(s, _)| *s == Scenario::Idle) && cases.len() > 1 {
                return Err("--scenario idle runs alone: one idle scenario per boot".into());
            }
            if notes > NOTES.len() {
                return Err(format!("--notes: at most {}", NOTES.len()));
            }
            if notes != 4 && !has_project {
                return Err("--notes needs --scenario project".into());
            }
            if punch.is_some() && !has_project {
                return Err("--punch needs --scenario project (a punch-in held over a project's own patch)".into());
            }
            if punch_latch && punch.is_none() {
                return Err("--punch-latch needs --punch".into());
            }
            Ok(Args { fwsc, elf, scenarios: cases, clear, wav, mhz, fast, seed, project, notes, punch, punch_latch,
                      profile, footprint })
        }
        _ => Err(USAGE.to_string()),
    }
}

fn main() -> ExitCode {
    let args = match parse_args() {
        Ok(a) => a,
        Err(e) => {
            eprintln!("emu_test: {e}");
            return ExitCode::from(2);
        }
    };
    match run(&args) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("emu_test: {e}");
            ExitCode::FAILURE
        }
    }
}

// ------------------------------------------------------------------------------------------ ELF --

/// The RAM addresses of the firmware's variables the test reads, from the ELF's symbol table.
struct Syms {
    dbg: u32,
    shed_count: u32,
    voice_kills: u32,
    perf_held: u32,
    perf_latched: u32,
    perf_act: u32,
    kb_lock: u32,
    perf_page2: u32,
    /// the four tracks (pfx.c's MIDI punch-ins write t->pfx / t->pfx_pit)
    trk: u32,
    /// fx.c's part_cost[NTRK]: each part's render in 24 MHz ticks, smoothed per block (None: older firmware)
    part_cost: Option<u32>,
    /// fx.c's bus_cost: bus_run's ticks, smoothed the same way (None: older firmware)
    bus_cost: Option<u32>,
}

/// Reads `path`'s ELF32 (little-endian) symbol table into name -> (value, size). A name defined twice
/// (two file-local statics) maps to None, so a lookup of it fails rather than picks one.
fn elf_symbols(path: &str) -> Result<HashMap<String, Option<(u32, u32)>>, String> {
    let mut out: HashMap<String, Option<(u32, u32)>> = HashMap::new();
    for (name, value, size, _) in elf_entries(path)? {
        out.entry(name).and_modify(|v| *v = None).or_insert(Some((value, size)));
    }
    Ok(out)
}

/// The functions of `path`'s ELF (STT_FUNC, sized), sorted by address: --profile's map from a PC to its function.
struct Funcs(Vec<(u32, u32, String)>);

impl Funcs {
    fn load(path: &str) -> Result<Self, String> {
        let mut f: Vec<(u32, u32, String)> = elf_entries(path)?.into_iter()
            .filter(|&(_, _, size, info)| info & 15 == 2 && size > 0)
            .map(|(name, value, size, _)| (value, value + size, name))
            .collect();
        f.sort();
        Ok(Funcs(f))
    }

    fn name(&self, pc: u32) -> &str {
        let i = self.0.partition_point(|&(start, _, _)| start <= pc);
        match i.checked_sub(1).map(|k| &self.0[k]) {
            Some((_, end, name)) if pc < *end => name,
            _ => "(no function)",
        }
    }
}

/// Every named entry of `path`'s ELF32 (little-endian) symbol table: name, value, size, st_info.
fn elf_entries(path: &str) -> Result<Vec<(String, u32, u32, u8)>, String> {
    let f = std::fs::read(path).map_err(|e| format!("{path}: {e}"))?;
    let bad = || format!("{path}: not a little-endian ELF32 with a symbol table");
    if f.len() < 52 || &f[..4] != b"\x7fELF" || f[4] != 1 || f[5] != 1 {
        return Err(bad());
    }
    let u16_at = |o: usize| f.get(o..o + 2).map(|b| u16::from_le_bytes([b[0], b[1]]) as usize);
    let u32_at = |o: usize| f.get(o..o + 4).map(|b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]));
    let (shoff, shentsize, shnum) = (u32_at(0x20).ok_or_else(bad)? as usize, u16_at(0x2e).ok_or_else(bad)?, u16_at(0x30).ok_or_else(bad)?);
    let section = |k: usize| -> Option<(u32, usize, usize, usize)> {
        let h = shoff + k * shentsize;
        Some((u32_at(h + 4)?, u32_at(h + 16)? as usize, u32_at(h + 20)? as usize, u32_at(h + 24)? as usize))
    };
    let symtab = (0..shnum).filter_map(section).find(|s| s.0 == 2).ok_or_else(bad)?; // SHT_SYMTAB
    let (_, str_off, str_size, _) = section(symtab.3).ok_or_else(bad)?;
    let strtab = f.get(str_off..str_off + str_size).ok_or_else(bad)?;
    let mut out = Vec::new();
    for s in (symtab.1..symtab.1 + symtab.2).step_by(16) {
        let (Some(name), Some(value), Some(size), Some(&info)) = (u32_at(s), u32_at(s + 4), u32_at(s + 8), f.get(s + 12))
        else { return Err(bad()) };
        let Some(rest) = strtab.get(name as usize..) else { return Err(bad()) };
        let end = rest.iter().position(|&b| b == 0).unwrap_or(rest.len());
        let name = String::from_utf8_lossy(&rest[..end]).into_owned();
        if !name.is_empty() {
            out.push((name, value, size, info));
        }
    }
    Ok(out)
}

impl Syms {
    fn load(path: &str) -> Result<Self, String> {
        let t = elf_symbols(path)?;
        // a symbol the firmware may not have: a wrong size or two definitions is still an error
        let opt = |name: &str, size: u32| -> Result<Option<u32>, String> {
            match t.get(name) {
                Some(Some((addr, sz))) if *sz == size => Ok(Some(*addr)),
                Some(Some((_, sz))) => Err(format!("{path}: {name} is {sz} bytes, expected {size}")),
                Some(None) => Err(format!("{path}: {name} is defined more than once")),
                None => Ok(None),
            }
        };
        let get = |name: &str, size: u32| -> Result<u32, String> {
            opt(name, size)?.ok_or_else(|| format!("{path}: no symbol {name}"))
        };
        Ok(Syms {
            dbg: get("felucca_dbg", 100)?,
            shed_count: get("shed_count", 4)?,
            voice_kills: get("voice_kills", 4)?,
            perf_held: get("perf_held", 4)?,
            perf_latched: get("perf_latched", 4)?,
            perf_act: get("perf_act", 4)?,
            kb_lock: get("kb_lock", 1)?,
            perf_page2: get("perf_page2", 1)?,
            trk: get("trk", TRK_SIZE * 4)?,
            part_cost: opt("part_cost", 8)?,
            bus_cost: opt("bus_cost", 2)?,
        })
    }
}

// felucca_dbg's words (audio.c struct felucca_dbg)
const DBG_MAGIC: u32 = 0x4442_4731;
const DBG_HALVES: u32 = 1;
const DBG_MAX_US: u32 = 2;
const DBG_IN_AUDIO: u32 = 4;
const DBG_LATE: u32 = 5;
const DBG_CPU_Q8: u32 = 9;
const DBG_LATE_OVER: u32 = 19;
const DBG_LATE_T5: u32 = 20;
const DBG_LATE_OTH: u32 = 21;
const DBG_WALL_MAX: u32 = 22;
const DBG_CPU1_Q8: u32 = 24;

// -------------------------------------------------------------------------------------- machine --

/// The emulated FM-1 as fm1-live's Machine sets it up, run headless; the host side (USB-MIDI out, the
/// drum part's hits, the punch-in probes) is serviced between `CHUNK`s.
struct Emu {
    cpu: Cpu,
    cpu1: Option<Cpu>,
    soc: Soc,
    pending_tick: bool,
    jit: bool,
    syms: Syms,
    /// the drum part's next 1/16 (ms) and its hit count; None until the drum part is set up
    drum_next: Option<u64>,
    drum_k: u64,
    drum_last: u8,
    probes: Vec<Probe>,
    /// every perf_act bit seen during the scenario
    act_seen: u32,
}

/// One punch-in check: between `from` and `to` (ms) the effects in `mask` show in perf_held (or perf_latched):
/// any of them, or (`all`) all of them at once. `midi`: instead, a MIDI punch-in is at work on some track
/// (that page's effects go through pfx.c's t->pfx / t->pfx_pit, not the FX layer's perf_held).
struct Probe {
    label: String,
    from: u64,
    to: u64,
    mask: u32,
    latched: bool,
    all: bool,
    midi: bool,
    seen: bool,
}

impl Emu {
    /// Power-on from the package, timed as an FM-1 at `mhz` (strict) or as fm1-live runs it (fast), the
    /// USB cable in.
    fn boot(fwsc: &str, syms: Syms, mhz: u64, strict: bool) -> Result<Self, String> {
        let pkg = std::fs::read(fwsc).map_err(|e| format!("{fwsc}: {e}"))?;
        let st = fm1_soc::stock_from_package(&pkg, None).map_err(|e| format!("{fwsc}: {e}"))?;
        let mut soc = Soc::new(RAM_SIZE, st.xip.clone());
        soc.periph.cycle_ticks = if strict { 8 } else { 1 };
        soc.periph.hz = mhz * 1_000_000 * soc.periph.cycle_ticks;
        soc.strict_align = strict;
        soc.lcd_timed = strict;
        if strict {
            soc.loop_skip.enabled = false;
            soc.xip_cache = Some(fm1_soc::XipCache::default());
        }
        soc.periph.skip_idle = true;
        let mut cpu = Cpu::new(st.entry);
        cpu.jit_on = !strict;
        cpu.cost_model = strict;
        cpu.user = true; // (the SPL hands cpu0 over in user mode)
        let params = RAM_BASE + 0x7fc00;
        soc.write32(params, params + 0x40);
        cpu.regs[0] = fm1_soc::stock_boot(&mut soc, &st.flash, params, st.board);
        soc.sfc_boot(st.key, st.app_end);
        soc.usb_plug();
        Ok(Emu {
            cpu,
            cpu1: None,
            soc,
            pending_tick: false,
            jit: !strict,
            syms,
            drum_next: None,
            drum_k: 0,
            drum_last: 0,
            probes: Vec::new(),
            act_seen: 0,
        })
    }

    fn ms(&self) -> u64 {
        self.soc.periph.ms()
    }

    /// A MIDI punch-in is at work on some track: pfx.c writes t->pfx (PFX_* bits) and t->pfx_pit (OCT- / OCT+).
    fn pfx_active(&mut self) -> bool {
        (0..4u32).any(|k| {
            let a = self.syms.trk + k * TRK_SIZE;
            self.soc.read8(a + TRK_PFX_OFF) != 0 || self.soc.read16(a + TRK_PFX_PIT_OFF) != 0
        })
    }

    /// One CHUNK of both cores, then the host's side: the drum hit due, the punch-in probes.
    fn step(&mut self) -> Result<(), String> {
        let Emu { cpu, cpu1, soc, pending_tick, jit, .. } = self;
        machine::run_dual(cpu1, soc, CHUNK, RAM_SIZE, *jit, |soc, k| machine::run_blocks(cpu, soc, k, pending_tick))
            .map_err(|e| format!("CPU halted at {} ms: {e}", soc.periph.ms()))?;
        if let Some(fault) = &self.soc.cpu1_fault {
            return Err(format!("core 1 faulted at {} ms: {fault}", self.ms()));
        }
        let now = self.ms();
        if self.drum_next.is_some_and(|t| now >= t) {
            self.drum_hit();
        }
        if !self.probes.is_empty() {
            let held = self.soc.read32(self.syms.perf_held);
            let latched = self.soc.read32(self.syms.perf_latched);
            self.act_seen |= self.soc.read32(self.syms.perf_act);
            let midi = self.probes.iter().any(|p| p.midi) && self.pfx_active();
            for p in self.probes.iter_mut().filter(|p| (p.from..p.to).contains(&now)) {
                if p.midi {
                    p.seen |= midi;
                    continue;
                }
                let v = (if p.latched { latched } else { held }) & p.mask;
                p.seen |= if p.all { v == p.mask } else { v != 0 };
            }
        }
        Ok(())
    }

    fn run_until(&mut self, ms: u64) -> Result<(), String> {
        while self.ms() < ms {
            self.step()?;
        }
        Ok(())
    }

    /// tests/hostsim.c drum_hit: kick on the beat, snare on 2 and 4, closed hat between, on channel 4
    fn drum_hit(&mut self) {
        let k = self.drum_k;
        let note = if k % 4 == 0 { 36 } else if k % 8 == 4 { 38 } else { 42 };
        if self.drum_last != 0 {
            self.soc.usb.midi_send(&[0x83, self.drum_last, 0]);
        }
        self.soc.usb.midi_send(&[0x93, note, 100]);
        self.drum_last = note;
        self.drum_k += 1;
        self.drum_next = self.drum_next.map(|t| t + DRUM_STEP_MS);
    }

    /// One editor command over USB-MIDI and its reply's arguments (the bytes after the command, before F7).
    fn ed(&mut self, cmd: u8, args: &[u8]) -> Result<Vec<u8>, String> {
        let mut msg = vec![0xF0, 0x7D, 0x46, 0x4C, cmd];
        msg.extend_from_slice(args);
        msg.push(0xF7);
        self.soc.usb.midi_send(&msg);
        let deadline = self.ms() + 3000;
        while self.ms() < deadline {
            self.step()?;
            for m in self.soc.usb.midi_take() {
                if m.len() >= 6 && m[1..5] == [0x7D, 0x46, 0x4C, cmd] && m[m.len() - 1] == 0xF7 {
                    return Ok(m[5..m.len() - 1].to_vec());
                }
            }
        }
        Err(format!("editor command {cmd} {args:02x?}: no reply in 3 s"))
    }

    /// TRACK_PARAM: part's parameter id set to v, checked against the device's reply.
    fn track_param(&mut self, part: u8, id: u8, v: i32) -> Result<(), String> {
        let [lo, hi] = v14(v);
        let r = self.ed(ED_TRACK_PARAM, &[part, id, lo, hi])?;
        if r != [part, id, lo, hi] {
            return Err(format!("part {}: parameter {id} = {v} answered {r:02x?}", part + 1));
        }
        Ok(())
    }

    /// A front-panel action at emulated time `ms` (now at the earliest).
    fn act(&mut self, ms: u64, a: Action) {
        let at = self.soc.periph.cycle_of_ms(ms).max(self.soc.periph.cycles);
        let q = &mut self.soc.periph.script;
        let pos = q.partition_point(|(t, _)| *t <= at);
        q.insert(pos, (at, a));
    }

    fn key(&mut self, ms: u64, name: &str, down: bool) {
        let id = fm1_soc::periph::key_id(name).unwrap_or_else(|| panic!("no FM-1 key {name}"));
        self.act(ms, Action::Key { id, down });
    }

    fn tap(&mut self, ms: u64, name: &str, hold: u64) {
        self.key(ms, name, true);
        self.key(ms + hold, name, false);
    }

    fn dbg(&mut self, word: u32) -> u32 {
        self.soc.read32(self.syms.dbg + 4 * word)
    }
}

/// A 14-bit editor value: two 7-bit bytes, LSB first, offset by 8192.
fn v14(v: i32) -> [u8; 2] {
    let u = (v.clamp(-8192, 8191) + 8192) as u32;
    [(u & 0x7f) as u8, (u >> 7) as u8]
}

// ---------------------------------------------------------------------------------------- project --

/// The backup protocol's PUT (editor_backup.c ED_BACKUP_PUT): an object's raw bytes as chunks, then a commit.
/// Object 0 is the running project, so a snapshot goes live without a flash slot or a restart.
const ED_BACKUP_PUT: u8 = 67;
/// a PUT chunk's data (editor_backup.c ed_unpack7's 256-byte buffer, as the sample writes)
const PUT_CHUNK: usize = 256;
/// the largest object the device stages (project.c proj_wire_u: a FUN21 project)
const PUT_MAX: usize = 4864;

/// CRC-32/IEEE (reflected, polynomial 0xEDB88320): editor_backup.c's st_crc32 and the archives' checksum.
fn crc32(bytes: &[u8]) -> u32 {
    let mut c = 0xFFFF_FFFFu32;
    for &b in bytes {
        c ^= u32::from(b);
        for _ in 0..8 {
            c = if c & 1 != 0 { 0xEDB8_8320 ^ (c >> 1) } else { c >> 1 };
        }
    }
    c ^ 0xFFFF_FFFF
}

/// The backup's 7-bit packing: for each group of up to 7 bytes, a mask (bit k = byte k's bit 7), then the
/// low 7 bits of the group's bytes (editor_backup.c ed_bk_pack, web/fm1backup.js bkPack).
fn pack7(bytes: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(bytes.len() + bytes.len() / 7 + 1);
    for chunk in bytes.chunks(7) {
        let mut mask = 0u8;
        for (k, &b) in chunk.iter().enumerate() {
            mask |= (b >> 7) << k;
        }
        out.push(mask);
        out.extend(chunk.iter().map(|b| b & 0x7f));
    }
    out
}

/// A number as the archive's five 7-bit bytes, LSB first (editor_backup.c ed_bk_u32).
fn u32_7(n: u32) -> [u8; 5] {
    let mut a = [0u8; 5];
    for (i, b) in a.iter_mut().enumerate() {
        *b = ((n >> (i * 7)) & (if i == 4 { 15 } else { 127 })) as u8;
    }
    a
}

/// Put `bytes` in as backup object 0 (the running project) and check every reply.
fn put_project(emu: &mut Emu, bytes: &[u8]) -> Result<(), String> {
    if bytes.is_empty() || bytes.len() > PUT_MAX {
        return Err(format!("--project: {} B is not a project object (1..{PUT_MAX})", bytes.len()));
    }
    let mut start = vec![0u8, 0u8];
    start.extend_from_slice(&u32_7(bytes.len() as u32));
    start.extend_from_slice(&u32_7(crc32(bytes)));
    let r = emu.ed(ED_BACKUP_PUT, &start)?;
    if r != [0, 0, 0] {
        return Err(format!("--project: PUT start answered {r:02x?} (2: data refused, 3: stop playback, 4: flash)"));
    }
    let mut off = 0usize;
    while off < bytes.len() {
        let n = (bytes.len() - off).min(PUT_CHUNK);
        let mut a = vec![1u8, 0u8];
        a.extend_from_slice(&u32_7(off as u32));
        a.extend_from_slice(&pack7(&bytes[off..off + n]));
        let r = emu.ed(ED_BACKUP_PUT, &a)?;
        if r != [1, 0, 0] {
            return Err(format!("--project: PUT at {off} answered {r:02x?}"));
        }
        off += n;
    }
    let r = emu.ed(ED_BACKUP_PUT, &[2, 0])?;
    if r != [2, 0, 0] {
        return Err(format!("--project: PUT commit answered {r:02x?} (1: not that object, 2: data or CRC, 5: stale)"));
    }
    eprintln!("[{:6} ms] project put: {} B, {} chunks, CRC {:08x}", emu.ms(), bytes.len(), off.div_ceil(PUT_CHUNK),
              crc32(bytes));
    Ok(())
}

/// Hold `notes` notes on each of the four parts (MIDI IN CH1-4 plays parts 1-4) and start the transport: what a
/// player does to a loaded project.
fn play_project(emu: &mut Emu, notes: usize) -> Result<(), String> {
    for p in 0..4u8 {
        for &note in &NOTES[..notes] {
            emu.soc.usb.midi_send(&[0x90 | p, note + 12 * p, 100]);
        }
    }
    let t = emu.ms();
    emu.tap(t, "PLAY", TAP_MS);
    emu.run_until(t + 1000)?;
    Ok(())
}

// ---------------------------------------------------------------------------------------- setup --

/// Boot to USB configured and the editor answering.
fn wait_ready(emu: &mut Emu) -> Result<(), String> {
    while emu.soc.usb.product().is_none() {
        if emu.ms() > 15_000 {
            return Err(format!("USB not configured after {} ms of boot", emu.ms()));
        }
        emu.step()?;
    }
    let magic = emu.dbg(0);
    if magic != DBG_MAGIC {
        return Err(format!("felucca_dbg magic {magic:#010x} at boot, expected {DBG_MAGIC:#010x}"));
    }
    eprintln!("[{:6} ms] USB configured: {:?}", emu.ms(), emu.soc.usb.product().unwrap_or(""));
    emu.ed(ED_PING, &[])?;
    Ok(())
}

/// synth_heavy() through the editor: each part's preset, POLY / SUS 127 / ARP off on the synths, every insert's
/// MIX 100 and both sends 100 on all four, 120 BPM; then the chords held and the drum part hit on 16ths.
fn heavy_patch(emu: &mut Emu) -> Result<(), String> {
    for (p, &(engine, preset, _)) in PARTS.iter().enumerate() {
        let part = p as u8;
        let r = emu.ed(ED_TRACK, &[part])?;
        if r.first() != Some(&part) {
            return Err(format!("TRACK {part}: selected {r:02x?}"));
        }
        let r = emu.ed(ED_PRESET, &[engine, preset])?;
        if r != [engine, preset] {
            return Err(format!("part {}: PRESET {engine} {preset} answered {r:02x?}", p + 1));
        }
        if engine != 10 {
            emu.track_param(part, P_VOICE, V_POLY)?;
            emu.track_param(part, P_SUS, 127)?;
            emu.track_param(part, P_AMODE, 0)?;
        }
        for id in INSERT_MIX.into_iter().chain([P_DLY, P_REV]) {
            emu.track_param(part, id, 100)?;
        }
    }
    emu.ed(ED_TRACK, &[0])?;
    // 120 BPM, and MIDI IN on CH1-4 (its default, SEL, plays every channel on the selected part)
    for (id, v, what) in [(G_BPM, 120, "BPM 120"), (G_ROUTE, 0, "MIDI IN CH1-4")] {
        let [lo, hi] = v14(v);
        let r = emu.ed(ED_SET, &[1, id, lo, hi])?;
        if r != [1, id, lo, hi] {
            return Err(format!("{what} answered {r:02x?}"));
        }
    }
    strike_chords(emu);
    eprintln!("[{:6} ms] heavy patch set up, chords held, drums on 16ths", emu.ms());
    Ok(())
}

/// Re-strike the heavy patch's chords and restart its drum pattern before a measurement.
fn strike_chords(emu: &mut Emu) {
    for (p, &(_, _, n)) in PARTS.iter().enumerate() {
        for &note in &NOTES[..n] {
            emu.soc.usb.midi_send(&[0x90 | p as u8, note + 12 * p as u8, 100]);
        }
    }
    emu.drum_k = 0;
    emu.drum_last = 0;
    emu.drum_next = Some(emu.ms());
}

/// The FX button tapped until the PUNCH-IN 1/2 page is shown (its sticky FX map: kb_lock bit 1), from HOME:
/// EFFECTS, then PUNCH-IN.
fn open_punch(emu: &mut Emu) -> Result<(), String> {
    for _ in 0..3 {
        let t = emu.ms();
        emu.tap(t, "FX", 150);
        emu.run_until(t + 600)?;
        if emu.soc.read8(emu.syms.kb_lock) & 2 != 0 {
            eprintln!("[{:6} ms] PUNCH-IN 1/2 open (FX map on the keys)", emu.ms());
            return Ok(());
        }
    }
    Err("the FX button never opened the PUNCH-IN page (kb_lock without its FX bit)".into())
}

// ------------------------------------------------------------------------------------ scenarios --

/// What a scenario does from its start (ms from it): panel actions and the checks that they engaged.
#[derive(Default)]
struct Plan {
    acts: Vec<(u64, Act)>,
    probes: Vec<Probe>,
    /// the last action: the run must last until after it
    end: u64,
}

enum Act {
    Key(&'static str, bool),
    /// SELECT turned: walks PUNCH-IN 1/2 <-> 2/2
    Select(i32),
}

impl Plan {
    fn key(&mut self, t: u64, name: &'static str, down: bool) {
        self.acts.push((t, Act::Key(name, down)));
        self.end = self.end.max(t);
    }
    fn tap(&mut self, t: u64, name: &'static str, hold: u64) {
        self.key(t, name, true);
        self.key(t + hold, name, false);
    }
    fn select(&mut self, t: u64, dir: i32) {
        self.acts.push((t, Act::Select(dir)));
        self.end = self.end.max(t);
    }
    fn probe(&mut self, label: String, from: u64, to: u64, mask: u32, latched: bool, all: bool) {
        self.probes.push(Probe { label, from, to, mask, latched, all, midi: false, seen: false });
    }
    /// a MIDI punch-in (pfx.c): some track's t->pfx or t->pfx_pit is set in the window
    fn probe_midi(&mut self, label: String, from: u64, to: u64) {
        self.probes.push(Probe { label, from, to, mask: 0, latched: false, all: false, midi: true, seen: false });
    }
}

const HOLD_MS: u64 = 300;
const GAP_MS: u64 = 200;
const TAP_MS: u64 = 100;
/// after a page walk, the keys wait this long (the encoder's detent, then the UI loop's frame)
const WALK_MS: u64 = 300;
/// the scenario's last this-many ms are quiet: every punch-in must have let go
const TAIL_MS: u64 = 400;

/// A `--punch` key spec (PAGE:KEY) resolved against the page maps.
fn find_punch(spec: &str) -> Result<(u32, &'static str, u32), String> {
    let (page, key) = spec.split_once(':').ok_or("--punch: expected PAGE:KEY, such as 1:C4")?;
    let page: u32 = page.parse().map_err(|_| "--punch: the page is 1 or 2")?;
    if page != 1 && page != 2 {
        return Err("--punch: the page is 1 or 2".into());
    }
    let (name, fx) = page_keys_all(page).into_iter().find(|&(k, _)| k == key)
        .ok_or_else(|| format!("--punch: {key} is not a punch-in of PUNCH-IN {page}/2"))?;
    Ok((page, name, fx))
}

/// project + --punch: one or more keys of one PUNCH-IN page held over the project's own patch from 200 ms. Each
/// effect's probe must show it engaged. One key a run isolates that effect's load; several stack. All the keys of a
/// run are one page (SELECT changes the map). `latch`: FX LATCH, so the buffer effects of a page stack instead of
/// the last one playing; each key is tapped, G5 CLR clears them at the end.
fn plan_project_punch(spec: &str, latch: bool) -> Result<Plan, String> {
    let keys: Vec<(u32, &'static str, u32)> = spec.split(',').map(find_punch).collect::<Result<_, _>>()?;
    let page = keys[0].0;
    if keys.iter().any(|k| k.0 != page) {
        return Err("--punch: the keys of a run are one page (SELECT switches the map)".into());
    }
    let mut pl = Plan::default();
    let mut t = 200;
    if page == 2 {
        pl.select(t, 1);
        t += WALK_MS;
    }
    if latch {
        let mut taps = Vec::new();
        for &(_, name, _) in &keys {
            pl.tap(t, name, TAP_MS);
            taps.push((t, name));
            t += 2 * TAP_MS;
        }
        for &(_, name, fx) in &keys {
            let label = format!("{page}/2 {name}");
            let from = taps.iter().find(|&&(_, n)| n == name).map_or(200, |&(at, _)| at);
            if page_keys(page).iter().any(|&(k, _)| k == name) {
                pl.probe(label, from, t + 20, 1 << fx, true, false);
            } else {
                pl.probe_midi(label, from, t + 20);
            }
        }
        pl.tap(t + 20, "G5", TAP_MS);
        return Ok(pl);
    }
    let down = t;
    for (i, &(_, name, _)) in keys.iter().enumerate() {
        pl.key(down + 20 * i as u64, name, true);
    }
    let release = down + HOLD_MS + 20 * keys.len() as u64;
    for &(_, name, _) in &keys {
        pl.key(release, name, false);
    }
    for &(p, name, fx) in &keys {
        let label = format!("{p}/2 {name}");
        if page_keys(p).iter().any(|&(k, _)| k == name) {
            pl.probe(label, down, release + 20, 1 << fx, false, false);
        } else {
            pl.probe_midi(label, down, release + 20);
        }
    }
    Ok(pl)
}

/// keys: every audio punch-in of PUNCH-IN 1/2, then of 2/2, held HOLD_MS with GAP_MS between.
fn plan_keys() -> Plan {
    let mut pl = Plan::default();
    let mut t = 200;
    for page in [1, 2] {
        if page == 2 {
            pl.select(t, 1);
            t += WALK_MS;
        }
        for (name, e) in page_keys(page) {
            pl.tap(t, name, HOLD_MS);
            pl.probe(format!("{page}/2 {name}"), t, t + HOLD_MS + 20, 1 << e, false, false);
            t += HOLD_MS + GAP_MS;
        }
    }
    pl
}

/// stack: REPEAT 1/16 + LPF + HPF (1/2) and FLANGER + PHASER + REV THROW + ECHO THROW + OCT UP (2/2) on together,
/// with FX LATCH (HPF and FLANGER share D4, so one hand of held keys cannot hold all eight), until `release`;
/// then G5 CLR.
fn plan_stack(ms: u64) -> Plan {
    const PAGE1_FX: [u32; 3] = [PF_R16, PF_LPF, PF_HPF];
    const PAGE2_FX: [u32; 5] = [PF_OUP, PF_FLG, PF_PHS, PF_RTHR, PF_ETHR];
    let mut pl = Plan::default();
    let all: u32 = PAGE1_FX.iter().chain(&PAGE2_FX).map(|e| 1 << e).sum();
    let mut t = 200;
    for (page, effects) in [(1, &PAGE1_FX[..]), (2, &PAGE2_FX[..])] {
        if page == 2 {
            pl.select(t, 1);
            t += WALK_MS;
        }
        let keys = page_keys(page);
        for &e in effects {
            let (name, _) = *keys.iter().find(|&&(_, k)| k == e)
                .unwrap_or_else(|| panic!("PF {e} is not on PUNCH-IN {page}/2 (perform.c PF_DEF / PF2_DEF moved it)"));
            pl.tap(t, name, TAP_MS);
            t += 2 * TAP_MS;
        }
    }
    let release = ms.saturating_sub(TAIL_MS + 200).max(t + 500);
    pl.probe("all eight latched together".into(), t, release, all, true, true);
    pl.tap(release, "G5", TAP_MS);
    pl
}

/// churn: from 200 ms, every 3..93 ms a move: an audio punch-in of the page shown pressed, one held let go,
/// or (now and then, nothing held) SELECT to the other page; every key let go TAIL_MS + 200 before the end.
fn plan_churn(ms: u64, seed: u32) -> Plan {
    let mut pl = Plan::default();
    let mut rng = seed.max(1);
    let mut next = move || {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        rng
    };
    let stop = ms.saturating_sub(TAIL_MS + 200);
    let pages = [page_keys(1), page_keys(2)];
    let mut held: Vec<&'static str> = Vec::new();
    let mut page2 = false;
    let mut t = 200;
    while t < stop {
        let r = next() % 8;
        if r == 0 && held.is_empty() {
            pl.select(t, if page2 { -1 } else { 1 });
            page2 = !page2;
            t += WALK_MS;
            continue;
        }
        if r < 4 || held.is_empty() {
            let keys = &pages[page2 as usize];
            let (name, _) = keys[next() as usize % keys.len()];
            if !held.contains(&name) {
                pl.key(t, name, true);
                held.push(name);
            }
        } else {
            let name = held.swap_remove(next() as usize % held.len());
            pl.key(t, name, false);
        }
        t += 3 + (next() % 31) as u64 * 3;
    }
    for name in held {
        pl.key(stop, name, false);
    }
    let audio: u32 = pages.iter().flatten().map(|(_, e)| 1 << e).sum();
    pl.probe("some audio punch-in held".into(), 200, stop, audio, false, false);
    pl
}

// ------------------------------------------------------------------------------------ footprint --

/// --footprint: the 64-byte flash lines (code and data) the firmware fetches in each audio half, per symbol.
/// A half starts with a cold cache, so the first fetch of every line in it misses: the misses between two half
/// boundaries are exactly the lines that half fetched, which is why the cache is flushed at every boundary. A
/// cold start also makes a half slower than it runs warm, so the run's cpu_pct and late counts are not the
/// device's while measuring.
struct Footprint {
    /// per line, how many halves fetched it
    lines_seen: HashMap<u32, u64>,
    /// the distinct lines each half fetched
    per_half: Vec<usize>,
    /// DBG_HALVES at the last boundary
    half: u32,
    /// the window before the first boundary ran with the cache as boot left it (a warm start, not a half)
    warm: bool,
}

impl Footprint {
    /// Starts counting at the next half boundary, the misses from here on kept aside.
    fn start(emu: &mut Emu) -> Self {
        emu.soc.miss_hist = Some(Default::default());
        Footprint { lines_seen: HashMap::new(), per_half: Vec::new(), half: emu.dbg(DBG_HALVES), warm: true }
    }

    /// A half boundary: the misses since the last one are the lines this half fetched. The cache starts cold
    /// again, its miss count carried on so --profile still reads the whole scenario's.
    fn boundary(&mut self, emu: &mut Emu) {
        let Some(hist) = emu.soc.miss_hist.replace(Default::default()) else { return };
        let lines: HashSet<u32> = hist.keys().map(|k| (k & 0xffff_ffff) as u32).collect();
        if !self.warm {
            for &line in &lines {
                *self.lines_seen.entry(line).or_default() += 1;
            }
            self.per_half.push(lines.len());
        }
        self.warm = false;
        let mut cache = fm1_soc::XipCache::default();
        cache.misses = emu.soc.xip_cache.as_ref().expect("--footprint runs strict: the cache is modelled").misses;
        emu.soc.xip_cache = Some(cache);
    }
}

/// --footprint's report: the lines a half overall and the top `args.footprint` symbols by average lines a half.
/// A line two symbols share (one's end and the next one's start) counts for both, so the per-symbol
/// column can add up to more than the total; a line in no flash object or function goes to "(no symbol)".
fn footprint_report(args: &Args, fp: &Footprint) -> Result<(), String> {
    let halves = fp.per_half.len();
    if halves == 0 {
        return Err("--footprint: no complete half in the scenario".into());
    }
    let avg = fp.per_half.iter().sum::<usize>() as f64 / halves as f64;
    let max = fp.per_half.iter().copied().max().unwrap_or(0);
    eprintln!("footprint: {halves} halves; flash lines a half: avg {avg:.1} ({:.1} KB), max {max}, against the cache's 512 (32 KB); this run's load numbers are not valid (cold cache each half)",
        avg * 64.0 / 1024.0);
    let mut syms: Vec<(u32, u32, String)> = elf_entries(&args.elf)?.into_iter()
        .filter(|&(_, value, size, info)| matches!(info & 15, 1 | 2) && size > 0 && value >= 0x0200_0000)
        .map(|(name, value, size, _)| (value, value.saturating_add(size), name))
        .collect();
    syms.sort();
    let mut sums = vec![0u64; syms.len()];
    let mut no_sym = 0u64;
    for (&line, &count) in &fp.lines_seen {
        let mut hit = false;
        for (k, (start, end, _)) in syms.iter().enumerate() {
            if *start < line + 64 && *end > line {
                sums[k] += count;
                hit = true;
            }
        }
        if !hit {
            no_sym += count;
        }
    }
    let mut rows: Vec<(f64, Option<(u32, u32)>, &str)> = syms.iter().enumerate()
        .filter(|&(k, _)| sums[k] > 0)
        .map(|(k, (start, end, name))| (sums[k] as f64 / halves as f64, Some((*start, *end)), name.as_str()))
        .collect();
    if no_sym > 0 {
        rows.push((no_sym as f64 / halves as f64, None, "(no symbol)"));
    }
    rows.sort_by(|a, b| b.0.total_cmp(&a.0).then_with(|| a.2.cmp(b.2)));
    let mut cum_kb = 0.0;
    for &(avg, size, name) in rows.iter().take(args.footprint) {
        cum_kb += avg * 64.0 / 1024.0;
        let (lines, bytes) = match size {
            Some((start, end)) => (((end - 1) / 64 - start / 64 + 1).to_string(), format!("{} B", end - start)),
            None => (String::new(), String::new()),
        };
        eprintln!("  {avg:6.1}  {lines:>5}  {bytes:>8}  {cum_kb:6.1} KB  {name}");
    }
    Ok(())
}

// ------------------------------------------------------------------------------------------ run --

/// Set FX LATCH through the existing editor protocol, outside the measurement window.
fn set_latch(emu: &mut Emu, on: bool) -> Result<(), String> {
    let [lo, hi] = v14(i32::from(on));
    let r = emu.ed(ED_MENU_SET, &[MENU_FX_LATCH, lo, hi])?;
    if r.get(1..) != Some(&[MENU_FX_LATCH, lo, hi][..]) || !matches!(r.first(), Some(0 | 3)) {
        return Err(format!("MENU FX LATCH {on} answered {r:02x?}"));
    }
    Ok(())
}

/// Reset controls and re-strike notes between scenarios without repeating boot or patch upload.
/// The synths and effect tails keep running; --clear applies this same preparation to a single run.
fn clear_scenario(emu: &mut Emu, scenario: Scenario) -> Result<(), String> {
    emu.probes.clear();
    emu.drum_next = None;
    let t = emu.ms();
    for name in WHITE {
        emu.key(t, name, false);
    }
    for (p, &(_, _, n)) in PARTS.iter().enumerate() {
        for &note in &NOTES[..n] {
            emu.soc.usb.midi_send(&[0x80 | p as u8, note + 12 * p as u8, 0]);
        }
    }
    if emu.drum_last != 0 {
        emu.soc.usb.midi_send(&[0x83, emu.drum_last, 0]);
    }
    let r = emu.ed(ED_SONG, &[3])?;
    if r.get(..2) != Some(&[3, 0]) {
        return Err(format!("SONG STOP answered {r:02x?}"));
    }
    emu.run_until(emu.ms() + 150)?;
    set_latch(emu, true)?;
    let t = emu.ms();
    emu.tap(t, "G5", TAP_MS);
    emu.run_until(t + TAP_MS + 100)?;
    let left = emu.soc.read32(emu.syms.perf_held) | emu.soc.read32(emu.syms.perf_latched) | emu.soc.read32(emu.syms.perf_act);
    if left != 0 {
        return Err(format!("CLEAR left punch-ins active ({left:#x})"));
    }
    if emu.soc.read8(emu.syms.perf_page2) != 0 {
        let t = emu.ms();
        emu.act(t, Action::Enc { enc: 0, clicks: -1 });
        emu.run_until(t + WALK_MS)?;
    }
    if emu.soc.read8(emu.syms.perf_page2) != 0 || emu.soc.read8(emu.syms.kb_lock) & 2 == 0 {
        return Err("CLEAR did not restore PUNCH-IN 1/2".into());
    }
    set_latch(emu, scenario == Scenario::Stack)?;
    strike_chords(emu);
    let t = emu.ms();
    emu.tap(t, "PLAY", TAP_MS);
    emu.run_until(t + 1000)?;
    emu.soc.periph.audio.clear();
    eprintln!("[{:6} ms] controls cleared, notes re-struck and settled", emu.ms());
    Ok(())
}

/// The actions and minimum duration of one requested scenario, validated before starting the emulator.
fn scenario_plan(scenario: Scenario, ms: u64, seed: u32, punch: Option<&str>,
                 latch: bool) -> Result<Plan, String> {
    let plan = match scenario {
        Scenario::Dry => Plan::default(),
        Scenario::Keys => plan_keys(),
        Scenario::Stack => plan_stack(ms),
        Scenario::Churn => plan_churn(ms, seed),
        Scenario::Idle => Plan::default(),
        Scenario::Project => match punch {
            Some(spec) => plan_project_punch(spec, latch)?,
            None => Plan::default(),
        },
    };
    if plan.end + TAIL_MS > ms {
        return Err(format!("--scenario {} needs --ms {} at least", scenario.name(), plan.end + TAIL_MS));
    }
    Ok(plan)
}

/// Boot and upload once, then measure each scenario with its own actions, probes and counters.
fn run(args: &Args) -> Result<(), String> {
    let plans = args.scenarios.iter()
        .map(|&(scenario, ms)| scenario_plan(scenario, ms, args.seed, args.punch.as_deref(), args.punch_latch))
        .collect::<Result<Vec<_>, _>>()?;
    let syms = Syms::load(&args.elf)?;
    let strict = !args.fast;
    let mut emu = Emu::boot(&args.fwsc, syms, args.mhz, strict)?;
    let wall = std::time::Instant::now();
    wait_ready(&mut emu)?;
    if args.punch_latch {
        // (a device setting, not a project one: set before the project is put, as the menu command needs)
        set_latch(&mut emu, true)?;
    }
    if args.scenarios[0].0 == Scenario::Idle {
        let t = emu.ms();
        emu.run_until(t + 1000)?; // (the running average settled)
    } else if args.scenarios[0].0 == Scenario::Project {
        let path = args.project.as_deref().ok_or("--scenario project needs --project FILE")?;
        let bytes = std::fs::read(path).map_err(|e| format!("--project {path}: {e}"))?;
        put_project(&mut emu, &bytes)?;
        play_project(&mut emu, args.notes)?;
        if args.punch.is_some() {
            open_punch(&mut emu)?;
        }
    } else {
        if args.scenarios[0].0 == Scenario::Stack {
            set_latch(&mut emu, true)?;
        }
        heavy_patch(&mut emu)?;
        open_punch(&mut emu)?;
        let t = emu.ms();
        emu.tap(t, "PLAY", 150);
        emu.run_until(t + 1000)?; // (the chords' attacks past, the running average settled)
    }

    for (i, (&(scenario, ms), plan)) in args.scenarios.iter().zip(plans).enumerate() {
        if scenario != Scenario::Project && scenario != Scenario::Idle && (args.clear || i > 0) {
            clear_scenario(&mut emu, scenario)?;
        }
        println!("{}", measure(args, &mut emu, scenario, ms, plan, &wall)?);
    }
    Ok(())
}

/// The stages line: each part's render on whichever core ran it, its engine, inserts and sends included, and
/// bus_run (reverb + delay), as means of the firmware's own per-block costs over the scenario's samples, scaled
/// to one audio half (HALF_FRAMES / CTL blocks); each in us and as a share of the half (2902 us). Inlined stages
/// such as master_out, events_block and core 0's wait for core 1 stay inside their callers in --profile.
fn stages_line(sums: &[u64; 5], n: u64) -> String {
    let blocks = f64::from(HALF_FRAMES / CTL);
    let stages: Vec<String> = ["part 1", "part 2", "part 3", "part 4", "bus_run"].iter().enumerate()
        .map(|(k, name)| {
            let us = (sums[k] as f64 / n as f64 / 24.0 * blocks).round();
            format!("{name} {us:.0} us ({:.1} %)", us * 100.0 / HALF_US)
        })
        .collect();
    format!("stages: {}", stages.join(", "))
}

/// Measure a prepared machine, keeping all reported counters and audio local to this scenario.
fn measure(args: &Args, emu: &mut Emu, scenario: Scenario, ms: u64, plan: Plan,
           wall: &std::time::Instant) -> Result<String, String> {
    let strict = !args.fast;

    // the scenario: its counters from zero
    let t0 = emu.ms();
    let magic = emu.dbg(0);
    if magic != DBG_MAGIC {
        return Err(format!("felucca_dbg magic {magic:#010x}, expected {DBG_MAGIC:#010x}"));
    }
    for w in [DBG_MAX_US, DBG_LATE, DBG_LATE_OVER, DBG_LATE_T5, DBG_LATE_OTH, DBG_WALL_MAX] {
        emu.soc.write32(emu.syms.dbg + 4 * w, 0);
    }
    let halves0 = emu.dbg(DBG_HALVES);
    let shed0 = emu.soc.read32(emu.syms.shed_count);
    let kills0 = emu.soc.read32(emu.syms.voice_kills);
    let resets0 = emu.soc.resets;
    let audio0 = emu.soc.periph.audio.len();
    eprintln!("[{t0:6} ms] scenario {} for {} ms ({:.1} s wall so far)", scenario.name(), ms, wall.elapsed().as_secs_f64());
    for (t, a) in &plan.acts {
        match a {
            Act::Key(name, down) => emu.key(t0 + t, name, *down),
            Act::Select(dir) => emu.act(t0 + t, Action::Enc { enc: 0, clicks: *dir }),
        }
    }
    emu.probes = plan.probes.into_iter().map(|p| Probe { from: t0 + p.from, to: t0 + p.to, ..p }).collect();
    // (perf_act only once the scenario runs)
    emu.act_seen = 0;
    let mut cpu_q8_max = 0;
    let mut cpu1_q8_max = 0;
    let mut next_sample = t0;
    // the firmware's own per-block costs at each sample: the four parts, then bus_run
    let mut stage_sums = [0u64; 5];
    let mut stage_n = 0u64;
    // --profile: each CHUNK's cycles charged to the function each core is in at its end (a sampling profile
    // weighted by device time); core 0 outside the audio ISR (felucca_dbg.in_audio) as "(main loop)", core 1 in
    // its job loop as "(idle)"
    let funcs = if args.profile > 0 { Some(Funcs::load(&args.elf)?) } else { None };
    let mut prof: [HashMap<String, u64>; 2] = [HashMap::new(), HashMap::new()];
    let mut last_cycles = emu.soc.periph.cycles;
    let misses0 = emu.soc.xip_cache.as_ref().map(|c| c.misses);
    let mut footprint = if args.footprint > 0 { Some(Footprint::start(emu)) } else { None };
    while emu.ms() < t0 + ms {
        emu.step()?;
        if let Some(fp) = &mut footprint {
            let half = emu.dbg(DBG_HALVES);
            if half != fp.half {
                fp.half = half;
                fp.boundary(emu);
            }
        }
        if let Some(f) = &funcs {
            let d = emu.soc.periph.cycles - last_cycles;
            last_cycles = emu.soc.periph.cycles;
            let c0 = if emu.dbg(DBG_IN_AUDIO) != 0 { f.name(emu.cpu.pc) } else { "(main loop)" };
            *prof[0].entry(c0.to_string()).or_default() += d;
            if let Some(c1) = &emu.cpu1 {
                let n = f.name(c1.pc);
                *prof[1].entry(if n == "fm1_c1_main" { "(idle)" } else { n }.to_string()).or_default() += d;
            }
        }
        if emu.ms() >= next_sample {
            cpu_q8_max = cpu_q8_max.max(emu.dbg(DBG_CPU_Q8));
            cpu1_q8_max = cpu1_q8_max.max(emu.dbg(DBG_CPU1_Q8));
            if let (Some(part_cost), Some(bus_cost)) = (emu.syms.part_cost, emu.syms.bus_cost) {
                for (k, sum) in stage_sums.iter_mut().take(4).enumerate() {
                    *sum += u64::from(emu.soc.read16(part_cost + 2 * k as u32));
                }
                stage_sums[4] += u64::from(emu.soc.read16(bus_cost));
                stage_n += 1;
            }
            next_sample += 10;
        }
    }
    if let Some(f) = &funcs {
        // the code the render ran, against the 32 KB flash cache: every function either core spent time in (not the
        // main loop or core 1's wait), with its size from the ELF
        let mut hot: Vec<(u32, &str)> = f.0.iter()
            .filter(|(_, _, n)| prof.iter().any(|p| p.contains_key(n.as_str())))
            .filter(|(s, _, _)| *s >= 0x0200_0000)
            .map(|(s, e, n)| (e - s, n.as_str())).collect();
        hot.sort_by(|a, b| b.0.cmp(&a.0));
        let total: u32 = hot.iter().map(|h| h.0).sum();
        eprintln!("profile: the render's code in flash: {} functions, {total} bytes; the largest: {}", hot.len(),
            hot.iter().take(8).map(|(sz, n)| format!("{n} {sz}")).collect::<Vec<_>>().join(", "));
    }
    if let (Some(_), Some(m0), Some(c)) = (&funcs, misses0, emu.soc.xip_cache.as_ref()) {
        // the flash cache is shared by both cores; a miss stalls the core that made it ~300 cycles
        let (m, halves) = (c.misses - m0, ms * 44_100 / 1000 / 128);
        eprintln!("profile: {m} flash cache misses ({:.1} a half of 128 frames, ~{:.0} us of 2902)", m as f64 / halves as f64,
            m as f64 / halves as f64 * fm1_soc::XipCache::MISS_TICKS as f64 / 8.0 / 240.0);
    }
    for (core, p) in prof.iter().enumerate().filter(|(_, p)| !p.is_empty()) {
        let total: u64 = p.values().sum();
        let mut rows: Vec<(&String, &u64)> = p.iter().collect();
        rows.sort_by(|a, b| b.1.cmp(a.1));
        eprintln!("profile core {core} (share of its time in the scenario):");
        for (name, c) in rows.into_iter().take(args.profile) {
            let at = funcs.as_ref().and_then(|f| f.0.iter().find(|(_, _, n)| n == name));
            let size = at.map_or(String::new(), |(s, e, _)| {
                format!("  {} B {}", e - s, if *s >= 0x0200_0000 { "flash" } else { "RAM" })
            });
            eprintln!("  {:5.1} %  {name}{size}", *c as f64 * 100.0 / total as f64);
        }
    }

    if stage_n > 0 {
        eprintln!("{}", stages_line(&stage_sums, stage_n));
    } else {
        eprintln!("stages: part_cost / bus_cost not in this ELF");
    }

    if let Some(fp) = &footprint {
        footprint_report(args, fp)?;
    }

    let magic = emu.dbg(0);
    if magic != DBG_MAGIC {
        return Err(format!("felucca_dbg magic {magic:#010x} at the end, expected {DBG_MAGIC:#010x}"));
    }
    if emu.soc.resets != resets0 {
        return Err(format!("the chip reset {} time(s) during the scenario", emu.soc.resets - resets0));
    }
    if let Some(path) = &args.wav {
        write_wav(path, &emu.soc.periph.audio[audio0..]).map_err(|e| format!("{path}: {e}"))?;
    }
    let audio = &emu.soc.periph.audio[audio0..];
    let rms = (audio.iter().map(|&s| (s as f64).powi(2)).sum::<f64>() / audio.len().max(1) as f64).sqrt();
    let peak = audio.iter().map(|&s| (s as i32).abs()).max().unwrap_or(0);
    eprintln!("[{:6} ms] done in {:.1} s wall; audio rms {rms:.0} peak {peak} ({} samples); perf_act seen {:#x}",
        emu.ms(), wall.elapsed().as_secs_f64(), audio.len(), emu.act_seen);
    let mut failed = Vec::new();
    for p in &emu.probes {
        eprintln!("  punch-in {:<28} {}", p.label, if p.seen { "engaged" } else { "NOT engaged" });
        if !p.seen {
            failed.push(p.label.clone());
        }
    }
    /* a project's punch lane is part of the music: its effects may be at work at any instant (the punch scenarios
     * release every key, so theirs must not be) */
    let left = emu.soc.read32(emu.syms.perf_held) | emu.soc.read32(emu.syms.perf_latched) | emu.soc.read32(emu.syms.perf_act);
    if left != 0 && scenario != Scenario::Project {
        failed.push(format!("still at work at the end ({left:#x})"));
    }
    if !failed.is_empty() {
        return Err(format!("scenario {}: {}", scenario.name(), failed.join(", ")));
    }

    let halves = emu.dbg(DBG_HALVES).wrapping_sub(halves0);
    // flash cache misses a half, strict mode only (--fast models no cache)
    let misses_half = match (misses0, emu.soc.xip_cache.as_ref()) {
        (Some(m0), Some(c)) if halves > 0 => format!("{:.1}", (c.misses - m0) as f64 / f64::from(halves)),
        _ => "null".to_string(),
    };
    let [max_us, late, late_over, late_t5, late_oth, wall_max] =
        [DBG_MAX_US, DBG_LATE, DBG_LATE_OVER, DBG_LATE_T5, DBG_LATE_OTH, DBG_WALL_MAX].map(|k| emu.dbg(k));
    let shed = emu.soc.read32(emu.syms.shed_count).wrapping_sub(shed0);
    let kills = emu.soc.read32(emu.syms.voice_kills).wrapping_sub(kills0);
    Ok(format!(
        "{{\"scenario\":\"{}\",\"ms\":{},\"strict\":{strict},\"mhz\":{},\"halves\":{halves},\"cpu_pct\":{:.1},\
\"cpu1_pct\":{:.1},\"misses_half\":{misses_half},\"audio_max_us\":{max_us},\"late\":{late},\"late_over\":{late_over},\"late_t5\":{late_t5},\
\"late_oth\":{late_oth},\"wall_max_us\":{wall_max},\"voices_shed\":{shed},\"voices_given_up\":{kills}}}",
        scenario.name(),
        ms,
        args.mhz,
        cpu_q8_max as f64 * 100.0 / 256.0,
        cpu1_q8_max as f64 * 100.0 / 256.0,
    ))
}

/// 44.1 kHz stereo 16-bit PCM, as fm1-emu writes its --wav.
fn write_wav(path: &str, samples: &[i16]) -> std::io::Result<()> {
    use std::io::Write;
    let data_len = (samples.len() * 2) as u32;
    let mut f = std::io::BufWriter::new(std::fs::File::create(path)?);
    f.write_all(b"RIFF")?;
    f.write_all(&(36 + data_len).to_le_bytes())?;
    f.write_all(b"WAVEfmt ")?;
    f.write_all(&16u32.to_le_bytes())?;
    f.write_all(&1u16.to_le_bytes())?;
    f.write_all(&2u16.to_le_bytes())?;
    f.write_all(&44100u32.to_le_bytes())?;
    f.write_all(&(44100u32 * 4).to_le_bytes())?;
    f.write_all(&4u16.to_le_bytes())?;
    f.write_all(&16u16.to_le_bytes())?;
    f.write_all(b"data")?;
    f.write_all(&data_len.to_le_bytes())?;
    for s in samples {
        f.write_all(&s.to_le_bytes())?;
    }
    f.flush()
}
