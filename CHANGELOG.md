# Changelog

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
