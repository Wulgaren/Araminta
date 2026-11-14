// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//! emu_test: Felucca's firmware in the fm1-emu emulator, device-timed (fm1-live's FM1_STRICT: each
//! instruction its cost measured on an FM-1, the 32 KB XIP cache), playing a heavy patch with punch-ins;
//! prints the firmware's own load counters (audio.c felucca_dbg, shed_count, voice_kills) as one JSON line.
//!
//!   emu_test --fwsc build/felucca.fwsc --elf build/felucca.elf --scenario dry|keys|stack|churn --ms N
//!            [--wav OUT.wav] [--mhz 240] [--fast] [--seed N]
//!
//! The run: boot, USB-MIDI configured, the heavy patch set up through the editor's SysEx (editor.c) and
//! played with MIDI notes on channels 1..4, the FX layer's PUNCH-IN 1/2 page opened with the FX button,
//! PLAY; then the scenario's N ms (--ms counts from there, not from power-on). The counters' maxima and
//! late counts are zeroed when those N ms start, so they cover the scenario only; `halves` counts its
//! halves; `cpu_pct` is the highest the firmware's running average (cpu_q8) reached in it. --wav writes
//! the scenario's audio. --fast: no cost model, no XIP cache, the JIT (quick, not device-timed).
//! Exit 1 with a message on stderr: boot, a CPU fault, a bad DBG magic, a setup the device did not take,
//! or a punch-in that never engaged / did not let go.

use std::collections::HashMap;
use std::process::ExitCode;

use fm1_core::{Bus, Cpu};
use fm1_soc::{machine, Action, Soc, RAM_BASE};

const RAM_SIZE: usize = 1 << 20;
/// Instructions a `run` call: about 50 us of the device, the granularity of the host's MIDI and RAM probes.
const CHUNK: u32 = 10_000;
const USAGE: &str = "usage: emu_test --fwsc FELUCCA.fwsc --elf FELUCCA.elf --scenario dry|keys|stack|churn --ms N \
[--wav OUT.wav] [--mhz 240] [--fast] [--seed N]";

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

// editor.c's SysEx commands (F0 7D 46 4C cmd args.. F7) and editor_menu.c's FX LATCH id: protocol constants,
// append-only by their own rules (web/EDITOR_PROTOCOL.md; "an id keeps its meaning for good")
const ED_PING: u8 = 25;
const ED_PRESET: u8 = 8;
const ED_SET: u8 = 3;
const ED_TRACK: u8 = 27;
const ED_TRACK_PARAM: u8 = 31;
const ED_MENU_SET: u8 = 73;
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
}

impl Scenario {
    fn parse(s: &str) -> Option<Self> {
        Some(match s {
            "dry" => Scenario::Dry,
            "keys" => Scenario::Keys,
            "stack" => Scenario::Stack,
            "churn" => Scenario::Churn,
            _ => return None,
        })
    }
    fn name(self) -> &'static str {
        match self {
            Scenario::Dry => "dry",
            Scenario::Keys => "keys",
            Scenario::Stack => "stack",
            Scenario::Churn => "churn",
        }
    }
}

struct Args {
    fwsc: String,
    elf: String,
    scenario: Scenario,
    ms: u64,
    wav: Option<String>,
    mhz: u64,
    fast: bool,
    seed: u32,
}

fn parse_args() -> Result<Args, String> {
    let a: Vec<String> = std::env::args().skip(1).collect();
    let (mut fwsc, mut elf, mut scenario, mut ms) = (None, None, None, None);
    let (mut wav, mut mhz, mut fast, mut seed) = (None, 240u64, false, 1u32);
    let mut i = 0;
    while i < a.len() {
        let val = |i: usize| a.get(i + 1).cloned().ok_or_else(|| format!("{} needs a value", a[i]));
        let num = |i: usize| val(i)?.parse::<u64>().map_err(|_| format!("{}: not a number", a[i]));
        match a[i].as_str() {
            "--fwsc" => fwsc = Some(val(i)?),
            "--elf" => elf = Some(val(i)?),
            "--scenario" => scenario = Some(Scenario::parse(&val(i)?).ok_or("--scenario: dry, keys, stack or churn")?),
            "--ms" => ms = Some(num(i)?),
            "--wav" => wav = Some(val(i)?),
            "--mhz" => mhz = num(i)?,
            "--seed" => seed = u32::try_from(num(i)?).map_err(|_| "--seed: at most 32 bits")?,
            "--fast" => {
                fast = true;
                i += 1;
                continue;
            }
            other => return Err(format!("unknown argument {other}")),
        }
        i += 2;
    }
    match (fwsc, elf, scenario, ms) {
        (Some(fwsc), Some(elf), Some(scenario), Some(ms)) if mhz > 0 => {
            Ok(Args { fwsc, elf, scenario, ms, wav, mhz, fast, seed })
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
        Ok(json) => {
            println!("{json}");
            ExitCode::SUCCESS
        }
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
}

/// Reads `path`'s ELF32 (little-endian) symbol table into name -> (value, size). A name defined twice
/// (two file-local statics) maps to None, so a lookup of it fails rather than picks one.
fn elf_symbols(path: &str) -> Result<HashMap<String, Option<(u32, u32)>>, String> {
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
    let mut out: HashMap<String, Option<(u32, u32)>> = HashMap::new();
    for s in (symtab.1..symtab.1 + symtab.2).step_by(16) {
        let (Some(name), Some(value), Some(size)) = (u32_at(s), u32_at(s + 4), u32_at(s + 8)) else { return Err(bad()) };
        let Some(rest) = strtab.get(name as usize..) else { return Err(bad()) };
        let end = rest.iter().position(|&b| b == 0).unwrap_or(rest.len());
        let name = String::from_utf8_lossy(&rest[..end]).into_owned();
        if name.is_empty() {
            continue;
        }
        out.entry(name).and_modify(|v| *v = None).or_insert(Some((value, size)));
    }
    Ok(out)
}

impl Syms {
    fn load(path: &str) -> Result<Self, String> {
        let t = elf_symbols(path)?;
        let get = |name: &str, size: u32| -> Result<u32, String> {
            match t.get(name) {
                Some(Some((addr, sz))) if *sz == size => Ok(*addr),
                Some(Some((_, sz))) => Err(format!("{path}: {name} is {sz} bytes, expected {size}")),
                Some(None) => Err(format!("{path}: {name} is defined more than once")),
                None => Err(format!("{path}: no symbol {name}")),
            }
        };
        Ok(Syms {
            dbg: get("felucca_dbg", 96)?,
            shed_count: get("shed_count", 4)?,
            voice_kills: get("voice_kills", 4)?,
            perf_held: get("perf_held", 4)?,
            perf_latched: get("perf_latched", 4)?,
            perf_act: get("perf_act", 4)?,
            kb_lock: get("kb_lock", 1)?,
        })
    }
}

// felucca_dbg's words (audio.c struct felucca_dbg)
const DBG_MAGIC: u32 = 0x4442_4731;
const DBG_HALVES: u32 = 1;
const DBG_MAX_US: u32 = 2;
const DBG_LATE: u32 = 5;
const DBG_CPU_Q8: u32 = 9;
const DBG_LATE_OVER: u32 = 19;
const DBG_LATE_T5: u32 = 20;
const DBG_LATE_OTH: u32 = 21;
const DBG_WALL_MAX: u32 = 22;

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
/// any of them, or (`all`) all of them at once.
struct Probe {
    label: String,
    from: u64,
    to: u64,
    mask: u32,
    latched: bool,
    all: bool,
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
            for p in self.probes.iter_mut().filter(|p| (p.from..p.to).contains(&now)) {
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
    let [lo, hi] = v14(120);
    let r = emu.ed(ED_SET, &[1, G_BPM, lo, hi])?;
    if r != [1, G_BPM, lo, hi] {
        return Err(format!("BPM 120 answered {r:02x?}"));
    }
    for (p, &(_, _, n)) in PARTS.iter().enumerate() {
        for &note in &NOTES[..n] {
            emu.soc.usb.midi_send(&[0x90 | p as u8, note + 12 * p as u8, 100]);
        }
    }
    emu.drum_next = Some(emu.ms());
    eprintln!("[{:6} ms] heavy patch set up, chords held, drums on 16ths", emu.ms());
    Ok(())
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
        self.probes.push(Probe { label, from, to, mask, latched, all, seen: false });
    }
}

const HOLD_MS: u64 = 300;
const GAP_MS: u64 = 200;
const TAP_MS: u64 = 100;
/// after a page walk, the keys wait this long (the encoder's detent, then the UI loop's frame)
const WALK_MS: u64 = 300;
/// the scenario's last this-many ms are quiet: every punch-in must have let go
const TAIL_MS: u64 = 400;

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

// ------------------------------------------------------------------------------------------ run --

fn run(args: &Args) -> Result<String, String> {
    let plan = match args.scenario {
        Scenario::Dry => Plan::default(),
        Scenario::Keys => plan_keys(),
        Scenario::Stack => plan_stack(args.ms),
        Scenario::Churn => plan_churn(args.ms, args.seed),
    };
    if plan.end + TAIL_MS > args.ms {
        return Err(format!("--scenario {} needs --ms {} at least", args.scenario.name(), plan.end + TAIL_MS));
    }
    let syms = Syms::load(&args.elf)?;
    let strict = !args.fast;
    let mut emu = Emu::boot(&args.fwsc, syms, args.mhz, strict)?;
    let wall = std::time::Instant::now();
    wait_ready(&mut emu)?;
    if args.scenario == Scenario::Stack {
        let [lo, hi] = v14(1);
        let r = emu.ed(ED_MENU_SET, &[MENU_FX_LATCH, lo, hi])?;
        if r.get(1..) != Some(&[MENU_FX_LATCH, lo, hi][..]) {
            return Err(format!("MENU FX LATCH ON answered {r:02x?}"));
        }
    }
    heavy_patch(&mut emu)?;
    open_punch(&mut emu)?;
    let t = emu.ms();
    emu.tap(t, "PLAY", 150);
    emu.run_until(t + 1000)?; // (the chords' attacks past, the running average settled)

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
    eprintln!("[{t0:6} ms] scenario {} for {} ms ({:.1} s wall so far)", args.scenario.name(), args.ms, wall.elapsed().as_secs_f64());
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
    let mut next_sample = t0;
    while emu.ms() < t0 + args.ms {
        emu.step()?;
        if emu.ms() >= next_sample {
            cpu_q8_max = cpu_q8_max.max(emu.dbg(DBG_CPU_Q8));
            next_sample += 10;
        }
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
    let left = emu.soc.read32(emu.syms.perf_held) | emu.soc.read32(emu.syms.perf_latched) | emu.soc.read32(emu.syms.perf_act);
    if left != 0 {
        failed.push(format!("still at work at the end ({left:#x})"));
    }
    if !failed.is_empty() {
        return Err(format!("scenario {}: {}", args.scenario.name(), failed.join(", ")));
    }

    let halves = emu.dbg(DBG_HALVES).wrapping_sub(halves0);
    let [max_us, late, late_over, late_t5, late_oth, wall_max] =
        [DBG_MAX_US, DBG_LATE, DBG_LATE_OVER, DBG_LATE_T5, DBG_LATE_OTH, DBG_WALL_MAX].map(|k| emu.dbg(k));
    let shed = emu.soc.read32(emu.syms.shed_count).wrapping_sub(shed0);
    let kills = emu.soc.read32(emu.syms.voice_kills).wrapping_sub(kills0);
    Ok(format!(
        "{{\"scenario\":\"{}\",\"ms\":{},\"strict\":{strict},\"mhz\":{},\"halves\":{halves},\"cpu_pct\":{:.1},\
\"audio_max_us\":{max_us},\"late\":{late},\"late_over\":{late_over},\"late_t5\":{late_t5},\"late_oth\":{late_oth},\
\"wall_max_us\":{wall_max},\"voices_shed\":{shed},\"voices_given_up\":{kills}}}",
        args.scenario.name(),
        args.ms,
        args.mhz,
        cpu_q8_max as f64 * 100.0 / 256.0,
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
