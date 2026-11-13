# Changelog

## Unreleased

### DRUM-X: JIANT's kits, Felucca's kits, DRUMS retired

- **JIANT V10.7.24's DRUM-X voice** replaces the older port: half-rate rendering, the V10.6q quality pass (cleaner
  noise, an 808 clap, a CMOS cowbell), the guiro filter GR, per-sound LAYER. **FM** now strikes with the hit and decays
  with the pitch envelope. DRV and the knee now run on the kit's sum.
- **17 DRUM-X presets.** The first is renamed **FM DRUMS** (same kit, same sound). JIANT's ten kits: 909 CLUB,
  808 BOOM, LOFI DUST, INDUSTRIAL, ACOUSTIC, DUB ROOTS, TECHNO RAW, BREAKS, HIP HOP, LATIN PERC. Per-sound sends left
  out (JIANT ships them at 0), and so is JIANT's own DIST on five kits.
- **Each sound has a MODEL** (EDIT > Kit > SOUND; LAYER on TONE): CLASSIC, or Felucca's DRUMS voices SHELL, METAL,
  TEETH, CLICK. Felucca's kits STD, 80, 10, 66, 55 and 77 are DRUM-X presets **STD, 80, 10, 66, 55 and 77**, translated
  from its numbers.
- **Kit pages read in percent**, as the rest of the device reads its 0..127 values. TONE's third card is **CHAR**: what
  it offers changes with the MODEL (a drive, a drive and its short tail, a click ratio), so DRV was a misnomer. PTCH
  reads as its note and the cents above it, and the FM's second oscillator (OSC2) as its amount, 0 to 100 %. The
  layer itself has 16 positions.
- **DRUMS (engine 15) is retired.** Projects and user presets on it load as DRUM-X with their KIT's kit (DEEP,
  TIGHT and PERC assembled from the others). Close to the old sound, not identical.
- **X-MOD playback removed** (no page since 3.x).
- **Kit labels.** EDIT's first section reads **KIT 1/2** and **KIT 2/2**: both pages edit the whole kit now, so the
  old DRUMS **HIT** name is gone. TONE puts **FILT** first, then COLR / CUT / MIX. CHAR names the combined drive modes
  **DR+BU** and **DR+SH**.
- **SEQ pages.** **PATTERN** is gone: its LEN / DIV / SWING / GATE knobs are on SEQ TOOLS and its layer. The step
  group walks **STEP → DETAIL → AUTOMATION**, and **SEQ TOOLS → GENERATION → GENERATION 2** is the tools section
  (generation only on a drum kit). The tools map opens on SEQ TOOLS alone, so the generation pages keep their own
  knobs; its footer keeps the normal page layout, with **OCT- UNDO** where the sound name goes and the page count at
  the right. SEQ TOOLS' drum-lane mute reads **MUT BD**, not **M BD**.

#### Compatibility

- Projects keep their size and layout. Each sound's LAYER and MODEL ride in the kit bytes' spare top bits, and the
  filter GR in the mode byte: 3.10 and older refuse a project saved with any of them.
- A DRUM-X project made before this plays the new voice. Its FM knob now strikes with the hit and decays with the pitch
  envelope, so a project with FM up sounds different.

## 3.10

Projects grow to FUN20. The flash map takes more of the gap for autosave and banks. POLY gets chord glide.
Factory sounds and FM6 patches land in the same build as DIST MODE OTT, with USB serial off so the image fits.

Splash and ABOUT show `v3.10`.

### Compatibility

Projects are FUN20 (`FUNK`, 4096 B, one flash copy). FUN7..FUN19 still load. Autosave is a full 4096 B A/B pair
(A at `0xE5000..0xE6FFF`, B at `0x93000..0x94FFF`). A pre-3.10 one-sector B at `0xE6000` migrates on boot.
Settings keep their layout; row 13 stays reserved. Older 3840 B backups still restore.

Before installing over stock, take a **full flash dump** with `tools/fm1_flash_dump.py`. An editor SysEx backup
does not keep factory radio calibration in the gap. The installer asks for that confirm when the device is not
already running Araminta. Stock V15 erases the gap on every boot.

### FUN20 and punch-in

The 256 B append holds motion through record 155, plus a 40 B audio punch-in lane. AUTOMATION SAVE LIMIT fires
on the 156th record (RAM still holds 192). A torn project save clears that slot; Save writes the current song
into the autosave first so recovery is current. Punch-in borrow of `sl_buf` also drops MASTER STUT takes, so a
master recording cannot read the punch loop.

### Flash layout

User presets are 96 slots (six banks × 16). Banks 0/1 stay at `0xDC000`. Banks 2..4 are single-copy at
`0xF5000` / `0xF6000` / `0xF7000`. Bank 5 uses `0x95000..0x96FFF`. Backup ids 10..13 cover banks 2..5.

FM6 has four 32-voice homes: `0xE7000`, `0xF8000`, `0xF9000`, and bank 3 at `0xF4000`. SLOT is 0..143
(F1..F15, OWN, B1..B128). INFO advertises 128 bank voices as two 7-bit bytes. User-preset FM6 patches reclaim
on delete and refuse a 33rd entry instead of saving a hollow preset.

### PIT GLD

`EDIT > VOICE` gains **PIT GLD** (POLY only). Chord notes pair with the next chord and glide, in the sequence or
live from CHRD keys and MIDI. A released live chord keeps sounding for one 1/8 so the next can glide into it.
A plucky envelope that has died attacks again at the new chord. With PIT GLD on, GLD reads as a share of the
chord. Off by default. The switch is bit 5 of `P_ALLOC` (no format bump). Editor SWEEP SET keeps that bit.
PITCH lives on **VOICE 2** in every mode; UNISON keeps UNI DTN on VOICE. POLY SPRD automation records SPRD, not
UNI DTN.

### Factory sounds and FM6

Seventeen new or reworked factory sounds join nine kept from the first pass (SYNC LEAD, SUB DRONE, TALK LEAD,
RING NOISE, SYNC STACK, PERC STOP, GLASS BOW, and reworks of STRETCH, REG BELL, WT FDBK; NOISE LD leaves LOFI).
FM6 adds F9..F15 (METAL VOX, HOLLOW FM, CRYSTAL, FM GROWL, OCT STACK, AIR NOISE, DUBSTEP BASS) and presets for
them. DUBSTEP BASS lists under BASS.

### OTT and size

DIST MODE OTT ships in the release image. USB serial (`FELUCCA_CDC`) defaults off so audio + MIDI fit; set
`FELUCCA_CDC=1` for a developer build with MENU > USB SERIAL.

### BOOT RESTORE

MENU > SYSTEM > **BOOT RESTORE** (was RESTORE LAST). The autosave still writes on the idle rules when OFF; OFF
only skips power-on restore.

### Installer

Return to V15 and the install copy name Araminta's reclaim sectors (`0xF4000..0xF9FFF` and the gap user bank).
Byte-exact stock needs the full flash dump above. UBOOT write-back of those sectors on return to V15 is not in
this build.

### Other

EFFECTS > TAPE starts as MASTER at MIX 100 % (idle head at SPEED 100). SAVE > CLEAR's ALL keys also reset the
mix and the arpeggiator. CC 71..74 turn the on-screen knobs; 75..77 are SELECT, ALGORITHM and PRESETS. MIDI
LEARN is gone; its settings bytes stay reserved so older settings load.

## 3.9

A free take can record the arpeggiator. With the ARP on, the take holds the notes the arp plays, not the keys
held. A latched ARP starts the take on its next note, the take keeps the tempo it played on, and closing it
stops the arp so the sequence plays alone. The VOICE page puts PITCH last, so POLY, MONO and LEGATO show
VCE GLD SPRD PITCH and the bend is K4.

Splash and ABOUT show `v3.9`.

### Compatibility

No project, preset or setting change. Saves stay FUN19 (`FUNJ`) and 3.8 files load as they are.

### ARP free take

With the ARP on, REC while stopped on an empty project records the notes the arp plays. A key starts a take,
and a latched chord starts it on the ARP's next note, so HOLD works. The take keeps the tempo the ARP played
on and ends at the nearest 1, 2 or 4 bars. Notes after the loop are dropped instead of wrapping onto step 1.
Closing the take clears the latch and stops the ARP, so playback is only the recorded sequence.

### Voice

PITCH is now the last card on VOICE. POLY, MONO and LEGATO show VCE GLD SPRD PITCH, so the bend is K4. UNISON
keeps its four cards and PITCH on VOICE 2. The card behaves as in 3.8.

## 3.8

The VOICE page gains a pitch bend. UNISON gets it too, on a second page. CLOCK moves next to MASTER. Newer FM-1s
with the UCUN flash can save and update again. The boot splash no longer pops, and there is a Control Surface for
Live.

Splash and ABOUT show `v3.8`.

### Compatibility

Projects and settings are unchanged. Saves stay FUN19 and 3.7 files load as they are.

### Voice pitch

In POLY, MONO and LEGATO, the empty card on the VOICE page is now PITCH. Turn it to bend the track. By default it
reaches 2 semitones each way, the same range as MIDI pitch bend.

The bend stays where you leave it, and changing track recentres it. It is live, so it is not saved with the
sound. An external pitch wheel moves the card too.

In UNISON the VOICE page keeps its four cards. Press SELECT to open VOICE 2 for PITCH, and SELECT again to go
back.

### Controls

CLOCK is now MASTER's second page. GLO opens MASTER, SELECT walks to CLOCK, and the next GLO press opens SONG.

LFO TRIG turns the other way. Left of NOTE is FREE, as DELAY's K4 steps left to TIME.

Dead engine cards no longer appear in MOD destinations, AUTOMATION rows or step locks.

SAVE > CLEAR keeps the keys silent while its confirmation is open.

CLEAR ALL4 puts MAP back to OFF with the rest of the track.

### Flash chip

Newer FM-1s with the UCUN UC25HQ80 flash now save settings, projects and samples, and update from the running
firmware. Both flash parts work. A unit running an older build on the UCUN chip needs one flash with M-Flasher
or m-bootloader before the usual installer works.

### Boot

The amp is muted before the splash draws. The audio starts while the panel is still dark, so the start click
lands in the boot window and the rose no longer pops.

### Ableton

`ableton/FM-1/` is a Control Surface script for Live 12. KNOB 1 to 4 of each FM-1 track controls one page of
four parameters on the selected device. ALGORITHM chooses the page. Install steps and the ROUT CH1-4
requirement are in `ableton/README.md`.

## 3.7

KNOB 1-4 send a relative MIDI CC on the USB output while a computer is connected, one CC per detent, without
changing what the knob does on the FM-1. The bank is the selected track: CC 16-19 on track 1, 20-23 on track 2,
24-27 on track 3 and 28-31 on track 4, value 64 + detents (Bin Offset), on the track's MIDI channel, the same one
its keys use. ALGORITHM selects the track, so it selects the bank. A fast turn carries the panel's knob
acceleration (x2..x16), so a flick crosses a DAW parameter as quickly as the FM-1's own. PRESETS is pitch bend
and SELECT the mod wheel (CC 1) on the same channel, beside their local jobs (browsing sounds, stepping pages);
a track change puts the old channel's bend back to centre and its mod to 0. Nothing is sent before the host
enumerates the device. The package is assembled from three pinned SDK files in `build/deps/ac79` instead of a
full AC79 checkout; the firmware image and the package are unchanged by that. Splash and ABOUT show `v3.7`.

### Compatibility

No project or settings change: saves stay FUN19 (`FUNJ`), and projects, presets and settings from 3.6 load as they
are. On the device nothing changes except the extra CCs on the USB MIDI output; with no host attached the output
is as before. A DAW that records MIDI from the FM-1 (Track enabled) will record those CCs into a clip.

### MIDI

- **Knobs out.** KNOB 1-4 send CC 16..31 as above. The knob still edits its page, layer or menu value: the CC is
  sent beside the local action, so navigating pages does not change which CC a knob sends.
- **Acceleration.** The CC follows the panel's curve (ui_input.c accel): ACC_RATE / ms per detent, doubled above
  17, up to x16, with the first detents of a turn and a reversal at one step. Its timing is separate, so sending
  CC never changes how the page knob accelerates.
- **Pitch and mod.** PRESETS sends pitch bend (14-bit, centre 8192, 1/64 of the range a detent) and SELECT sends
  the mod wheel (CC 1, 0..127). Both persist and both keep their local jobs. A track change sends the old
  channel's bend back to centre and its mod to 0.
- **Relative only.** The value is 64 + detents, clamped in 63-step chunks. In Live, map the knob and set the
  mapping to Bin Offset if the automatic detection guesses another relative mode; Live's manual suggests turning
  the knob slowly left while assigning.
- **No collision.** 16..31 are not used by anything else the firmware sends; notes and clock are untouched.

## 3.6

FM6's E5 macro is VIB now, the patch LFO's pitch depth, instead of VMOD. Old projects and user presets move
their VMOD value into the modulator operators' velocity sensitivity, so their sound and velocity response stay
the same; their E5 automation is dropped. The DIGITAL engine's DSP and its conversion are gone, and project
formats FUN1..FUN6 (0.x .. 1.0 development) are no longer read. MRAT moves the modulators' ratios on a sounding
note, so a matrix slot on it works. ANALOG's INT greys out on SUPER; SEQ's DETAIL / AUTOMATION open the EDIT
page and AUTOMATION's OCT- deletes a row; the matrix gains VEL and its CHRD / VOIC slots reach the chord keys.
No project format change: saves stay FUN19 (`FUNJ`); header bit 2 marks the VIB meaning. The boot splash's rose
is drawn in code now, the same mark without its 2 KB bitmap. Splash and ABOUT show `v3.6`.

### Compatibility

- **DIGITAL sounds no longer convert.** Engine 1 keeps its reserved index as a silent stub; a project or user
  preset whose sound was DIGITAL does not play (projects load the track silent, a DIGITAL user preset is refused).
  Convert such files in the web editor before uploading if their sound matters.
- **FUN1..FUN6 projects are refused.** Projects of Felucca 0.x and the 1.0 development builds (before FUN7) show
  as EMPTY, as a format this firmware does not know. Re-save them from an older build or the web editor.
  FUN7..FUN19 are unchanged, and new saves stay FUN19 (`FUNJ`).
- **The web editor converts on upload.** DIGITAL sounds (library files) and old project files are converted
  client-side before they are sent: FUN1..FUN6 to FUN19, and DIGITAL tracks to FM6. No firmware code needed.

### FM6

- **VIB.** EDIT 2 KNOB 2 changes the patch LFO's pitch depth, not the track LFO. It is also an LFO / matrix
  destination and a lockable parameter under EDIT.
- **Old VMOD.** A project or user preset from before 3.6 has VMOD in E5. On load, the value is added to the
  modulator operators' velocity sensitivity in the stored FM6 patch and E5 is cleared. VMOD automation has no
  patch target, so those E5 events go.
- **MRAT in the matrix.** MRAT (E4) moves the modulators' ratios on a sounding note now; before, it was baked
  into the operators at note-on, so a MOD slot pointed at it did nothing. No sound change with the macro at
  rest (the same coarse offsets, applied per block).
- **CHRD / VOIC in the matrix.** The chord keys read the matrix value as a key starts, so a slot on CHRD (chord
  type) or VOIC (voicing) changes what the key plays; before, the block's value was never read.
- **Every destination tested.** `mod_test` renders one child per destination with a slot on it, in a rig where
  each can reach the output, and fails if the sound is unchanged: the bug class behind MRAT. `master_test` does
  the same for GLO > MASTER's CLIP / PNCH / DUCK / DREL.

### MASTER

- **CLIP.** The knob now lowers the clipper's ceiling (32000 down to 6000) instead of driving a fixed one, so an
  ordinary mix audibly clips instead of passing almost unchanged; the makeup restores the ceiling's level and adds
  up to +1.5 dB. CLIP 0 stays bit-identical (bypass).
- **PNCH on every kit.** The punch curve (x1.46 attack, x1.70 body into the knee, the tail cut) now applies to
  the kick of DRUMS and SAMPLE kits (CR78, USR drumkits) as well as DRUM-X, which keeps punching every lane. It
  is still per hit, by the hit's own age.
- **DUCK on every kit.** A kick from any kit's track (DRUM-X all lanes, DRUMS and SAMPLE kits' kick) triggers the
  sidechain; a kit track is never ducked by its own kick.

### DIGITAL

- **Engine and conversion removed.** `eng_digital.c`, the `FELUCCA_FM4` build flag and its UI / test variants are
  gone, and `fm4_convert.c` follows them: engine index 1 is a silent stub. Old DIGITAL projects load with that
  track silent; DIGITAL user presets are refused (see Compatibility).

### UI

- **Splash mark in code.** The boot rose and its dotted polar grid are drawn from the sine table now; the 2 KB
  bitmap, ten never-drawn keycaps and the hidden DIGITAL OP pages are gone.

### ANALOG

- **OSC B INT on SUPER.** SUPER has no second oscillator, so INT does nothing there; its card is drawn DIM and
  keeps its value, as WAVE and MORPH are under SYNC / SUB / SUPER.

### SEQ

- **EDIT no longer resets or deletes.** On DETAIL and AUTOMATION the EDIT button opens the EDIT page, as on the
  other pages: a step's CHANCE / RATCH / NUDGE / VEL and an automation record stay. OCT- deletes the selected row
  there (SAVE held undoes it), as does the web editor's `MOTION` delete.

### MOD

- **VEL destination.** A matrix slot can scale a note's velocity at note-on, a gain like AMP: LFO -> VEL thins
  the notes out at the bottom of the cycle, VEL -> VEL -64 closes a full-velocity note. The sources are taken as
  they are at note-on, and the VEL source reads the scaled value after. Appended as DST 51.

## 3.5

ANALOG gets OSC A / OSC B pages: OSC B's own wave on every filter TYPE, PWM and its MORPH, DRIFT, and the
filter TYPE on page 3. A strummed chord records into one step in free take too; ENV DEST's fourth card reads
SYNC and TAPE's WOW reads FLUTR; SAVE > CLEAR clears FX and HELP is retired. CLOUD's SIZE 127 is a true
freeze that holds the tail instead of droning. No project format change: saves stay FUN19 (`FUNJ`). Splash
and ABOUT show `v3.5`.

### ANALOG

- **OSC A / OSC B pages.** OSC A: WAVE, MORPH (the wave slides toward the next plain one; PWM widens toward
  54 %), DRIFT (each voice's slow pitch / filter wander), NOISE. OSC B: its own WAVE (SAME by default, following
  OSC A), INT (interval from OSC A, -24..+24 semitones), DTN, MIX; it works through LP, BP, HP and COMB, and is
  greyed only in SYNC / SUB / SUPER, which define OSC B themselves. EDIT > FILTER is page 3 now,
  its KNOB 4 DRIVE; the KTR knob is gone (keytracking fixed at its old default).
- **Compatibility.** E8 is INT now. Older projects and user presets load it as 0, with MORPH / DRIFT 0, and drop
  their E8 / OP automation, with OSC B's wave on OSC A's; project header byte 67 bit 1 and the user preset
  category byte's bit 7 mark the new meaning, so no format version changed.

### Recording

- **Free take + STRUM.** A strummed chord records into the first string's step, as step recording does, so
  the take plays the chord together instead of one string per step. A fine strum no longer forces a 1/32 grid.

### FX

- **CLOUD FREEZE holds the tail instead of droning.** At SIZE 127 the line stops taking input and its buffer
  is held; the last grain swarm keeps reading it with fixed windows, so no new grains are born and the texture
  no longer mutates. The room return is DC-blocked on the freeze path too, so the hold stays centred instead
  of settling into the old sub-audio drone. The last second stays level-stable at high DAMP as well.

### Labels

- **ENV SYNC and TAPE.** ENV DEST's fourth card is `SYNC` (was `ESYNC`) and draws LFO 2's tempo icon; TAPE's
  `WOW` card is `FLUTR`. Library files keep the `ESYNC` key, so files written before 3.5 still load.

### UI

- **SAVE > CLEAR clears FX.** B3 clears the selected track's inserts, order, sends and slicer; F4 clears
  every track's plus MASTER, the SEND knobs and the global FX. The ALL / ALL4 actions clear FX too and
  move to the black keys F#3 / G#3; the white cells there are FX / FXS now.
- **MENU > SYSTEM > HELP is gone.** The hint row under the keys and its HELP setting are retired; the
  editor's id 24 stays reserved. SYSTEM keeps USB SERIAL, RESTORE LAST, HARDWARE CALIBRATION, INFO and
  ABOUT.

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
- DIST MODE **OTT**: the Ableton Multiband Dynamics preset of that name, a three-band LR4 split (88.3 Hz / 2.5 kHz) with 4:1 upward compression below its threshold and 66.7:1 downward above it, in the same insert slot. DRIVE is DEPTH, TONE is TIME (1 ms / 50 ms attack and release at the centre), and MIX stays the dry/wet fader; DEPTH 0 is the transparent crossover sum. The stored MODE is appended, so projects and presets keep their existing values and older firmware reads OTT as FUZZ.
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
