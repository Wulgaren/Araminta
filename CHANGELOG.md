# Changelog

## 3.4

The LFO pages' waveform comes alive: it draws from the running LFO and scrolls at RATE (or SYNC), and S&H's
SMTH is drawn as a smooth glide from hard steps at 0% to a rounded random wave at 100%. CLOUD's reverb tail
now ends in silence instead of leaving a sub-audio drone, and SIZE 127 is an explicit FREEZE that holds the
tail. No project format change: saves stay FUN19 (`FUNJ`). Splash and ABOUT show `v3.4`.

### LFO

- **Live LFO graphs.** The LFO page's wave draws from the part's live phase and scrolls left at RATE (LFO 2: at
  SYNC's note value) instead of sitting at PHS. SIN / TRI / SAW / SQR show two cycles; S&H scrolls its steps.
- **SMTH is drawn.** S&H's SMTH (LFO 2's KNOB 4) blends the graph from hard steps at 0% to a smooth,
  spline-like random wave at 100%, with rounded turns. The graph keeps the last three step values (a few bytes)
  so the older cycle in its two-cycle view, and a wrap, stay continuous.

### FX

- **CLOUD's tail ends.** The room → granular return was floored and had no DC block; ROOM's comb gain at
  1–5 Hz and the grain bank's overlap gain pushed that loop to unity, so an impulse of any level grew into the
  same ~15k RMS offset that played for ever after the music stopped. The loop now rounds towards zero and
  high-passes the room return, and the tail reaches exact silence at every SIZE below 127.
- **CLOUD FREEZE at SIZE 127.** The top SIZE holds the tail instead of treating it as a heavier decay: the
  line recirculates with no new input. The graph shows `FREEZE` over the last normal grain pattern.

## 3.3

Safer audio maths, the LEDs dark during a flash erase, MAP ALL sharing one scale across every track, and
the AUTO / MELO chord keys, with a strummed chord recorded as one step. No project format change: saves stay
FUN19 (`FUNJ`); an old ALL loads as CURR and an old CURR / SCALE LOCK loads as ALL. Splash and ABOUT show
`v3.3`.

### Storage

- **LEDs dark during a flash erase.** The erase stops the LED scan for ~17 ms, which left one column lit;
  the rows are switched off first (#236). Ported from Felucca 1.6.

### Audio

- **Safer audio maths (#265).** Left shifts of negative audio values (undefined in C) are multiplies,
  wrapping multiplies run in unsigned, and signed phase additions wrap in unsigned; the output is bit
  for bit the same (golden hashes unchanged, binary identical). From Felucca (`f9a98fc`, Mario Faúndez
  Vidal) and Felucca-Engines (`e88bc70`, `7511c0a`). New strict-UBSan tests for the DC blocker, low-cut,
  FM6 phase and PHYS.

### SCL

- **MAP ALL is the shared scale.** ALL now sets MAP, ROOT and SCALE on every track while it is on, quantises
  every track's keys and sequence, and puts each track's own MAP / ROOT / SCALE back when you leave ALL. CURR is the per-track
  mode: it quantises the current track's keys and sequence and shares nothing. KBD and SEQ keep their meanings.
  Old projects keep their behaviour: an old ALL loads as CURR, and an old CURR / SCALE LOCK loads as ALL.

### Chord

- **AUTO chord keys.** CHRD gains AUTO, before MELO. The key picks a 3- or 4-note chord in the selected
  scale. It follows the other tracks' recent notes (their last two unharmonized onsets, arp-held keys and
  sequenced notes) and moves smoothly from the previous chord. A two-beat pause, STOP, or a scale / root /
  map / voicing change starts a fresh phrase. AUTO keeps 20 bytes of history per track, packed so the pool
  gate stays green. Ported from Melodee (`f4fdbc7`, Kerem Kilic, GPL-3.0-only).
- **A strummed chord records as one chord.** With REC armed, the strings of one strum now go into
  the first string's step instead of one string per step, so the pattern plays the chord back.
- **MELO chord keys.** CHRD gains MELO, after POW, the former AUTO2 name. The key you press is the melody and always the top note;
  the chord below it uses only notes of the selected scale, and VOIC moves the lower voices only. A DOWN
  strum sounds the melody first. MONO / LEGATO / UNISON play the melody alone; kits still ignore CHRD.
  No project format change: older firmware clamps MELO to POW. Ported from Melodee (`f4fdbc7`, Kerem Kilic,
  GPL-3.0-only).

## 3.2

The stock V15 supply rails before core 1, Felucca's DRUMS engine, a per-step PITCH lock, beat-synced chord
STRUM / STROKE, the new REPEAT 1/12, /2 and *2 TEMPO and GLITCH punch-ins, the DELAY page's TIME / RATE
direction swapped, TAPE as the tenth EFFECTS bus, and MASTER on every EFFECTS
row. New saves are FUN19 (`FUNJ`); FUN18 and older load with TEMPO ON. FUN17 and older have no MASTER owners;
FUN16 and older load with TAPE off and the EFFECTS order rebuilt. Splash and ABOUT show `v3.2`.

### Boot

- **Stock V15 supply rails.** Before the guards, core 1 and the audio start, the FM-1 is set to the
  stock V15 rails (VDC14 1.60 V, SYSVDD 1.38 V, VDDIO 3.2 V) with the stock write order, then read
  back. A timeout or a wrong value stops the sequence and keeps one core. MENU > ABOUT shows
  `RAILS 7/15 (3/11)` (after, and what the boot loader left) or `RAILS FAIL 1 CORE`. Ported from
  Melodee (`hal/fm1_power.h`); the P33 driver stays Araminta's. `tests/power_test.c` replays the V15
  transaction trace, every timeout and the read-back faults.

### FX

- **MASTER on every EFFECTS row.** USE AS now goes CHAIN / SEND / MASTER for the six inserts and the common
  DLY / REV / SLCR / TAPE buses. A master stage runs on the whole stereo mix, in the EFFECTS order of the track
  that selected it; that track shows MASTER and the others show USED on the shared buses. Inserts keep their
  per-track CHAIN / SEND beside a private MASTER instance (2x state, in the POOL); the shared buses are
  exclusive, since a second delay line / third reverb tank would not fit. MASTER inserts use the owner track's
  parameters and MIX; shared masters use the owner track's DLY / REV / SLCR MIX. The routing saves in FUN19.
- **TAPE TEMPO (K4).** The TAPE page's fourth knob is TEMPO, OFF / ON, default ON. ON keeps the transport, voices
  and sequencer in time and makes SPEED the rotating head's pitch only; CHAIN and MASTER then mix the head in by MIX
  (a wet amount, like SEND) and at SPEED 100 with no WOW / DROPOUT the head is bypassed. OFF is the source-clock
  varispeed: CHAIN / MASTER move the transport and voices with SPEED. SEND always keeps time, so its TEMPO card is
  dimmed and fixed ON. Saves in FUN19; FUN18 and older load ON, and older FUN19 saves keep their bit (ON now means
  keep-time). The TAPE page draws an OP-1-style transport: two large reels with a ribbon and guides; SPEED turns
  the reels, WOW rocks them and the ribbon, DROPOUT sags it.
- **TAPE SPEED / WOW in LFO MOD and step locks.** Both are MOD destinations (`TSPEED` / `TWOW` after the
  existing DLY / REV targets) and lock targets from the TAPE page (KNOB 1 / 2 on a held SEQ step). As with
  other shared sends, CHAIN allows only the owner track and MASTER allows every track; SEND is claimed by the
  first track that uses it. The matrix's per-part values and step locks take effect on the next audio block.
- **DELAY K4: left TIME, right RATE.** Turning knob 4 left selects free milliseconds, turning it right
  the note divisions; the mode column shows `TIME` or `RATE`. The division last used is still the one
  TIME returns to. This swaps the 3.0–3.1 direction, so it is a change of muscle memory, not of stored
  data (`G_DTIME` is unchanged and old projects load the same).
- **RATE tap through a tempo change.** With TAPE MASTER or TEMPO OFF the RATE tap's target follows the
  tape speed every block. The tap now holds while the target moves, then fades out, moves and fades back
  in (~12 ms), so the feedback loop keeps its pitch instead of splicing (crackle) or gliding (detuned
  grain). Division changes still snap; free-ms TIME still glides.
- **REPEAT 1/12 (PUNCH-IN 1/2, G3).** A third of a beat (the 8T rate), on the same 1/16 start as the other
  REPEATs. It is a buffer effect like 1/8: below ~54 BPM the loop is too short and the cell dims.
- **/2 and *2 TEMPO (PUNCH-IN 2/2, A4 and B4).** /2 is the former 1/2 TEMPO, now beside its double. While held,
  every target track's sequencer runs at half / double speed; let go and it lands where it would have been.
  /2 + *2 cancel. Recorded in the punch-in lane as nibble 11 (older firmware ignores it); /2 is nibble 3.
- **GLITCH (PUNCH-IN 2/2, C5).** The master is cut on the transport's 1/16s. A deterministic hash of each
  slice's place in a 2-bar cycle decides: play live, repeat the last L, reverse the last 1/16, an octave up or
  down, gate, SKIP, or CRUSH; every slice is windowed. Knobs are fixed (DENS 50 / PITCH 0 / SIZE 0 / VARI 1).
  It borrows the SLICER's buffer, like REPEAT / REVERSE / TAPE / FREEZE; below ~82 BPM only gates, crushes and
  live slices play. Ported from JIANT 0.9.9-snd2 / 0.9.18. GLITCH is audio-only and never recorded.
- **The punch-in maps reordered.** PUNCH-IN 1/2: REPEAT 1/8 / 1/12 / 1/16 / 1/32, LPF, HPF, OCT-, OCT+, DEC-,
  DEC+, ST8, ST16, ATK+, ARP, RND, none. PUNCH-IN 2/2: REVERSE, TAPE, FREEZE, PITCH+, PITCH-, FLANGER, PHASER,
  REV THROW, ECHO THROW, /2, *2, GLITCH, then none; G5 is still CLR with FX LATCH. The FX map's cells show
  `GLITCH`, `/2` and `*2`.
- **FILTER COMB knee and DELAY FDBK bars.** Insert FILTER COMB gets a soft knee at half scale, threshold
  32000 after the voices are summed, so a chord is not compressed hard. The DELAY page's FDBK bars now
  follow the real loop gain (`FDBK * 230 / 32768`) instead of clamping above 100.
- **TAPE, the tenth EFFECTS bus.** SPEED (25..400 %), WOW (slow + flutter) and DROPOUT (short pitch
  fluctuations, not level dips). USE AS is CHAIN / SEND / MASTER: MASTER and CHAIN slow the **source** (the
  transport and every voice's pitch); SEND keeps the dry at tempo and adds the head's pitched copy. MIX is
  one global amount, the same on every track and kept when switching route; in MASTER / CHAIN it is the depth
  (0 normal, 127 the full SPEED change), in SEND the send amount. The TAPE page opens from EFFECTS like DELAY /
  REVERB (SPEED WOW DROP); MIX stays on the EFFECTS list.
- **TAPE's clock and head.** SPEED scales `beat_samples()` for MASTER and the owner track's sequencer for
  CHAIN, plus a pitch offset in 1/16 semitones (a 376-entry log table, 192 * log2(pct / 100)) through the
  voice pitch. Notes and the beat stretch together; envelopes and the reverb decay stay in real time, the
  delay divisions and the click follow the transport, and CHAIN leaves the other tracks alone. The SEND head
  is a two-head crossfaded delay line at 44.1 kHz; its rest delay and the SEND dry compensation are 512
  samples (11.6 ms). The wrapping head has weight zero at the wrap, so the old click is gone. At 100 % with
  MIX 0 the list is bit-identical and the golden renders are unchanged.
- **TAPE storage.** The dead global ids `G_DMIX`, `G_MIDI`, `G_INFO`, `G_CLRSEQ`, `G_INITSND` are TAPE SPEED,
  WOW, DROP, the one MIX and the route; `P_TAPEMIX` (the old GLMOD id) is unused. The 8 KB + 4 KB head and
  compensation lines live in the POOL.

### Drums

- **DRUMS: Felucca's drum engine is back.** A separate engine (15) beside DRUM-X, with the original
  synthesized 8-lane kits: STD, 80, 10, 66, 55, 77, DEEP, TIGHT and PERC. Parameters KIT / TUNE / TONE /
  DECY / SNAP / ACC / KICK / DRV, the GM key map, lane mutes and the SEQ drum grid. DRUM-X stays engine 10;
  old projects and presets still play DRUM-X. Flash +14,384 B, RAM +208 B, POOL unchanged. Favorites for
  engine 15 are skipped because the 16-row table is full. Editor protocol engine 15 and the web editor table
  are updated.
- **GENERATION on every drum kit.** The SEQ GENERATION section is no longer DRUM-X-only: DRUMS, the CR78
  SAMPLE set and a USR slot tagged Drumkit show it too. It writes the same KICK / SNARE / HAT lanes and
  keeps the same MAP / density / CHAOS controls and SAVE undo.

### Projects

- **FUN19 (`FUNJ`).** Same layout and 3840-byte size as FUN16/17/18. Each track's ten-slot EFFECTS order rides in
  the unused high bits of its FM6 patch bytes (after the six generation controls); each stage's MASTER owner
  (3 bits) goes in the chain header's spare bytes 1..3 and the motion header's spare byte 3; TAPE TEMPO is header
  byte 67 bit 7. FUN18 and older load with TEMPO ON; FUN17 and older have no MASTER owners; FUN16 and older
  rebuild the order from `P_IORD` / `P_CHOR` / the SLCR slot and load with TAPE off. Only FUN19 is written.
  Firmware that only knows FUN16/17/18 refuses a FUN19
  (magic): its slot shows EMPTY until saved over, so keep a backup before the first save. The editor reads the
  order through `PROJECT` / `TRACK_DUMP` as before; no command changed.

### SCL

- **SCALE LOCK is now MAP = CURR.** The CHORD page's third knob is gone; MAP on SCL page 1 is
  **OFF / ALL / CURR / KBD / SEQ** (CURR is stored as 4, shown third). CURR quantises keys and steps exactly
  like ALL and shares ROOT / SCALE across every CURR track: entering pushes them, ROOT / SCALE edits follow,
  leaving restores the track's stash. MAP itself is each track's own, which the old link could not do.
- **Compatibility.** CURR saves as MAP = ALL plus the lock bit, so v3.1 opens a v3.2 project as SCALE LOCK +
  ALL, which sounds the same. A v3.1 project locked with MAP OFF / KBD / SEQ loads with that MAP and unlinked
  (the notes are unchanged; only future linking is lost). New tracks default to MAP OFF and are unlinked.
  The stored layout is unchanged.

### SEQ

- **Free take: a strummed chord keeps its sweep.** The strings are re-spaced to the STRUM division (1/8,
  1/16, ...) at the take's closing tempo instead of the tempo the project had before the take. A chord
  strummed at 1/8 while playing a slower loop therefore still sounds as 1/8, and its strings no longer
  receive the legato SF_SLIDE or the mono return (a strum is a chord, not a tap). A **1/16 strum** needs
  two steps, so a 1- or 2-bar take that recorded one closes at **1/32** (LEN 32 / 64) instead of 1/16;
  a 4-bar take has only 64 steps and stays 1/16. `tests/freetake_test.c` covers a 1/8 strum played at
  80 BPM with the project at 120, and a 1/16 strum on 1- and 2-bar takes.
- **NOTE EDITS on SEQ TOOLS.** Stored-note edits, one undo each like COOK/RANDOM. On a melodic track the six edits
  follow the sequence tools with no gap: **OCT− / OCT+** on E4 / F4 transpose the written notes ±12 (ties, rests and
  the notes past LEN stay); **HALF** on G4 stretches the pattern over twice the steps (LEN ×2, ties keep holding);
  **\*2** on A4 squeezes each pair into one (LEN ÷2, the first playing step wins); **HUMAN** on B4 writes fresh
  velocity ±16 and nudge ±2 into every note step and switches QUANTIZE off so the nudge plays; **DUP** on C5 copies
  steps 1..LEN after themselves (LEN ×2, up to 64; locks and automation copy). On a DRUM track BEAT / MUTE and the
  lane tools keep E4..C5, so HALF / \*2 / HUMAN / DUP are D5..G5. OCT− on the page puts back every edit of the visit,
  including QUANTIZE.

### Chord

- **STRUM and STROKE (SCL > CHORD K3 / K4).** A chord key or a MIDI-in chord sweeps from its lowest to its highest
  note over the chosen division (OFF, 1/8, 1/16, 1/32, 1/64, 1/128, 16T, 32T); DOWN / UP / ALT set the direction, ALT
  flips per chord and track. Rate and direction freeze at the press, the timing follows a tempo change, and a
  release cancels the strings that have not played. Off with ARP on, MONO / LEGATO (their chord is the root), and a
  one-note chord. The knobs share the inert ALLOC parameter (packed); legacy ROT/REUSE values 0/1 remain OFF,
  and new sweeps use values 2..22. The stored layout is unchanged. The editor shows the packed list as SWEEP.
  Strum settings saved with earlier unreleased 3.2 builds need resetting after this encoding correction.

### Locks

- **Per-step PITCH lock (SCL K4 on a held step).** A new lock-only motion id, 127: an absolute chromatic
  transpose (-24..+24) that replaces the track's TRN on that step, one value for the whole step (a chord uses its
  first stored note, lowest for a keyed chord, when TRN records it). It counts toward the 88 on-disk records and
  the save-limit warning.
  **Downgrade:** firmware before 3.2 refuses a project whose motion holds id 127 (EMPTY SLOT), so save a backup
  before opening a v3.2 project with pitch locks on an older build.
- **Recording TRN writes that lock.** With REC armed on the selected track while playing, turning SCL TRN stores
  the scale-fitted semitone result as the current step's PITCH lock (C4 +1 in C major stays C4), never a P_TRANS
  automation event; the TRN knob still moves (other steps without a lock follow it), and this step uses the
  fitted value. Editing that PITCH row changes its semitone value and it stays a lock (C4 +1 then plays C#4;
  off-scale is allowed). With REC off, TRN is the ordinary global transpose.
- **Kits ignore PITCH.** On a DRUM or other key-mapped engine, SCL K4 and `MOTION` op 5 refuse to create a PITCH
  lock (rc 1), and a stored kit PITCH lock is inert: note events and lane hits play as stored, as TRN already
  does. Existing projects still load; the lock can be deleted.

### Editor

- **Motion capacity is the device's, not 64.** The web editor reads the real motion pool from INFO `41 01` (192
  since FUN16, 128 in 1.2) instead of rejecting anything but 64, uses `MOTION` op 8 for every record and its lock
  kind, and still works with 64-record firmware. A PITCH lock (id 127) now edits and deletes as a lock instead of
  being sent back as automation.
- **`DUMP` and `SLOWN` agree with the rest of the protocol.** `DUMP` sends `P_IORD` as its 0..719 insert order,
  like `GET` / `SET`, `TRACK_DUMP` and `CHANGED`; the SEND flags stay on the device. `G_SLOWN`'s descriptor is
  0..15 (legacy 1..4 still accepted), so an editor cannot clamp a mask to 4.

### Memory

- **Smaller CPU0 stacks, 13,824 B more sample pool.** The main-loop stack is 12,288 B (was 24,320) and the
  interrupt stack 6,144 B (was 7,936), the sizes Melodee 1.5 audited on the same SDK. Araminta's static
  worst call paths are 5,652 B and 1,868 B (2.2x / 3.3x margin); CPU1 stays at 4,096 B. MENU > ABOUT is
  unchanged; the console's `status` now prints `c0_stack_used user/super` so a device run can confirm the
  high-water marks. `docs/RAM_STACK_AUDIT_3.2.md` has the evidence and the workload checklist.

### LFO

- **S&H SMOOTH.** LFO > LFO 2's fourth knob (`SMTH`, 0..100 %) rounds the WAVE S&H steps into a smooth,
  sine-like random curve. It shows only while WAVE is S&H; 0 is the hard steps, bit for bit. The matrix's
  S&H / SLEW sources are unchanged. It uses the long-inert PRIO parameter's id, so the stored layout is unchanged.

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
