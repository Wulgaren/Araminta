# Changelog

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
