# Changelog

## Unreleased

### LFO

- **S&H SMOOTH.** LFO > LFO 2's fourth knob (`SMTH`, 0..100 %) rounds the WAVE S&H steps into a smooth,
  sine-like random curve. It shows only while WAVE is S&H; 0 is the hard steps, bit for bit. The matrix's
  S&H / SLEW sources are unchanged. It uses the long-inert PRIO parameter's id, so projects stay FUN16.

## 3.1

A public-domain CR-78 drum kit joins SAMPLE, and the web editor's Samples tab becomes one card per USR
slot. Same project and settings format as 3.0; splash and ABOUT show `v3.1`.

### SAMPLE

- **CR78 factory kit.** SAMPLE gains a public-domain CR-78 set at **SET 8**, after USR1..USR3 so their
  indices stay 5..7. Eight one-shots map to the SEQ drum lanes (36, 38, 39, 42, 46, 45, 37, 56) and
  show the drum grid; preset 5 selects it. Source: Oramics "Sampled" CR-78 (Boxed Ear), public domain.
- **Samples tab.** One card per USR slot with **Sample / Drumkit / Wavetable**; Drumkit maps roots to
  exact lane notes, Wavetable takes a table file or a folder medley, and the editor-only name field is
  gone. The FM-1 still shows USR1..3.
- **WAVETABLE.** OSC B's knob is **LEVEL** (B LEVEL in MOD / AUTOMATION); the EDIT graph draws B's own
  wave on OSC B and A, labelled OSC A, while RATIO is hot there.
- **Fix.** CR78 lanes no longer fall back to the missing-sample sine: the kick, snare and hats play
  their real one-shots.

## 3.0

A fifteenth engine, WAVETABLE, brings two morphing wavetable oscillators and FM from B to every track, and the three user slots now hold complete wavetables made from a Serum `.wav` or a folder of single cycles. Install from the [web installer](https://wulgaren.github.io/Araminta/) (Chrome or Edge, USB). Try it first in the [browser](https://wulgaren.github.io/Araminta/webapp/try/).

Same project format as 2.8 (FUN16 / `FUNG`). Settings move to PER5; older settings load and migrate. Splash and ABOUT show `v3.0`.

### WAVETABLE

- **Two oscillators, one table each.** A and B read a 16-frame table with a mip chain, frame-major, in XIP; A-POS / B-POS morph through the frames and the mip level is chosen per block and voice, so a high note reads a short, band-limited level. BASIC and SPECTRAL are factory tables (16 x 512 samples down to 8, 65,024 B for both) and eight presets ship with the engine: WT BASS, FM LEAD, GLASS FM, WT PAD, GROWL, DIGITAL, PLUCK, SWEEP.
- **FM and feedback.** FDBK feeds A back into its own phase (up to 0.60 cycle) and B>A phase-modulates A (up to one cycle); LEVEL at 0 leaves B as a modulator only, and RATIO tunes B against A at 0.25, 0.5, 0.75, 1, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 12 or 16. FLT moves A-POS and SHP moves B>A; there is no engine filter.
- **Pages.** EDIT has OSC A (WAVE, POS, FDBK, B>A) and OSC B (WAVE, POS, LEVEL, RATIO); MOD DST, AUTOMATION and step locks name the two oscillators WAVE A / POSITION A / WAVE B / POSITION B. The EDIT graph draws A after feedback and FM on OSC A, B's own wave on OSC B, and A (labelled OSC A) while RATIO is the hot knob there, since RATIO changes A's FM and not B's wave.
- Editor: engine 14 in the list (NENGINES 15), the eight parameters, both pages and the protocol are in `web/EDITOR_PROTOCOL.md`; `engines.c` lists it after DRUM.

### User wavetables

- **One table per slot.** USR1..USR3 share the SAMPLE engine's three user slots. A slot holds an `FWT1` header at 0 and int16 PCM at +512, frame-major with a mip chain: 16 frames x 1024 samples by default (65,280 B), and any layout that fits 81,408 B is accepted (40 x 512 is 81,280 B). An empty, sample or invalid slot keeps the sine fallback.
- **Protocol.** SMP_BEGIN / WRITE / END / ERASE / INFO handle either header type; END validates the header and the data CRC-32, SMP_INFO bit 5 marks a wavetable, and backup ids 32..34 carry a sample or a wavetable. The published-slot rules are the sample's: WRITE waits for SMP_BEGIN (rc 4) and a different header is refused (rc 2). Uploads are PCM, not ADPCM.
- **Conversion and upload.** `tools/wavetableio.py` reads a Serum `.wav` (the `clm` frame size, every frame), keeps 16 frames, resamples each cycle to 1024 with an FFT and builds the mip chain; it also makes a medley from one cycle per file (a larger folder is sampled evenly) and exports a Serum-style `.wav`. `tools/fm1_wavetable_upload.py` loads, medleys, erases and builds over the sample commands. The web editor has the same converter, byte-identical to Python (`web/test_web.mjs`), with a per-slot table picker, a folder/medley bar, and upload, erase and `.wav` export.

### Settings

- PER5 (`PER5`, 0x50455235): MIDI LEARN's 16 CCs move from `favorites.factory[14]` to `[13]`, the retired SLICE row, and row 14 becomes WAVETABLE's favourites. A PER4 import moves the map and clears row 14; PER1..PER3 still import, and the editor backup accepts PER4.

### Fixes

- BPM can read 0 for a moment while globals load. `beat_samples()` (the FX delay's tempo maths) and the SLICER's `sl_enter()` now read it as 120, the default, instead of dividing by zero (#282).

## 2.8

Projects move to FUN16 with 88 motion records, the sequencer's steps take nine bytes each in RAM, and the panel receives only the bands that changed. Install from the [web installer](https://wulgaren.github.io/Araminta/) (Chrome or Edge, USB). Try it first in the [browser](https://wulgaren.github.io/Araminta/webapp/try/).

New project format: FUN16 (`FUNG`), the same 3,840-byte object as FUN15. FUN15 and older load; only FUN16 is written. Splash and ABOUT show `v2.8`.

### Projects

- **FUN16 (`FUNG`).** A song section packs into 2 bytes after the 4-byte header (4 slots x 3 bits, repeat - 1 x 4 bits): 36 bytes instead of 84, which pays for 16 more motion records in the same 3,840-byte object — 88 on disk instead of 72. The RAM motion pool grows 128 -> 192, more than a project saves, so a session keeps its locks until the save clips them and shows the save-limit warning. FUN15 and older still load; the writer only emits FUN16. Firmware that only knows FUN15 refuses a FUN16 slot (EMPTY until saved over), so keep a backup before the first save; the upgrade is one-way for anything re-saved.
- Editor: INFO answers `41 01 40 01` (192 records) and MOTION op 8 lists up to 192, in a 984-byte reply. The web backup knows `FUNG`.

### Memory

- The in-RAM step is the 9-byte FUN15 wire step, bit for bit: note j with its nudge bit in bit 7, n | time << 3 | accent/slide << 5, vel | ratchet bit 0 << 7, hit, acc & hit, probability | ratchet bit 1 << 7. No format bump and no migration: `proj_pack` / `proj_unpack` write and read the same bytes as before; accessors hide the packing and keep the ratchet / nudge top bits. RAM 69,336 -> 66,648 B (-2,688), pool 354,472 -> 353,704 B (-768), image +1,184 B.
- FUN16 spends 1,280 B of that RAM back on the 192-record motion pool: RAM 67,928 B (5,800 free), pool 354,920 B (13,720 free).

### Performance

- The strip cache hashes each 30 px column group of every 8-row band: a band the panel already shows is neither expanded nor sent, and a changed band goes out from its first to its last dirty group. `cv_strip` splits into two 8-row halves, so the next half expands while the previous one is on the wire. Fills, power, wake, one-shot screens and a counted LCD timeout drop the bands they touch; `ui.force` drops everything. The console shows `ui_draw_us_max` / `ui_draw_us_avg`, `lcd_bytes` and `lcd_skips`.
- The rest of the UI and editor pages build at LLVM `minsize` (clang 4 has no `-Oz`), flash only: image 461,424 -> 459,392 B (-2,032), RAM and pool unchanged, `.ram_text` and `.c1_text` unchanged. `BUILDING.md` describes the XIP layout and the `minsize` / `-Os` split; `FELUCCA_SIZE=0` builds everything at `-Os` for comparison.

## 2.7

An overload that runs past the deadline sheds two voices at once, and the editor's project slots line up one per row. Install from the [web installer](https://wulgaren.github.io/Araminta/) (Chrome or Edge, USB). Try it first in the [browser](https://wulgaren.github.io/Araminta/webapp/try/).

Same project format as 2.6 (FUN15 / `FUNF`). Splash and ABOUT show `v2.7`.

### Audio

- A half past 100 % of the deadline now sheds two voices at once. A burst around 110 % leaves one voice's share short, and the next half would be late anyway. The request is carried out at the next half's start, each voice fading over the block. A single half over 92 % and two 85 % halves in a row still shed one.

### Editor

- Project slots A to D are one row each: letter, used or empty, Load / Save, then Export / Import. The six controls per slot used to flow through a four-column grid, which put one slot's buttons beside the next slot's label.

## 2.6

The FX punch-ins reach every effect again on two fixed pages, the second core takes the parts it actually costs, and an overload thins a voice the moment a half runs hot. Install from the [web installer](https://wulgaren.github.io/Araminta/) (Chrome or Edge, USB). Try it first in the [browser](https://wulgaren.github.io/Araminta/webapp/try/).

Same project format as 2.5 (FUN15 / `FUNF`). Splash and ABOUT show `v2.6`.

### Performance

- **The FX punch-in maps are two fixed pages — the reassignment is gone.** Under PUNCH-IN, the white keys hold a fixed map; SELECT walks to the second page and every effect is reachable again. PUNCH-IN 1/2: REPEAT 1/8 1/16 1/32, 1/2 TEMPO, LPF, HPF, OCT- / OCT+, DEC- / DEC+, STUTTER 1/8 1/16, ATK+, ARP, RANDOM (G5 free). PUNCH-IN 2/2: REVERSE, TAPE STOP, FREEZE, PITCH UP / DN, FLANGER, PHASER, REV THROW, ECHO THROW (G4 and G5 free). The FX layer keeps the page's own bottom bar (engine, sound, the PUNCH page title — no hint row). FX LATCH, the black-key mutes and A#4's ALL / SYN / DRM step behave as before on both pages. The 1.2 hold-key + PRESETS assign, EDIT's default reset and the saved key map (settings bytes 15][14..23) are retired: settings still keep those bytes, unused.

### Second core

- The block's splits are weighed by each part's measured render, no longer by the estimate formula: estimated ~35 % low on real projects (FM6 324 against 589, GRAIN 0 against 25) and left core 1 underused at about a quarter of the work. Measured where each part runs (TIMER4 ticks, core 0 in the mix, core 1 reporting its own parts back through the mailbox), the same splitter balances them — the busy core drops about 5 % of the render per block and keeps track as voices move. Core 1's reads are proved once at boot; a core that cannot read TIMER4 fails the probe and keeps the estimate (the console's `c1_t4_ok` tells which).
- The insert-SEND copy in `ins_bus_block` runs only for stages a SEND reads: readers or not it copied all six stages every block, ~16 instructions/sample on projects without an insert SEND.

### Audio

- Overload sheds a voice on a single half over 92 % of the deadline, not only on two 85 % halves in a row: a load that sits at 56–79 % between its spikes never built the pair, so the protection sat out the exact moments it exists for. A hot burst now thins a voice per half instead of replaying whole halves. The console's `late` counter splits by cause (render / nested TIMER5 / blocked entry), with `wall_max` and `entry_gap`.

## 2.5

The delay's TIME roll is the FM-1's own: one pole a sample, so the bend is how fast you turn, not how far. Install from the [web installer](https://wulgaren.github.io/Araminta/) (Chrome or Edge, USB). Try it first in the [browser](https://wulgaren.github.io/Araminta/webapp/try/).

Same project format as 2.4 (FUN15 / `FUNF`). Splash and ABOUT show `v2.5`.

### Effects

- Delay TIME: on the free-ms rates the read head follows the knob with the FM-1's own tape law — a one-pole walk toward the target, 0.00014391 of the remaining error per sample (tau 157 ms at 44.1 kHz), with no step cap and no direction. A sustained turn rides at the turn's own speed (455 → 25 ms in half a second: 0.82 samples a sample, about +11 st, landing in 1.6 s); an instant jump sweeps its own chirp. The 2.4 slew and its ±224 Q8 caps are gone. RATE mode and the snap onto a loaded time are unchanged.

### Performance

- **The FX punch-in maps are two fixed pages — the reassignment is gone.** Under PUNCH-IN, the white keys hold a fixed map; SELECT walks to the second page and every effect is reachable again. PUNCH-IN 1/2: REPEAT 1/8 1/16 1/32, 1/2 TEMPO, LPF, HPF, OCT- / OCT+, DEC- / DEC+, STUTTER 1/8 1/16, ATK+, ARP, RANDOM (G5 free). PUNCH-IN 2/2: REVERSE, TAPE STOP, FREEZE, PITCH UP / DN, FLANGER, PHASER, REV THROW, ECHO THROW (G4 and G5 free). The FX layer keeps the page's own bottom bar (engine, sound, the PUNCH page title — no hint row). FX LATCH, the black-key mutes and A#4's ALL / SYN / DRM step behave as before on both pages. The 1.2 hold-key + PRESETS assign, EDIT's default reset and the saved key map (settings bytes 15][14..23) are retired: settings still keep those bytes, unused.

## 2.4

The second core renders parts of the mix, the DIST MODEs keep their own character up to DRIVE 127, the delay's read head follows the knob, and an update refuses a package built for another board. Install from the [web installer](https://wulgaren.github.io/Araminta/) (Chrome or Edge, USB). Try it first in the [browser](https://wulgaren.github.io/Araminta/webapp/try/).

Same project format as 2.3 (FUN15 / `FUNF`). Splash and ABOUT show `v2.4`.

### Second core

- Parts render on both AC79 cores: each block's parts are split by their measured cost. A core that rejects its mailbox costs one part of one block (rendered on core 0) and is restarted, up to three times in a clean-run window; one that keeps failing, or never answers at boot, is left off for the session, silently — the FM-1 plays on one core. The UART console shows the counters.
- The update path holds the second core first (its waiting loop sits in RAM the ROM and the SPL use at boot). Staged updates are reliable with the core running.

### Effects

- DIST: one curve per MODE. SOFT a rounded curve that never flat-tops, HARD an edge at full scale, FOLD a wavefolder that folds further as it is driven, FUZZ a gated octave wave. Each has its own drive gain, so the MODEs stay distinct at DRIVE 127; all peak alike, so a MODE switch stays even (measured in `docs/audio-effects-comparison.md`). New DIST stages default DRIVE to 50 (MIX still starts at bypass).
- Delay TIME: on the free-ms rates the read head follows the knob (1/256 of the remaining error per sample, capped at 224 Q8), so pitch is how fast you turn, not how far — 455 → 25 ms turned in half a second lands in half a second.
- FX LATCH turned on while an FX key was held (a settings load) could leave the effect fully active with no key held until a power cycle. A release now always clears its held bit; deliberate latches stay sticky until toggled off or G5 CLR.

### Update

- The loader and OTA refuse a package whose `flash.bin` is not this board's app area exactly (another board's package, or one cut short) before any erase.

## 2.3

Drum pattern generation on DRUM-X, cleaner slicer recording when tracks share a SEND, and a native Windows build path. Install from the [web installer](https://wulgaren.github.io/Araminta/) (Chrome or Edge, USB). Try it first in the [browser](https://wulgaren.github.io/Araminta/webapp/try/).

Same project format as 2.2 (FUN15 / `FUNF`); generation controls ride unused high bits in the FM6 pack so older readers ignore them. Splash and ABOUT show `v2.3`.

### Sequencer

- SEQ GENERATION on DRUM-X: second SEQ section builds editable kick, snare and closed-hat steps from Mutable Instruments Grids. SELECT flips between LEN / KICK / SNARE / HAT and MAP / MAP X / MAP Y / CHAOS. Density edits one lane; map, chaos and length regenerate all three. SAVE held undoes the gesture. Controls save with the project.

### Slicer and transport

- STUT records every live step, including silence, so rests replace the previous take.
- SEND keeps shared MODE / PAT / RATE and clock, but records and MIX stay per-track so a quiet track cannot overwrite or replay another track's take.
- Host coverage for REC / PLAY LED and click behaviour across track arming and clock changes.

### Build

- Native Windows builds via Git Bash (toolchain under `build/deps/`); macOS and Linux paths unchanged. See BUILDING.md.

## 2.2

Leaner FM6 and USB audio timing, a thinner scope when the CPU is busy, and navigation that matches how people actually hop around the panel. Install from the [web installer](https://wulgaren.github.io/Araminta/) (Chrome or Edge, USB). Try it first in the [browser](https://wulgaren.github.io/Araminta/webapp/try/).

Same project format as 2.1 (FUN15 / `FUNF`). Splash and ABOUT show `v2.2`.

### Performance

- Scope draws at 1 px when CPU load is high (≥ 60 %), 2 px otherwise.
- USB audio fill band tightened (~1 ms more headroom; LO/HI 124–172, prime 148; 48 kHz scaled).
- FM6: carrier table; skip flat pitch EG / idle AMS / unused pitch LFO math; operator kernels inlined (same sound).

### UI and transport

- Re-entering a button family from elsewhere always opens its first section/page (in-page cursors kept).
- GLO cycles Master → Clock → Song.
- Arp snaps to note 1 with the sequencer on PLAY, MIDI Start, and each song-section enter; free-run while stopped is unchanged.

## 2.1

Effects you can chain or send, slicer on that same list, SCALE LOCK and MAP, playhead follow in the sequence, ARP drone, and a pile of fixes on top of Araminta v2. Install from the [web installer](https://wulgaren.github.io/Araminta/) (Chrome or Edge, USB). Try it first in the [browser](https://wulgaren.github.io/Araminta/webapp/try/).

Projects from Felucca 1.5 / 1.5.1 and from Araminta v2 still open. New saves use FUN15 (`FUNF`) with the SEND bus and SCALE LOCK layout. Disk motion max on new saves is 72 (RAM still 128). A project saved on 2.1 will not open cleanly on stock Felucca 1.5, and older Araminta builds that only know FUN11 will show those slots empty until you save over them there.

### Effects

- One EFFECTS list per track: DIST, FILTER, CRUSH, PHASR, FLANG, CHOR, DLY, REV, and SLICER. CHOOSE picks a stage, ORDER moves it, USE AS is CHAIN or SEND, MIX is dry/wet or the send amount. Factory order puts CRUSH before FILTER.
- Dual reverb tanks keep separate CHAIN and SEND TYPE / SIZE / DAMP while REV is chained. SHIMR and CLOUD stay available.
- Shared FILTER send bus across tracks; SEND knobs packed into reclaimed FUN13 bytes.
- Insert FX knobs and MIX are mod-matrix and param-lock destinations, including delay, reverb and slicer. One shared destination list for MOD and locks.
- DELAY and SLICER pages hide when that stage is USED as a send (the send amount lives on the list).
- CHAIN delay and reverb keep ringing after short notes.

### Sequencer and performance

- SEQ playhead follow on PATTERN / STEP / DETAIL: the edit cursor tracks the playhead while you play; moving or editing pauses follow for about 3 seconds. PATTERN folds into the STEP section.
- SCALE LOCK on CHORD shares ROOT / SCL / MAP across tracks that opt in.
- SCL QNT renamed MAP (OFF / ALL / KBD / SEQ). Chromatic (CHR) scale removed; use MAP OFF for chromatic play. Older projects map the old CHR index onto MAJ.
- ARP gains DRONE mode.
- Free take: held-note slides get TIE gaps so the chain does not drop; mono fall-back returns after held-note taps; SF_SLIDE when a note starts while another is held.
- CLEAR is a sticky key map under TRACK / ALL / SONG / BOOT, with STEP before PATTERN.
- Turning LEN to 0 clears a piano-roll note and advances the cursor; PRESETS moves notes. TRN is a playback-only offset.
- Knob acceleration is always on (menu toggle removed).

### Sound and UI

- DRUM KIT 78 (CR78-style), synthesized and as SAMPLE set material.
- CLIP / PNCH / DUCK on GLO > MASTER; JIANT MIDI punch-ins on their own FX section.
- SAMPLE, GRAIN, NOISE and SLICE engines retired (USR drum-grid samples and DRUM-X cover the drum cases).
- Flat FX technical graphs; full stage names on the EFFECTS list and pages; live graphs redraw every UI frame.
- Pitch-black GREY palette; splash and ABOUT show `v2.1`; web footers and README branded Araminta (Wulgaren).

### Fixes

- COMB filter on ANALOG and insert FILTER.
- Insert FILTER CUT / RES (cutoff had stuck near 30 Hz).
- FX LATCH honours the MENU preference again.
- MIDI CC93 maps to insert CHOR MIX, not FXPOS.
- FX lock IDs no longer clobber track edits; mod cost budget restored.
- Loud startup click: amp stays muted until DMA settles; FX buffers clear at boot.
- EFFECTS mix bars width and ORDER reorder; Punch-in CLR unlatches; CHAIN slicer gating on the SPREAD side.
