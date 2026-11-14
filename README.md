# Araminta

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

![Araminta](docs/araminta.png?v=2)

Araminta is synthesizer firmware for the M-VAVE FM-1. It is a fork of [Felucca](https://github.com/hugelton/Felucca)
1.5.1 with JIANT's DRUM-X and punch-ins, free-take recording, an effects list you can chain or send, and Felucca
0.9's flat look.

- **Install:** plug the FM-1 in by USB, open the [web installer](https://wulgaren.github.io/Araminta/) in Chrome or
  Edge, press Install. Or run `tools/fm1_install.py`.
- **Try it without hardware:** [Araminta in your browser](https://wulgaren.github.io/Araminta/webapp/try/), the same
  firmware compiled to WebAssembly. Mouse, touch, computer keyboard and Web MIDI work.
- **Edit sounds and back up:** [web editor](https://wulgaren.github.io/Araminta/webapp/editor/).
- **Preview `dev`:** [dev emulator](https://wulgaren.github.io/Araminta/preview/dev/), built from the `dev` branch.
- **Release notes:** [CHANGELOG.md](CHANGELOG.md). **Build it yourself:** [BUILDING.md](BUILDING.md).
- **Report a bug:** [Issues](https://github.com/Wulgaren/Araminta/issues).

Install at your own risk. The installer's **Return to official V15**, or M-VAVE's own updater, takes you back to
stock.

## Before you install

- **From stock firmware:** take a full flash dump first with `tools/fm1_flash_dump.py`. The installer asks for it.
  Stock V15 keeps radio calibration in a flash area that an editor backup does not cover.
- **From Felucca 1.5 / 1.5.1:** no wipe needed. Projects, presets, settings, samples and the FM6 bank stay. See
  [Coming from Felucca 1.5](#coming-from-felucca-15).
- **From an older Araminta:** install over it. Your data stays.

## New in 4.2

- The **mod matrix** has eight slots per track. KNOB 1 on the MOD page runs SLOT 1..8.
- A project saves **148 automation records** (was 155): slots 5..8 take the rest. A 4.1 project with more still
  loads whole and shows AUTOMATION SAVE LIMIT.
- The DRUM-X **sound dice** (OCT+ held + PRESETS) starts from a random factory kit, and can change a sound's WAVE and
  MODEL.

Full notes: [CHANGELOG.md](CHANGELOG.md).

## What Araminta adds to Felucca

- **Free take.** On an empty project with a track armed and the transport stopped, the first note starts a
  recording. No pattern needed. Held notes become slides, strums record as chords, and the ARP's notes record too.
- **JIANT punch-ins.** Tap FX twice for PUNCH-IN: repeats, filters, stutters, reverse, tape stop, freeze, pitch,
  flanger, phaser, reverb and echo throws, glitch.
- **DRUM-X.** An 8-lane morphable drum kit with 17 presets.
- **Beat generation.** On any drum kit, SEQ > GENERATION writes editable kick, snare and hat steps from Mutable
  Instruments Grids.
- **Effects list.** Six inserts plus delay, reverb, slicer and TAPE in one list. Reorder them and set each to CHAIN,
  SEND or MASTER.
- **MIXER solo.** KNOB 4 on the MIXER is ON, OFF or SOLO, saved with the track.
- **Chord tools.** AUTO and MELO chord keys (from Melodee), PIT GLD chord glide on POLY tracks.
- **Scale MAP.** OFF, ALL, CURR, KBD or SEQ decide whether the keys and the sequence follow the scale.
- **Felucca 0.9 look.** Flat cards, Terminus text, Fukiai icons, no animations.
- **Knobs as MIDI CC** to a DAW, and an Ableton Control Surface script.
- **Stock V15 power rails.** The FM-1 boots on the stock supply voltages. MENU > ABOUT shows the result.

## Features

- **13 engines**, each with factory presets sorted by category (BASS, LEAD, PAD, PLUCK, KEYS, DRUM, FX, OTHER).
- **4 tracks**, each with its own engine and sound. 8 voices shared between them. ALGORITHM picks the track.
- **Sequencer.** 64 steps per track with chords, ties, accent, slide, chance, velocity, nudge and ratchets. Piano
  roll for synths, drum grid for kits (white keys are steps, black keys are lanes). Live recording with overdub,
  step recording, and a playhead the cursor follows.
- **Parameter locks and automation.** Hold a step and turn a knob to lock a value on that step. Knob moves record
  as automation. SEQ > AUTOMATION lists every lock and event to edit or delete. 192 records live, 148 saved per
  project.
- **SEQ TOOLS.** Hold SEQ for CLEAR, REVERSE, SHIFT, RANDOM, COOK, octave shifts, HALF, \*2, HUMAN and DUP. Each tool
  is one undo.
- **Songs.** GLO > SONG chains up to 16 sections of project slots A..D, per track.
- **Arpeggiator** with 14 modes, **15 scales**, chord keys with voicings and beat-synced STRUM.
- **2 LFOs** (free or tempo-synced), an **8-slot mod matrix** per track, envelope times as note values.
- **Effects.** DIST (with OTT), FILTER, CRUSH, PHASER, FLANGER, CHORUS, delay, reverb (HALL, ROOM, SPRING, SHIMR,
  CLOUD), SLICER, TAPE, and a master clipper, kick punch and sidechain duck.
- **Presets.** Factory sounds, 96 user slots, 4 projects. Autosave restores your music at power-on.
- **Undo.** Hold SAVE.
- **USB.** Class-compliant MIDI in and out, and a stereo audio input to the computer at 44.1 or 48 kHz. No driver.
- **MIDI.** USB and TRS in, channel modes for one or several FM-1s, clock in, Program Change, CCs for the on-screen
  knobs.
- **Web editor.** Every parameter, a 6-operator FM patch editor, step grid, mixer, preset library, sample slots
  with recording and trim, settings, full backup and restore.

## Engines

| Engine | What it is |
|---|---|
| ANALOG | Virtual analog. Two oscillators (SYNC, SUB, SUPER modes), noise, drive, LP / BP / HP / COMB filter |
| FM6 | 6-operator FM (Dexed). 32 algorithms, editable on the device or in the web editor, which imports .syx. SLOT picks F1..F15, the track's own patch, or B1..B128 from the banks you sent |
| WAVETABLE | Two morphing wavetable oscillators with phase modulation and feedback. BASIC, SPECTRAL, or your own table in USR1..USR3 |
| PHASE | Phase distortion (from CrispyZebra) |
| LOFI | Chiptune: pulse, triangle, saw, noise, 4-bit wave RAM |
| VOICE | Formant oscillator, sung vowels |
| TRIO | 3 oscillators, ring mod and sync, multimode filter |
| WHEEL | Tonewheel organ with drawbars, percussion, click and rotary |
| PHYS | Physical models: modal, strings, membranes, sympathetic strings |
| NOISE | Noise colours, crackle, shift-register and metallic tones |
| GRAIN | Granular, from built-in samples or a user slot |
| SAMPLE | PIANO, FLUTE, SAX, the public-domain CR-78 kit (SET 8), and 3 user slots as a sample or a drumkit |
| DRUM-X | 8-lane drum kit (JIANT port), 17 kits, a MODEL per sound |

Retired engines (DIGITAL, SLICE, DRUMS) load safely: DIGITAL tracks stay silent, SLICE and DRUMS load as DRUM-X.

## Controls

| Button | Tap to cycle (SELECT walks pages inside a section) | Hold |
|---|---|---|
| HOME | Home, Mixer (DRUM-X: MIXER 1/3, 2/3, 3/3) | Menu |
| SEQ | STEP / DETAIL / AUTOMATION, then SEQ TOOLS / GENERATION | + OCT±: tools layer |
| GLO | MASTER / CLOCK, then SONG | + OCT±: mutes, solos, levels, tap tempo |
| SAVE | PRESETS / USER / PHRASES, then PROJECT / TOOLS | Undo |
| LFO | LFO 1/3, 2/3, LFO DEST 3/3, then MOD | + a knob: momentary change |
| EDIT | Sound pages, Kit (DRUM-X), Voice | + OCT±: engine layer |
| FX | EFFECTS, SLICER, PUNCH-IN | |
| SCL, ENV, ARP | Their pages | SCL + OCT±: its layer |
| REC | Arms the track | |

- Page buttons act when you let go. HOME always returns home.
- OCT+ confirms, OCT- goes back. In the menu they change the value.
- On SEQ > STEP, hold a step and turn a knob to lock that value on the step. PRESETS moves held steps in time.
- To save a sound: stop, tap SAVE, pick a slot with KNOB 1, press OCT+ twice. Name it with the keys.

## Menu

Hold HOME. SELECT changes tab, ALGORITHM moves through rows, any knob or OCT± changes the value.

| Tab | Settings |
|---|---|
| DISPLAY | COLOR (9 palettes), LARGE knob cards, LEDS, SCREEN OFF timer, SCOPE source, HOME view (scope, tracks or levels) |
| CONTROL | HOLD (retired), FX LATCH, BPM LOCK, SCALE LEDS, STEP PREVIEW, CHORD ENTRY (HOLD or ADD) |
| AUDIO | SPEAKER EQ, USB LEVEL, CLICK, CLICK LEVEL, COUNT-IN, TUNE |
| MIDI | MIDI IN (SEL, or a block of four channels) |
| SYSTEM | BOOT RESTORE, HARDWARE CALIBRATION, INFO, ABOUT (USB SERIAL only in a `FELUCCA_CDC=1` developer build) |

New settings default to the old behaviour. The web editor's Settings tab shows the same tabs. A few notes:

- SPEAKER EQ shapes the headphone and USB output too. Keep it on FLAT when recording.
- The click goes to the speaker and headphones only, never to USB audio.
- BOOT RESTORE OFF still writes the autosave. It only skips loading it at power-on.

## Scale keyboard

On the SCL page, MAP decides how the keys and the sequence use the scale:

| MAP | Keys | Sequence |
|---|---|---|
| OFF | Chromatic piano | As stored |
| KBD | White keys walk the scale from ROOT, black keys play the nearest scale note | As stored |
| CURR | Like KBD | Mapped onto this track's scale |
| ALL | Like KBD | Mapped, and every track shares this ROOT / SCALE while ALL is on |
| SEQ | Chromatic | Mapped onto the scale |

TRN transposes playback without changing the stored steps. Hold a step and turn TRN for a per-step PITCH lock. Kits
ignore TRN and PITCH. Press SCL again for the CHORD page: chord keys (triads, sevenths, shapes, AUTO, MELO),
voicing and STRUM.

## MIDI and DAWs

- **MIDI IN:** SEL plays the selected track from every channel. CH1-4 (or 5-8, 9-12, 13-16) maps four channels to
  the four tracks.
- **CC 71..74** turn the on-screen knobs. **CC 75 / 76 / 77** are SELECT, ALGORITHM and PRESETS.
- **Other CCs:** 5 glide, 7 level, 10 pan, 91 reverb send, 93 chorus mix, 94 delay send. Program Change loads a
  sound.
- **Knobs out:** KNOB 1-4 send relative CCs (Bin Offset) on the track's channel. Track 1 is CC 16-19, track 2
  20-23, track 3 24-27, track 4 28-31. PRESETS sends pitch bend and SELECT sends the mod wheel (CC 1).
- **Ableton Live:** enable Remote for the FM-1 input, enter MIDI Map Mode, turn a knob. Or use the Control Surface
  script in [`ableton/FM-1/`](ableton/README.md).

## Coming from Felucca 1.5

Every Felucca 1.5 / 1.5.1 project, user preset and setting loads. The installer replaces only the app and its
loader.

Saving on Araminta (by hand, or when autosave runs after you stop) writes the current project format, FUN20.
Felucca 1.5 and older Araminta builds cannot open it. **Back up in the web editor before your first save** if you
still need a 1.5-readable copy.

Return to official V15 gives you stock firmware, not Felucca 1.5. The web editor's full backup skips the FM6 voice
bank, so keep its `.syx` file.

Araminta uses flash at `0xF4000..0xF9FFF` and `0x95000..0x96FFF`. Stock V15 erases the second area on every boot. For a
byte-exact return to stock, keep the full flash dump from `tools/fm1_flash_dump.py`.

## If the FM-1 does not start

If an update stops halfway and the screen stays black, check whether the computer sees a USB device called
**WL80UBOOT** (ID 4C4A:8057). That is the chip's boot mode and the FM-1 can be recovered:

1. Try another USB data cable, close other apps that use MIDI, and run the web installer again.
2. If it stays in boot mode, [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter) can read and write
   the flash through a Seeed XIAO RP2040. Back up the flash, then write M-VAVE's FM-1.fwsc.
3. Still stuck? Open an [issue](https://github.com/Wulgaren/Araminta/issues).

## Repository

| Path | What |
| --- | --- |
| `firmware/` | Firmware: `src/` app, `hal/` hardware layer, `loader/` update loader |
| `tools/` | Build script, generators, package maker, installer, uploaders |
| `assets/` | UI font, icons, CC0 samples, the CR-78 kit |
| `docs/` | Design notes, audits, the parameter contract (`PARAM_CONTRACT.md`) |
| `web/` | Web installer, editor and browser emulator |
| `tests/` | Host tests |
| `LICENSES/` | Licences of bundled fonts, icons, ported DSP and SDK files |

## Support

Reproducible bugs go in [Issues](https://github.com/Wulgaren/Araminta/issues), one per report. Ideas go in
[Discussions](https://github.com/Wulgaren/Araminta/discussions) when it is open, or an issue marked as a request.
Pull requests are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md).

Araminta is a fork. Felucca itself lives at [hugelton/Felucca](https://github.com/hugelton/Felucca). If it is useful
to you, [sponsor on GitHub](https://github.com/sponsors/hugelton) or donate on [itch.io](https://hugelton.itch.io/felucca).

## AI disclaimer

Araminta is developed with AI coding agents for coding, testing, documentation, translation and maintenance.
Design, features, sound choices and direction stay with the maintainer and contributors.
No generative AI makes music, icons or artwork for this project.
Upstream context: [On AI-Assisted Development and Responsibility](https://github.com/hugelton/Felucca/discussions/166).

## Credits

- **Fork:** [Wulgaren/Araminta](https://github.com/Wulgaren/Araminta) by [Wulgaren](https://github.com/Wulgaren)
- Felucca 1.6: the LED rows switch off before a flash erase (#236, kurogedelic, GPL-3.0)
- [JIANT FM](https://github.com/juanjiant-bit/JIANT) by [juanjiant-bit](https://github.com/juanjiant-bit) (GPL-3.0-only):
  the DRUM-X voice and kits (V10.7.24), the MIDI punch-ins, the SHIMR and CLOUD reverbs and the GLITCH effect
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
