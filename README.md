# Araminta

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

![Araminta](docs/araminta.png?v=2)

**TL;DR:** Araminta is an opinionated fork of [Felucca](https://github.com/hugelton/Felucca) for the M-VAVE FM-1.
Felucca 1.5.1 underneath; on top, JIANT's DRUM-X and punch-ins, Sloop free take, MIXER solo, Felucca's
0.9 flat look, and an EFFECTS list you chain or send.

Plug the FM-1 into a computer by USB, open the [web installer](https://wulgaren.github.io/Araminta/) in Chrome or Edge, and press Install.
Install at your own risk: M-VAVE's updater or the installer's **Return to official V15** takes you back.

Coming from Felucca **1.5 / 1.5.1**? Read [Coming from 1.5](#coming-from-felucca-15--151): your projects load; once
you save (or quiet autosave runs) on Araminta, Felucca 1.5 cannot open those slots.
Want a look without hardware? [Try it in your browser](https://wulgaren.github.io/Araminta/webapp/try/).

Multi-engine synthesizer firmware for the M-VAVE FM-1, based on Felucca.
Report what you find in [Araminta Issues](https://github.com/Wulgaren/Araminta/issues).

- Install: [web installer](https://wulgaren.github.io/Araminta/) (Chrome or Edge, USB), or `tools/fm1_install.py` from a terminal
- Try: [Araminta in your browser](https://wulgaren.github.io/Araminta/webapp/try/): the same firmware compiled to
  WebAssembly, with the panel on screen (mouse, touch, computer keyboard, Web MIDI in)
- Editor: [web editor](https://wulgaren.github.io/Araminta/webapp/editor/)
- Upstream: [hugelton/Felucca](https://github.com/hugelton/Felucca) (and its [itch.io](https://hugelton.itch.io/felucca) page)
- Build: [BUILDING.md](BUILDING.md)

## What Araminta changes

Araminta keeps Felucca 1.5.x as the base. The day-to-day differences:

- **Sloop free take.** Project empty, track armed, transport stopped: the first note starts a free take. No pattern required first.
  Hold a note and press others while it is still down: those steps get slide flags (acid legato) when the take closes,
  and releasing a tap while the hold continues records a return to the held pitch. A strummed chord keeps its STRUM
  sweep at the take's tempo and is recorded as a chord, without the legato slide. A 1/16 strum on a 1- or 2-bar take
  closes at 1/32 so its strings have two steps to land on; a 4-bar take stays at 1/16.
- **JIANT MIDI punch-ins.** On FX under Punch-in (FX taps: EFFECTS → SLICER → PUNCH-IN). Two fixed PUNCH-IN pages, 1/2 and 2/2 (SELECT walks the section; no reassignment): 1/2 holds REPEAT 1/8 **1/12** 1/16 1/32, LPF, HPF, OCT±, DEC±, stutters, ATK+, ARP, RANDOM; 2/2 holds REVERSE, TAPE STOP, FREEZE, PITCH UP / DN, FLANGER, PHASER, REV THROW, ECHO THROW, **/2** (A4), **\*2** (B4) and **GLITCH** (C5). Holding FX alone does not open punch-ins. **GLO > MASTER** has CLIP / PNCH / DUCK. Reverb adds SHIMR / CLOUD.
- **Melodee.** Silent tracks still run LFO, bend, the engine block and fades, so modulation keeps moving when a track is muted or resting.
- **Stock V15 supplies.** Before core 1 and the audio start, the FM-1 is set to the stock V15 rails (VDC14 1.60 V, SYSVDD 1.38 V, VDDIO 3.2 V) and read back. A failed setup keeps one core. MENU > ABOUT shows `RAILS 7/15 (3/11)` or `RAILS FAIL 1 CORE`.
- **DRUM-X.** One morphable 8-lane kit instead of Felucca's sampled / VA kit enum (EDIT Sound > Kit > Lanes; Voice stays hidden on DRUM-X). Kit pages are SOUND / SOUND 2 only (no X-MOD / SOUND 3; morph motion via the mod matrix). Kit and mutes save with the project (FUN19 / `FUNJ`; same 3840-byte size as FUN10; disk motion max 88). FUN10 loads the factory kit. Old projects that stored a KIT value still play as DRUM-X.
- **DRUMS.** Felucca's original synthesized drum engine, ported as a separate engine (15) beside DRUM-X. Nine kits: STD, 80, 10, 66, 55, 77, DEEP, TIGHT and PERC. Its own KIT / TUNE / TONE / DECY / SNAP / ACC / KICK / DRV. These are the exact 1.6.1 kits. DRUM-X stays on engine 10.
- **SLICE retired.** 
- Samples tab: one card per USR slot, each set to **Sample**, **Drumkit** or **Wavetable**. Drumkit maps every root to its own lane note and tags the slot (`FSMP` header), so SEQ uses the drum grid; Wavetable takes one Serum file or a folder medley. The name field is gone: names were editor-only, the FM-1 shows USR1..3.
- **VOICE** is one page: VCE, GLD, UNI DTN, SPRD. GLD is free milliseconds (no GLMOD on the page). PAN / PLAY live on MIXER. UNI DTN shows only in UNISON; SPRD only in POLY / UNISON. Blank cards otherwise; values stay stored.
- **EFFECTS.** One list for the six inserts and the shared buses: CHOOSE a stage, ORDER moves it, USE AS is CHAIN, SEND or MASTER, MIX is dry/wet (or send amount). Delay, reverb and slicer sit in that list (bus chorus is gone); **TAPE** is the tenth row: SPEED, WOW, DROPOUT and TEMPO. With TEMPO ON (the default) the transport and voices keep time and SPEED is the head's pitch only, with MIX as the wet amount on every route; OFF makes CHAIN and MASTER slow the source (transport and voice pitch). SEND always adds a pitched copy and fixes TEMPO ON (K4 is dimmed there). One global MIX is the depth in CHAIN/MASTER and the send amount in SEND. MASTER runs on the whole mix in the EFFECTS order of the track that selected it; the common buses show USED on the other tracks. Inserts keep their per-track CHAIN / SEND beside a MASTER copy. Dual reverb tanks keep SEND and CHAIN settings while REV is chained.
- Reverb modes from JIANT - SHIMR and CLOUD.
- Delay has divisions or smooth ms; K4 turned left is TIME, turned right RATE.
- Assign FX values in LFO MOD or SEQ param locks. TAPE SPEED / WOW are shared destinations: in CHAIN only the owner track can move them, in MASTER any track, in SEND the first track to use one reserves it.
- **MIXER PLAY.** KNOB 4 is ON, OFF or **SOLO** (saved with the track).
- **Felucca 0.9 design.** Flat cards, Terminus text, Fukiai icons. STYLE and ANIM are gone: values snap, no rolling digits, no LED sweep or idle animation.
- **TRN while playing.** On SCL, TRN offsets the sequence as it plays. Steps on disk stay put; kits ignore TRN and PITCH. With REC armed while playing on a synth track, a TRN turn also writes the current step's PITCH lock (scale-fitted); the knob still moves (other steps without a lock follow it), and no P_TRANS automation event is recorded.
- **MAP (was QNT).** SCL knob: OFF / ALL / **CURR** / KBD / SEQ. KBD walks the scale on white keys (blacks = closest in-scale note); ALL is KBD plus sequence follows the scale; CURR is ALL's quantising plus ROOT / SCALE shared by every CURR track (the CHORD page's old SCALE LOCK, now a MAP value); SEQ is chromatic keys plus sequence follows. Chromatic (CHR) scale removed; use MAP OFF. CURR saves as ALL + the lock bit, so v3.1 opens it as SCALE LOCK + ALL; a v3.1 project locked with MAP OFF / KBD / SEQ loads unlinked. Current saves are FUN19 (`FUNJ`; FUN18 `FUNI` loads).
- **Following playhead** in the SEQ. Start selecting a step to pause the following, returns after 3 seconds of inactivity.
- **SEQ GENERATION.** On a drum kit (DRUM-X, DRUMS, CR78 or a USR Drumkit), the second SEQ section generates editable kick, snare and closed-hat steps from Mutable Instruments Grids. SELECT switches between LEN / KICK / SNARE / HAT and MAP / MAP X / MAP Y / CHAOS. MAP selects one of 25 templates; X/Y blend between them and show CUSTOM. Turning MAP, MAP X or MAP Y briefly shows the full-screen map, then returns to the drum grid; CHAOS keeps the grid visible. Density edits change only their lane; map, chaos and length edits regenerate all three. Other lanes stay as written, SAVE held undoes the gesture, and each track's controls save with the project. Tips follow MENU > HELP.

Everything below that is Felucca 1.5.1 behaviour unless a note says otherwise.

### New in 3.2

- **TAPE.** The EFFECTS list gains a tenth row: SPEED, WOW, DROPOUT and TEMPO, with CHAIN / SEND / MASTER routing.
  TEMPO ON (the default) keeps the transport, voices and sequencer in time, so SPEED is a pitch-only control and
  CHAIN / MASTER mix the head in by MIX like SEND. TEMPO OFF makes CHAIN and MASTER slow the source clock, so notes
  and beat stretch together. SEND always adds a pitched copy and shows TEMPO fixed ON. The TAPE page draws an
  OP-1-style transport: two large reels with a ribbon and guides. The reels turn at the SPEED rate; WOW rocks
  them and the ribbon, DROPOUT sags the ribbon.
- **DRUMS.** Felucca's synthesized drum engine returns as engine 15, with the STD, 80, 10, 66, 55, 77, DEEP,
  TIGHT and PERC kits. DRUM-X stays engine 10; existing sounds and projects keep playing it.
- **Filter and delay readout.** Insert FILTER COMB gets a soft knee, and the DELAY page's FDBK bars follow
  the real loop gain.
- **PITCH lock.** A held step on SCL K4 stores an absolute chromatic PITCH lock; with REC armed, a TRN turn
  records the scale-fitted value. Kit engines ignore PITCH (and TRN).
- **FUN18 (`FUNI`).** Saves carry the ten-slot EFFECTS order in the FM6 patch bytes and each stage's MASTER
  owner in spare chain / motion header bytes. FUN17 and older still load with no MASTER owners and the order
  rebuilt; older firmware refuses a FUN18 slot (EMPTY until saved over).
- **FUN19 (`FUNJ`).** New saves add TAPE TEMPO (header byte 67 bit 7) on top of FUN18. FUN18 and older load with
  TEMPO ON, which now means the transport keeps time. Older saves keep their stored bit, so a TEMPO ON project from
  before this change now plays with the keep-time meaning. Older firmware refuses a FUN19 slot (EMPTY until saved over).

### New in 3.1

- **CR78 factory kit.** SAMPLE gains a public-domain CR-78 kit at **SET 8** (after USR1..USR3, whose indices stay
  5..7). Its eight one-shots use the SEQ drum lanes `36 38 39 42 46 45 37 56`; the drum grid appears on that set.
- **Samples tab.** One card per USR slot with **Sample / Drumkit / Wavetable**, choose files, upload and erase.
  Drumkit maps each root to its lane note; Wavetable offers a table file or a folder medley. The name input is
  gone (names were editor/backup-only; the FM-1 shows USR1..3).
- **WAVETABLE.** OSC B's knob is **LEVEL** (MOD / AUTOMATION call it **B LEVEL**); the graph now draws B's own wave
  on the OSC B page and A, labelled **OSC A**, while RATIO is hot there.

## Features

- **Fourteen engines** (below), each with its own factory presets; every factory sound has a category (BASS, LEAD,
  PAD, PLUCK, KEYS, DRUM, FX, OTHER) to browse by
- **Four tracks**, one synth part each with its own engine and sound (drums are the DRUM-X or DRUMS engine);
  8 voices shared between them. ALGORITHM selects the track on every page
- **Sequencer:** 64 steps per track with chords, ties, accent, slide, per-step chance, velocity and ratchets
  (a step played 2, 3 or 4 times in its length); **SEQ** sections are the step group (SELECT: PATTERN / STEP /
  DETAIL), AUTOMATION, then SEQ TOOLS; a piano roll of the steps, where KNOB 3 sets a note's length (0 clears it and advances the cursor)
  and PRESETS moves that note in time (or, while step-recording with a key held, sets its LEN); a drum grid (white keys =
  steps, black keys = lanes); **SEQ > DETAIL**: the cursor step's chance, ratchet, nudge and velocity on the four
  knobs, drawn as lanes over the steps; while playing on PATTERN / STEP / DETAIL the edit cursor follows the playhead
  (the playhead line only shows when you have moved the cursor away); twiddling STEP or editing the sequence pauses
  follow for a few seconds, then it resumes; automation: recording of knob moves per step (an automation icon marks the cards it
  drives); live loop recording with overdub; Sloop free take when the project is empty; step recording at the cursor (REC on SEQ > STEP while stopped), one
  chord at a time or note by note (MENU > CHORD ENTRY ADD); divisions listed by length, 4 bars to 1/32; loading a
  sound never touches your patterns
- **Timing:** live recording keeps when you played each note, as a per-step **NUDGE** (1/16 of a step, half a step
  early to 7/16 late); the track's **QUANTIZE** decides what plays: ON (the default) every step on the grid, as
  always, OFF your own timing. Nudges can also be set by hand
- **Parameter locks and the AUTOMATION list:** hold a step on SEQ > STEP (or the drum grid) and turn a knob to set
  that parameter for that step only; EDIT with the step held clears its locks. **SEQ > AUTOMATION** is one list of
  everything that happens on the track's steps: PLAY / CLEAR, QUANTIZE, each step's CHANCE, RATCH, NUDGE and VEL, its
  locks and automation events; change their step, parameter or value, turn a lock into automation or back, add or
  delete one. 192 records shared by the four tracks (88 saved in a project). SAVE held undoes every edit. A step can
  also hold a **PITCH** lock (SCL K4): an absolute per-step transpose (-24..+24), chromatic and not snapped; the
  step uses it instead of the track's TRN. Kits ignore PITCH: SCL K4 on a DRUM track creates no lock, and
  `MOTION` op 5 refuses one.
  With REC armed while playing on a synth track, turning **TRN** writes that step's PITCH lock from the scale-fitted
  result (C4 +1 in C major records 0 ST); the TRN knob still moves (other steps without a lock follow it), and no
  P_TRANS automation event is recorded. Editing that PITCH row changes its semitone value and it stays a lock, so +1 ST
  plays C#4 (off-scale is allowed); it is lock-only and never becomes ordinary automation.
- **SEQ TOOLS:** hold SEQ on any page for tools on the keys: CLEAR, REVERSE, SHIFT < / >, **RANDOM** (a new beat,
  or new notes in the scale on the same rhythm) and **COOK** (changes the pattern a little each press); on a DRUM-X
  track also BEAT, **MUTE** (F4 toggles the selected lane) and each lane's CLEAR, REVERSE, FILL and RANDOM;
  KNOB 1-4 are LEN, DIV, SWING and GATE. Each tool is one undo (SAVE held); OCT- puts the track back as the layer found it
- **NOTE EDITS on SEQ TOOLS:** on a melodic track the six edits follow the sequence tools with no gap: **OCT- /
  OCT+** on E4 / F4 transpose the stored notes ±12; **HALF** on G4 stretches the pattern over twice the steps;
  **\*2** on A4 squeezes pairs into one (LEN changes with them); **HUMAN** on B4 writes fresh velocity ±16 and
  nudge ±2 into every note step (and switches QUANTIZE off so the nudge plays); **DUP** on C5 copies the pattern
  after itself (`A -> A A`, LEN doubles, max 64). On a DRUM-X track BEAT / MUTE and the lane tools keep E4..C5, so
  HALF / \*2 / HUMAN / DUP are D5..G5. One undo each (SAVE held); OCT- puts back the whole visit
- **Metronome and count-in:** CLICK (OFF, while recording, or always) with three levels, and a count-in
  of 1 or 2 bars before recording from stop; the click goes to the headphones and speaker only, never to
  USB audio or into a pattern
- **Songs** (GLO > SONG): up to 16 sections, each naming the project slot (A-D) **every track** plays, or "-" for a
  track that rests there, with its repeats; drawn as a timeline with a lane per track. GLO also holds MASTER and CLOCK
  (HOME is only Home ↔ Mixer). **MIXER** KNOB 4 is PLAY: ON, OFF or SOLO
- **Chord keys:** one finger plays an in-key chord (triads or sevenths of the scale, or fixed chord
  shapes), with voicings and a beat-synced **STRUM** (DOWN / UP / ALT); on the keys, MIDI in, recording and the
  arpeggiator
- **Arpeggiator** with UP, DN, UPDN, RND, ORD (the notes in the order you pressed them), REPEAT and, for chords,
  DNUP, UP+8, CONV, DIVG, PINKY, THUMB, WALK and CHORD; a beat LED; 15 scales with MAP (OFF/ALL/CURR/KBD/SEQ), glide,
  MONO / LEGATO / UNISON
- **LFO:** RATE or **SYNC** to a note value (4 bars to 1/32, following the tempo or an external clock), **TRIG** NOTE
  or FREE, **POL** bipolar or unipolar (LFO > LFO 2), and **SMTH** on LFO 2 (only when WAVE is S&H): rounds the
  sample-and-hold steps into a smooth, sine-like random curve
- **Modulation matrix:** 4 slots per track, MIDI controllers as sources, **S&H** and **SLEW** (a random value per
  LFO cycle, held or gliding), and **DEPTH** as a destination, so the matrix can modulate the LFO itself
- **SPREAD** (EDIT > VOICE, SPRD): the voices of a POLY or UNISON track alternate left and right of its PAN
- **ENV SYNC** (ENV > ENV DEST): the envelope's attack, decay and release as note values of the tempo
- **Effects list** chained or sendable per track (DIST, FILTER, CRUSH, PHASR, FLANG, CHOR, DLY, REV stages):
  CHAIN or SEND with a MIX fader; delay in time with the tempo (divisions or smooth ms, dotted values included);
  reverb as **HALL**, ROOM, SPRING, plus Araminta's **SHIMR** / **CLOUD**; technical graphs on the FX pages;
  insert knobs and MIX are mod-matrix destinations; master limiter, with CLIP / PNCH / DUCK on the master
- **FX layer:** hold FX for repeat, reverse, filter sweeps, tape stop, freeze, a harmonizer
  (OCT UP / OCT DN with shimmer), **FLANGER** and **PHASER** (in time with the tempo), **REV THROW** and **ECHO THROW**
  (every track's reverb or delay send full while held), JIANT punch-ins on their own FX section, and mutes on the black keys;
  the two PUNCH-IN pages have fixed keys (SELECT walks the section); MENU > FX LATCH makes them toggle,
  so nothing has to stay held
- **MOMENTARY:** hold LFO and turn a knob on HOME or a sound / FX page; let go of LFO and the value jumps back
  (OCT+ while holding keeps it)
- **Quick layers:** hold FX, GLO, SCL, EDIT, REC or SEQ for shortcuts on the keys and knobs, or double-tap it to
  keep the layer open; one-step undo (SAVE held); REC on every page; OCT+ confirms, OCT- goes back; what can be
  pressed breathes softly instead of blinking
- **Presets:** factory presets, 32 user preset slots and 4 projects, named on the device (an FM6
  sound keeps its own patch in both); user presets get a category, and a project can get a random name; projects
  from every earlier version load. The PRESETS knob changes the sound from every page that is not a list.
  Turn the FM-1 off and on and the music is as you left it (MENU > RESTORE LAST, an autosave of its own,
  written only when stopped and idle)
- **Screen:** Felucca 0.9's flat look (Terminus + Fukiai), 9 palettes including pitch-black grayscale,
  high contrast and NIGHT; type sized by role; MENU > LARGE for tall knob cards; the menu in five tabs. The header
  shows the track and the **project slot of its pattern** ("1 A", "1 A*" once edited), play state and tempo. HOME
  shows the oscilloscope or, with MENU > HOME TRACKS, **the four tracks** with their steps (LEVELS: track levels on
  the knobs); MENU > SCREEN OFF can darken the panel when left alone; MENU > HELP shows a short hint when a page
  or layer opens
- **LEDs:** idle buttons and keys glow dim so the panel can be found in the dark (MENU > LEDS: OFF,
  DIM LO, DIM HI or INV, the official firmware's look); PLAY turns green while playing; the keys show
  the notes the sequencer and MIDI IN play, and on SEQ > STEP (stopped) the notes of the step under the cursor;
  the drum grid marks the beats (steps 1, 5, 9, 13); MENU > SCALE LEDS shows the track's scale on the keys
- **USB:** class-compliant MIDI in and out, and a stereo audio input (named "FM-1" on the wire; loader "FM-1 Update") at 44.1 or 48 kHz
  (the computer picks) that records the master output on the computer, no driver needed (at a fixed level with MENU > USB LEVEL FIXED;
  on macOS 13-15, set MENU > USB SERIAL to OFF so the audio input appears)
- **MIDI:** USB and TRS MIDI in; MENU > MIDI > MIDI IN: CH1-4 (channels 1-4 play tracks 1-4), CH5-8, CH9-12 or
  CH13-16 (another block of four, e.g. for two FM-1s on one cable), or SEL (every channel plays the selected track);
  the keys send on the track's channel; pitch bend, sustain, panic; clock from internal, USB or TRS. MIDI CCs set
  track parameters: 5 GLIDE, 7 LEVEL, 10 PAN, 71 resonance, 72 / 73 / 75 release / attack / decay, 74 brightness,
  91 / 93 / 94 reverb send, chorus mix (insert CHOR), delay send. **MIDI LEARN** (GLO + D4) sets up to 16 more CCs of your
  controller to any knob's parameter of any track, on the device; **Program Change** loads a track's sound by its
  number in PRESETS
- **Web:** editor for every parameter (with a 6-operator FM patch editor that also sends a whole 32-voice bank),
  step grid, mixer, preset library, sample/drumkit/wavetable slots with recording and trim, the MENU settings;
  full backup and restore
  (everything but the FM6 voice bank: keep its .syx file);
  return to the official firmware; Araminta itself running in the browser

## Coming from Felucca 1.5 / 1.5.1

**Yes: you can install Araminta over Felucca 1.5 / 1.5.1 without wiping the FM-1.**
Projects, user presets, settings, samples and the FM6 voice bank stay in flash; the installer only replaces the app
(and its update loader).

What loads:

- **Projects (FUN10 / `FUNA`).** Every 1.5 / 1.5.1 project opens. Patterns, motion, songs, FM6 patches and globals
  stay. Disk motion max on new saves shrinks from 110 to 72 records (SCALE LOCK + SEND bus layout; RAM still 128).
- **1.5 INSERT.** TYPE / A / B / C / MIX migrate into the six-stage chain (DIST drive / CRUSH / PHASR / FLANG / CHOR
  and MIX). DIST TONE comes back as an offset on the drive-linked cutoff. TYPE OFF stays dry.
- **ANALOG filter TYPE and ENV SYNC.** Kept at their values.
- **User presets and MENU settings.** Load; new chain / kit fields take defaults where the old store had none.
- **DRUM-X.** A FUN10 has no kit bytes: the factory kit. An old KIT enum value still maps into MRPH.

What changes when you save (or when quiet autosave writes after idle):

- **New project format is FUN19 (`FUNJ`).** Same 3840-byte size and layout as FUN16/17/18; the ten-slot EFFECTS
  order (six inserts + DLY + REV + SLCR + TAPE) rides in the FM6 patch bytes' unused high bits, each stage's
  MASTER owner (3 bits) in the chain header's spare bytes 1..3 and the motion header's spare byte 3, and TAPE
  TEMPO in header byte 67 bit 7. The song chain still packs two bytes a section, for 88 motion records (FUN15 and
  older: 72), on top of the DRUM-X kit, SCALE LOCK, SEND bus knobs, SLICER list slots and exact FX lock packing.
  FUN18 and older load with TEMPO ON. FUN17 and older have no MASTER owners; FUN16 and older rebuild the order
  from P_IORD / P_CHOR / the SLCR slot and use the other TAPE defaults. Only FUN19 is written.
  **Any firmware before this build refuses FUN19:**
  that slot shows EMPTY there until you save over it (flash data stays until then). A backup that holds FUN19 is
  refused there too. Upgrade is one-way for anything you re-save on this build.
- **Motion on disk max is 88** (FUN10: 110; FUN15: 72; RAM 192). Extra locks stay in RAM until truncated on pack;
  the UI can warn when you cross the save limit.
- **P_COUNT is 127** (1.5: 111). Editors that read `INFO` keep working; a 1.5-only editor will not know the new ids.

Day-to-day differences after the flash (not data loss):

- FX pages: Effects list with mix faders, reorderable chain, SEND vs CHAIN for DLY / REV (dual tanks when REV is chained).
- SEQ / GLO / SAVE layout shifts (PATTERN, DETAIL, MASTER, CLOCK under GLO); see Controls.
- Upstream 1.5.1 extras still apply: SEQ > DETAIL, MIDI Program Change, dotted delay TIME, REV / ECHO THROW.
- Full backup still omits the FM6 voice bank: keep the `.syx` if you use B1–B32.

Rollback: installer's **Return to official V15**, or M-VAVE's updater. That is stock FM-1, not Felucca 1.5.
It will not reopen FUN19 projects. Back up in the web editor **before** the first save or quiet autosave on Araminta
if you still need a Felucca 1.5-readable copy.

## Controls

- Tap a page button for its page, again for the next; HOME returns home. Page buttons act when let go
- Hold FX, GLO, SCL, EDIT, REC or SEQ for its quick layer; hold SAVE to undo, HOME for the menu; hold LFO and turn a
  knob for a MOMENTARY change
- Double-tap a layer's button to keep its layer open without holding; tap it again to close
- On SEQ > STEP, hold a step and turn KNOB 1-4 to lock those parameters on that step (PRESETS: move the held steps in time; while step-recording with a key held, PRESETS sets that note's LEN)
- On MIXER, KNOB 4 is **PLAY**: ON (default), OFF, or SOLO (saved with the track)
- On action pages and in dialogs, OCT+ does it and OCT− goes back; in the menu, OCT+ / OCT− change the value and HOME closes it
- Save a sound: stop, tap SAVE, pick a slot with KNOB 1, then OCT+ and OCT+ again (name it with the keys)

The pages by button:

| Button | Taps go round (sections; SELECT walks pages inside one) | Held |
|---|---|---|
| HOME | Home ↔ Mixer | the menu |
| SEQ | Step group (SELECT: PATTERN / STEP / DETAIL), AUTOMATION, SEQ TOOLS | SEQ TOOLS, on every page |
| GLO | MASTER, CLOCK, SONG | the GLO layer (mutes, solos, levels, TAP tempo, D4: MIDI LEARN) |
| SAVE | Presets (SELECT: PRESETS / USER / PHRASES), Project (SELECT: PROJECT / TOOLS) | UNDO |
| LFO | LFO (SELECT: LFO / LFO 2 / LFO DEST), then MOD | MOMENTARY (with a knob) |
| EDIT | Sound (EDIT 1 / 2; FM6 adds OPERATOR, OP ENV; ANALOG FILTER / FILTER DRIVE), Kit, Lanes, Voice (hidden on DRUM-X) | the EDIT layer (the engines; on DRUM-X the black keys mute lanes) |
| FX | EFFECTS (SELECT: list + stage pages), SLICER, PUNCH | the FX layer |
| SCL, ENV, ARP | their pages | SCL: its layer |
| REC | arms the track | — |

## Menu

Hold **HOME** for the menu, in five tabs: **DISPLAY** (COLOR, LARGE, LEDS, SCREEN OFF, SCOPE, HOME),
**CONTROL** (HOLD, FX LATCH, BPM LOCK, SCALE LEDS, STEP PREVIEW, CHORD ENTRY), **AUDIO** (SPEAKER EQ,
USB LEVEL, CLICK, CLICK LEVEL, COUNT-IN, TUNE), **MIDI** (MIDI IN, LEARN CLEAR) and **SYSTEM** (USB SERIAL,
RESTORE LAST, HELP, HARDWARE CALIBRATION, INFO, ABOUT).
SELECT moves between the tabs and ALGORITHM through the rows of one (PRESETS is idle there; ALGORITHM also scrolls
ABOUT); any of KNOB 1-4, or OCT+ / OCT−, changes the value (OCT+ opens HARDWARE CALIBRATION, INFO and ABOUT); press
HOME to close the menu. Every new
setting defaults to the earlier behaviour. The web editor's Settings tab reads and changes them too, saved the same
way as from the menu, in the same tabs. STYLE and ANIM are gone (0.9 flat look; see What Araminta changes).
KNOB ACCEL is always on for wide values.

- **COLOR:** the palette. GREY (pitch-black grayscale, called MONO up to 1.0.1), GREEN, AMBER, ICE, VIOLET,
  ROSE, PAPER (light), HI-CON (high contrast) and NIGHT (the 0.9 look: true black, green-tinted text).
  Every palette draws without a raised card fill (SURF matches the background)
- **LARGE:** OFF or ON: on HOME and the value pages the four knob cards grow tall, marked K1-K4, with
  larger labels and values about twice the size; the graph below becomes a strip. Lists, the piano roll,
  the drum grid and the quick layers keep their layout with larger labels
- **LEDS:** OFF (no glow), DIM LO, DIM HI (default) or INV (the idle LEDs lit, the active ones dark).
  What can be pressed breathes up to about 60 % of a lit LED, about 30 % with DIM LO
- **SCREEN OFF:** NEVER (default), 5 MIN, 15 MIN, 30 MIN or 60 MIN: after that long with no button, key or knob
  touched, the screen goes dark; the sound, the sequencer, MIDI and USB go on. The backlight stays on (on the FM-1
  its line also enables the buttons and keys). The next button, key or knob only turns the screen back on
- **SCOPE:** OUT (default) or MIX: what HOME's oscilloscope shows. OUT the sound after MASTER (the picture shrinks
  with the volume), MIX the mix before MASTER, the same size at any volume
- **HOME:** SCOPE (default), TRACKS or LEVELS: under HOME's four cards, the oscilloscope or the four tracks, a row
  each with its sound, its steps playing, MUTE and a meter. With TRACKS only the look changes; with LEVELS KNOB 1-4
  set the LEVEL of T1-T4 (the engine's main parameters are on EDIT)
- **HOLD:** how long a layer's button is held before its map shows
- **FX LATCH:** ON, FX + an effect key turns the effect on until pressed again, the knob macros stay
  where you leave them, and FX + OCT− turns everything off
- **BPM LOCK:** ON, SELECT no longer changes the tempo; hold GLO and turn SELECT, tap F4 in the GLO
  layer or use HOME > CLOCK instead
- **SCALE LEDS:** OFF or ON: the keys show the selected track's scale all the time (the root a little
  brighter); not on DRUM-X or SLICE tracks
- **STEP PREVIEW:** OFF (default) or ON: on SEQ > STEP with the transport stopped, moving the cursor plays the step
  it lands on once (its notes or hits, through the track's sound)
- **CHORD ENTRY:** HOLD (default) or ADD: how step recording writes. HOLD: the keys held together become the step,
  letting go moves on. ADD: each key adds its note to the step, up to 4 (a key whose note is there takes it out),
  and KNOB 1 moves on, so a chord goes in one finger at a time
- **SPEAKER EQ:** FLAT, LOWCUT or BASS+, a tone setting for the built-in speaker (not a switch). It also
  shapes the headphone out and USB audio, so keep it on FLAT when recording. The firmware cannot turn
  the speaker off, but headphones in the jack do
- **USB LEVEL:** MASTER (the MASTER knob sets the USB audio level too) or FIXED (USB always at full
  level, MASTER sets only the speaker and headphones)
- **CLICK:** OFF (default), REC (while a track is armed) or ON: a click on every beat while playing, the
  bar's first beat higher. **CLICK LEVEL:** LOW, MID (default) or HIGH; MASTER sets it too
- **COUNT-IN:** OFF (default), 1 BAR or 2 BARS: with a track armed, PLAY counts in first (clicking even with
  CLICK OFF), and a note played in the last half beat lands on step 1. Not with an external clock
- **TUNE:** the global tuning, −50 to +50 cents. It is the project's, saved and loaded with it
- **MIDI IN:** CH1-4 (default), SEL, CH5-8, CH9-12 or CH13-16 (see MIDI above). The project's too
- **LEARN CLEAR:** how many CCs MIDI LEARN has set (NONE, 1 CC … 16 CC); OCT+ clears them all
- **USB SERIAL:** ON or OFF, applied when the menu closes (the FM-1 reconnects). OFF leaves out the
  serial console, a developer tool, so the FM-1 is a plain audio + MIDI device; this lets macOS 13-15
  see its USB audio input. MIDI, the editor and the installer work either way
- **RESTORE LAST:** ON (default) or OFF. ON, the FM-1 starts with the music you left (the header says
  RESTORED), kept in an autosave apart from the four projects; it is written when the music changed, the
  transport is stopped and nothing has been touched for 10 s, at most once a minute. OFF starts with the
  power-on sounds
- **HELP:** OFF (default) or ON: a short hint in the footer for about 2 s when a page opens, and under a quick
  layer's map while it shows
- **HARDWARE CALIBRATION**, **INFO** (the version, the USB link and the CPU load) and **ABOUT**

## Engines

In the order the device lists them:

- **ANALOG**: virtual analog; two oscillators (WAVE SYNC: the second hard-synced to the first; SUB: a square an
  octave down), noise, drive, a resonant filter: low-pass, band-pass, high-pass or comb (EDIT > FILTER; COMB:
  CUT sets the delay / tooth spacing, RES the feedback)
- **FM6**: classic 6-operator FM (Dexed-based): 32 algorithms, a full patch per track, edited in the
  web editor (which imports .syx files) or on the device: EDIT > OPERATOR (FREQ is one list: ratio then fixed Hz,
  fine, level; no OPERATOR 2 page) and OP ENV (each stage's rate and level, drawn as the envelope), one operator
  at a time on the algorithm chart; macros on top; SLOT picks a factory patch (F1-F8), the track's own (OWN) or a
  voice of the bank the web editor sent (B1-B32)
- **PHASE**: phase distortion (ported from CrispyZebra)
- **LOFI**: chiptune; pulse, triangle, saw, noise and a 4-bit wave RAM, stepped envelope, sweep, arpeggio
- **SAMPLE**: multisampled instruments (PIANO, FLUTE, SAX) and 3 user sample slots (shared with WAVETABLE); **CR78**
  is a public-domain factory kit at SET 8 with the SEQ drum grid. A USR slot can be a **Drumkit** in the web editor
  (each root mapped to its lane note) or a **Sample**
- **VOICE**: formant oscillator, sung vowels
- **TRIO**: 3 oscillators with ring modulation and sync, multimode filter
- **WHEEL**: tonewheel-style organ; drawbar registrations, percussion, key click, drive, rotary speaker
- **GRAIN**: granular textures from the built-in samples or a user slot
- **PHYS**: physical models: modal resonators, strings, struck membranes, sympathetic strings
- **NOISE**: noise from analog to digital: colours, crackle, shift-register and metallic tones
- **DRUM-X**: an 8-lane morphable kit on the General MIDI key map (JIANT port). EDIT sections
  **Sound** (MRPH / TUNE / TONE / DECY), **Kit** (SOUND: lane A/B plus pitch env; SOUND 2: DCAY / NOIS / COLR / MODE)
  and **Lanes**. Kit and mute persist in FUN19 projects (FUN10 opens with the factory kit). Old Felucca KIT values
  still play as DRUM-X. SLICE is retired; old stores load as DRUM-X
- **DRUMS**: the ported Felucca drum engine (engine 15): KIT STD / 80 / 10 / 66 / 55 / 77 / DEEP / TIGHT / PERC,
  TUNE, TONE, DECY, SNAP, ACC, KICK, DRV. Existing projects and presets stay on DRUM-X; DRUMS is for new sounds
  or for a project that selects it. It runs the 1.6.1 code unchanged in its own engine state.
- **WAVETABLE**: two wavetable oscillators, A and B, each morphing through 16 frames of a mip-mapped table; B
  phase-modulates A (B>A) and can also be heard through LEVEL at an exact RATIO, and A takes phase feedback
  (FDBK). BASIC and SPECTRAL are factory tables; USR1..USR3 each hold one complete 16 x 1024 table, loaded from
  a Serum `.wav` or a folder medley in the web editor. The EDIT graph draws A on OSC A and B's own wave on OSC B,
  and switches to a labelled OSC A while RATIO is the hot knob

The DIGITAL engine of 0.9 has been replaced by FM6: projects and presets with DIGITAL sounds load
as FM6 sounds converted from them. The SAMPLE engine's PERC kit was removed in 1.0.2: sounds and
projects that used it load as the DRUM-X engine's kit, on the same key map. FM6's patch bank (the B
slots) was removed in 1.0.3: user presets keep their own FM6 patch, and presets that used a B slot get
that patch on the first start of 1.0.3. 1.4.1 brings B slots back as a voice bank sent from the web editor
(B1-B32): a voice picked from it becomes the track's own patch, so nothing saved depends on the bank. SLICE once had
BREAK and (from 1.0.4) PIANO; both left the ROM with the engine. Since 1.0.4 a missing sample (an empty user slot, or a set missing
from the build) plays a plain sine at the note's pitch on SAMPLE and GRAIN, and the screen says
NO SAMPLE once. DRUM-X's KIT variants HAND, CYM and H+CYM were retired in 1.0.5: sounds and projects that
used them play the 66, 10 and 77 kits. SAMPLE's PIANO was a lo-fi piano in 1.4 and 1.4.1 (the full one installed into a user slot from the web editor as
PIANO HD); 1.5 brings the full piano back.

**SLICER** (FX page, every track): a tempo-synced 16-step gate or stutter, with 16 patterns.

**Reverb** (FX > REVERB): KNOB 1–3 are TYPE / SIZE / DAMP for the tank the track owns (SEND or CHAIN).
**HALL** (since 1.4, the default on a fresh start: eight delay lines feeding each other with slowly drifting taps,
stereo, SIZE the decay up to about 4.5 s, DAMP how fast the highs die), **ROOM** (the default up to 1.1.5),
**SPRING**, plus Araminta's **SHIMR** / **CLOUD**. A project plays the TYPE it was saved with.

## Scale keyboard

On the **SCL** page, **MAP** chooses how the keys (and optionally the sequence) sit on the scale:

- **OFF** — normal chromatic piano. Sequence plays as stored.
- **KBD** — white keys walk the scale (C4 = **ROOT**; consecutive white keys = consecutive scale notes). Black keys play the closest in-scale note (often the same as a neighbour). Sequence plays as stored.
- **ALL** — same keys as KBD, and sequenced notes map onto the current ROOT / SCALE as they play (steps unchanged; kits never remapped).
- **SEQ** — live keys as OFF; sequenced notes map onto the scale as they play (same as ALL for the sequence).

**TRN** transposes the resulting notes (and sequence playback; steps stay as stored); the octave buttons shift them by full octaves. Kits ignore TRN and PITCH. With REC armed while playing on a synth track, turning it also records the step's **PITCH** lock (the scale-fitted semitone result); the knob still moves, and no P_TRANS automation event is recorded.

Available scales: major (MAJ), natural minor (MIN), Dorian (DOR), Mixolydian (MIX),
major pentatonic (PEN), minor pentatonic (MPEN), harmonic minor (HARM), Phrygian (PHRY),
Lydian (LYD), Locrian (LOC), ascending melodic minor (MEL), minor blues (BLUES), whole tone
(WHOLE), half-whole diminished (DIMHW), and whole-half diminished (DIMWH). Scales with other
than seven notes continue across the white keys without repeating notes; their roots need not
fall on every C key. Chromatic play is MAP OFF. Drum kits and incoming MIDI keep their own note mapping.

Press **SCL** again for the **CHORD** page: CHRD picks the chord keys (OFF, the scale's triads or
sevenths, a fixed shape, AUTO: the key picks an in-scale chord that follows the other tracks' recent
notes, or MELO: the played key is the melody, always the top note, and the chord below stays in the
scale), VOIC the voicing, STRUM the sweep (OFF, 1/8 to 1/128, 16T, 32T) and STROKE its direction (DOWN,
UP, ALT). MELO leaves the melody fixed in every voicing; a DOWN strum sounds it first. Hold a step on **SEQ > STEP** (piano roll; with SCL as the last sound page) and turn
K4 (TRN) for a per-step **PITCH** lock: an absolute chromatic transpose (-24..+24) that replaces the track's TRN on
that step (not snapped), one value for the whole step (a chord uses its first stored note, lowest for a keyed chord,
when TRN is recorded). Kits ignore PITCH: on a DRUM track SCL K4 creates no lock and `MOTION` op 5 refuses one. It
appears as a **PITCH** row in **SEQ > AUTOMATION** on synth tracks. Turning TRN on SCL without a step held is the
ordinary track transpose; with REC armed while playing it writes the fitted PITCH lock, the knob still moves the
track's TRN, and the step uses the fitted value. No P_TRANS automation event is created.

## Layout

| Path | What |
| --- | --- |
| `firmware/` | firmware sources: `src/` app, `hal/` hardware layer, `loader/` update loader |
| `tools/` | build script, generators, package maker, installer and sample uploader |
| `assets/` | UI font, icon names, CC0 instrument samples and the public-domain CR-78 kit |
| `docs/` | design notes, audits and the parameter / ID contract (`PARAM_CONTRACT.md`) |
| `web/` | the web installer, editor and browser emulator (`web/emu/`) |
| `tests/` | tests that run on the build machine |
| `LICENSES/` | licence texts of the bundled fonts, icons, ported DSP and SDK files |

## If the FM-1 does not start

If an update is interrupted and the FM-1 stays black, check whether a computer sees it as a USB device named
**WL80UBOOT** (or a USB mass-storage device with ID 4C4A:8057). That is the chip's built-in boot mode, and
the FM-1 can be brought back:

- First try another USB data cable, and close every other app that uses MIDI, then run the web installer again.
- If it stays in boot mode, [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter) reads and
  writes the FM-1's flash from a Mac through a Seeed XIAO RP2040 (three wires to the FM-1's USB lines). Back up
  the flash first, then write the official firmware (M-VAVE's FM-1.fwsc).
- Questions: [Issues](https://github.com/Wulgaren/Araminta/issues). Upstream Felucca: [hugelton/Felucca](https://github.com/hugelton/Felucca).

## Support

Bugs that you can reproduce belong in [Araminta Issues](https://github.com/Wulgaren/Araminta/issues), one report each.
Ideas and requests go in [Discussions](https://github.com/Wulgaren/Araminta/discussions) when that board is open; otherwise open an issue and say it is a request. Pull requests are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md).

Araminta is a fork. Upstream Felucca development, sponsorship and itch.io releases live at [hugelton/Felucca](https://github.com/hugelton/Felucca). If Felucca is useful to you, [sponsoring on GitHub](https://github.com/sponsors/hugelton) or a donation on [itch.io](https://hugelton.itch.io/felucca) helps keep that work going.

## AI disclaimer

Araminta is developed with the assistance of AI coding agents for coding, testing, documentation, translation and maintenance.
Design, features, sound choices and direction stay with the maintainer and contributors.
No generative AI is used to create music, icons or visual artwork for this project.
Upstream context: [On AI-Assisted Development and Responsibility](https://github.com/hugelton/Felucca/discussions/166).

## Credits

- **Fork:** [Wulgaren/Araminta](https://github.com/Wulgaren/Araminta) by [Wulgaren](https://github.com/Wulgaren)
- Felucca 1.6: the LED rows switch off before a flash erase (#236, kurogedelic, GPL-3.0)
- AUTO / MELO chord harmony: [Melodee](https://github.com/keremimo/melodee) `f4fdbc7` by Kerem Kilic / Ellic Studio (GPL-3.0-only)
- Signed audio arithmetic (#265): Felucca `f9a98fc` by Mario Faúndez Vidal (GPL-3.0-only); ANALOG, LOFI,
  NOISE, TRIO and WHEEL fixes from [Felucca-Engines](https://github.com/hugelton/Felucca-Engines) by Mario
  Faúndez Vidal and kurogedelic (MIT)
- **[Hügelton Instruments](https://hugelton.com)** (Leo Kuroshita, [@kurogedelic](https://github.com/kurogedelic)):
  Felucca, the base this fork builds on; the PHASE engine's waveforms (a C port of the oscillator of
  [CrispyZebra](https://github.com/hugelton/CrispyZebra), GPL-3.0); the DRUM-X and DRUMS voices and kits; the Hügelton Sample
  Pack (the drum samples, GPL-3.0-only, not CC0); the [Fukiai](https://github.com/hugelton/Fukiai) icon
  font ([MIT](LICENSES/MIT-Fukiai.txt))
- Fonts: [Terminus](https://terminus-font.sourceforge.net/) by Dimitar Toshkov Zhekov, [SIL OFL 1.1](LICENSES/OFL-Terminus.txt)
  (the UI text, as in Felucca 0.9); [Inter Tight](https://github.com/rsms/inter-tight) by The Inter Project Authors,
  [SIL OFL 1.1](LICENSES/OFL-InterTight.txt) (keycap labels and diagrams);
  the browser emulator's labels: [DotGothic16](https://github.com/fontworks-fonts/DotGothic16) by The DotGothic16 Project Authors, [SIL OFL 1.1](LICENSES/OFL-DotGothic16.txt)
- Samples: [Versilian Studios](https://versilian-studios.com/) [VSCO-2 Community Edition](https://github.com/sgossner/VSCO-2-CE) and [VCSL](https://github.com/sgossner/VCSL), CC0 1.0: the SAMPLE sets and PIANO HD; the CR-78 kit from [Oramics "Sampled"](https://oramics.github.io/sampled/DM/CR-78/) (Boxed Ear), public domain ([attribution](assets/samples-cc0/ATTRIBUTION.txt))
- VOICE engine: after [klattsch](https://github.com/tgies/klattsch) by Tony Gies (MIT); formant data from Klatt (1980) and Hillenbrand et al. (1995)
- PHYS engine: models ported from [DaisySP](https://github.com/electro-smith/DaisySP) by Electrosmith and Emilie Gillet ([MIT](LICENSES/MIT-DaisySP.txt)) and from Emilie Gillet's [eurorack](https://github.com/pichenettes/eurorack) code ([MIT](LICENSES/MIT-Rings.txt))
- FM6 engine: msfa from [Dexed](https://github.com/asb2m10/dexed) by Google Inc. and Pascal Gauthier ([Apache-2.0](LICENSES/Apache-2.0-msfa.txt))
- Browser emulator: after [X0X](https://github.com/charlesvestal/fm1-x0x) by [charlesvestal](https://github.com/charlesvestal) (GPL-3.0), a Felucca fork whose browser build showed the way
- Package format and boot files: [JieLi AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) ([Apache-2.0](LICENSES/Apache-2.0.txt); three of its files are in every package, none in this tree)
- Contributions (upstream and here): [keremimo](https://github.com/keremimo) (white-key scales, #2), [ChanceTheMaker](https://github.com/ChanceTheMaker)
  (TRS MIDI, bend, sustain and clock, palettes, favourites, editor display settings: #8, #10, #11, #12),
  [andreahaku](https://github.com/andreahaku) (sample recording and trim, #29; SLICE manual slices and tests, #27, #22),
  [spinkham](https://github.com/spinkham) (the boot fix for 0.9 projects, #111),
  [zednaked](https://github.com/zednaked) (ratchets, #100),
  [jasonpersinger](https://github.com/jasonpersinger) (the ROOM reverb click fix, #121),
  [renebohne](https://github.com/renebohne) (MIDI Program Change, #179)

## Licence

Free software: [GPL-3.0-only](LICENSE), the Hügelton Sample Pack included. The bundled fonts and the
ported DSP keep their own licences ([LICENSES/](LICENSES/)); details in [LICENSING.md](LICENSING.md).

M-VAVE and FM-1 are trademarks of their respective owners. Araminta is not affiliated with or endorsed by them.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
Copyright (C) 2026 Wulgaren for Araminta fork changes
