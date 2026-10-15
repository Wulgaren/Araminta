# Audio effects investigation

## Scope and state

Recorded 2026-10-15. Compare Araminta's SOFT, HARD and FUZZ distortion, then
M-VAVE's published FM-1 overdrive and delay, especially the sound of changing
free millisecond delay time. Investigation only; no DSP redesign approved or made.

The checkout was already on `main`. Fetched and merged `origin/main` as
`bdb53b1`. The remote had replaced `36d8d3b` with `31cd1db`; the merge kept
both histories. Incoming changes widened FM6 patch generation counters and
invalidated cached effective patches, and adjusted UI test reset/order.
Fetch also reported an existing `v2.3` tag conflict; the local tag was preserved.
Nothing was pushed. Production `bash ./build.sh`, `ui_test` and `inschain_test`
passed. The working tree was clean after that investigation.

## Araminta distortion findings

Relevant code: `firmware/src/fx.c` (`ins_shape`, `ins_run_dist`, `ins_lpk`),
`firmware/src/dsp.c` (`softclip`), and `firmware/src/params.c`.

- SOFT uses the shared interpolated tanh lookup.
- HARD clamps to -24000..24000.
- FUZZ clips positive samples at 20000 and uses half-amplitude soft clipping
  for nonpositive samples.
- All share drive gain `1 + 3 * drive^2 / 4096`, about 1..12.81 times.
- All share two low-pass stages. Cutoff index is
  `clamp(96 - (drive >> 2) + tone, 0, 127)`.
- At neutral TONE, cutoff falls from about 3455 Hz at DRIVE 0 to 746 Hz at 127.
- The stage removes input DC and output DC and blends dry/wet with a slew.

A temporary C probe included the actual host/DSP sources and called
`ins_run_dist` with fresh state per case, fully wet. Input was a 24000-peak
sine or saw with a 128-sample period, about 344.5 Hz at 44.1 kHz. It rendered
8192 settling samples and measured the following 8192. Pearson correlation
removes mean and ignores overall gain; it measures waveform similarity,
not perceptual equivalence. These are isolated-stage measurements, not a
claim that every preset or transient sounds identical.

| Input | DRIVE, internal 0..127 | TONE | SOFT/HARD correlation | HARD/FUZZ correlation | SOFT/FUZZ correlation |
| --- | ---: | ---: | ---: | ---: | ---: |
| Sine | 0 | 0 | 0.99934 | 0.98903 | 0.99019 |
| Sine | 32 | 0 | 0.99652 | 0.99820 | 0.99572 |
| Sine | 64 | 0 | 0.99838 | 0.99942 | 0.99887 |
| Sine | 96 | 0 | 0.99988 | 0.99987 | 0.99984 |
| Sine | 127 | 0 | 0.99999 | 0.99996 | 0.99996 |
| Saw | 64 | 0 | 0.99362 | 0.99815 | 0.99488 |
| Saw | 96 | 0 | 0.99836 | 0.99943 | 0.99872 |
| Sine | 64 | 63 | 0.99431 | 0.99776 | 0.99552 |
| Sine | 96 | 63 | 0.99701 | 0.99891 | 0.99786 |
| Saw | 64 | 63 | 0.99181 | 0.99669 | 0.99294 |

Opening TONE to 63 gave a 16 kHz cutoff in these cases and increased the
differences, but did not prevent convergence at high drive. The code supports
two causes: SOFT and HARD both approach square waves; FUZZ's unequal saturation
plateaus largely become gain/DC differences after heavy saturation and DC
removal. The common darkening filter further removes distinctive harmonics.

Suggested direction, not implemented: SOFT stays gentler, HARD keeps a sharper
clipping edge, FUZZ has stronger asymmetry or gating. Consider separating drive
from automatic darkening. These changes would alter existing preset sounds.
The temporary probe source and executable were removed after measurement.

## Araminta delay baseline

Relevant code: `firmware/src/fx.c`, `dtime_q8`, `delay_samples`, `dly_process`,
`DLY_SLEW`.

- RATE uses tempo divisions and an integer read tap.
- TIME stores free milliseconds, 1..1486 ms, and converts them to Q8 sample
  positions. Reads interpolate adjacent samples.
- Entering TIME from RATE snaps to the selected time. While TIME stays active,
  the delay position moves toward the target by at most 64/256 samples per
  output sample, rather than crossfading between two fixed taps.
- Thus turning time produces a pitch glide. Increasing delay slows the read
  head and lowers pitch; decreasing delay raises pitch. At the slew limit,
  read speed is approximately 0.75x or 1.25x, about -4.98/+3.86 semitones.
- The buffer stays intact. Feedback passes through a one-pole color filter;
  writes clamp to int16. Shared delay processing serves SEND or CHAIN.

## M-VAVE comparison

User-provided source: <https://github.com/m-vave/fm1_open>.

### Access limitation

On 2026-10-15, opening this link returned 404 and
`git ls-remote https://github.com/m-vave/fm1_open.git` returned "Repository not
found". The public GitHub API listed no repositories for `m-vave`, and its
[organization page](https://github.com/m-vave) said it had no public repositories.
A GitHub repository-name search found no relevant alternative. This does not
establish whether the source was deleted, renamed, or made private. The user
subsequently provided a Discord archive, which enabled the comparison below.

### Verified factory controls

The [official download page](https://www.m-vave.com/download) links an
[English MIDI reference](https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/releaseNote/firmware/FM-1%20MIDI%20EN.docx).
Downloaded and extracted its `word/document.xml` on 2026-10-15. Temporary
copies are `build/FM1-midi-official.docx` and `build/FM1-midi-official.txt`.

The reference documents six factory effects: filter, reverb, delay, distortion,
chorus, and phaser. It calls the drive effect Distortion, not Overdrive.

| Effect | Factory controls in MIDI reference | Current Araminta controls |
| --- | --- | --- |
| Distortion | Switch, Gain, Tone, Level; continuous controls 0..100, CC12..15 | SOFT/HARD/FOLD/FUZZ, DRIVE, TONE, stage MIX; no separate distortion output-level knob |
| Delay | Switch, Decay, Rate, Mix; continuous controls 0..100, CC8..11 | Tempo RATE or free TIME 1..1486 ms, feedback, color, per-track mix |

These verify factory effect categories and controls only. They do not reveal the
factory distortion transfer curve, delay RATE-to-ms mapping, or time-edit DSP.

### Supplied archive and reproducibility

Downloaded the user-supplied Discord attachment `fm1_open.tar.gz` on 2026-10-15.
The download link is temporary; the retained local archive is
`build/fm1_open.tar.gz`. Extracted under `build/vendor-fm1/fm1_open/` after
checking archive paths. All extracted source references below are relative to
that directory.

- SHA256: `ec8dc7fcc6f4b8f02aec568faa47352d45f0a553d407e976428a670b5a3a62ad`.
- Included Git HEAD: `015aa00f3722bb680aeea022804fd01b85e3bab8`.
- README identifies the release as FM1 Open V10. HEAD is dated 2026-10-14;
  its message is "docs: use renamed GitHub repository links".
- Device build uses `app/` and `sdk/lib/libfm1_engine.a`.
  Browser synthesis/effects use separate C sources. See `sdk/Build.ps1:329`,
  `sdk/Build.ps1:400`, and `THIRD_PARTY.md:21`.
- `sdk/Build.ps1:414` describes `FM-1_015.fwsc` as the separate original-V15
  recovery package. It is not evidence that V10 reuses the factory effect rack.

Research artifacts are in ignored `build/effects-research/`: two diagnostic
C programs, result logs, and `native-effects-disassembly.txt`. No vendor build
script was run. No application or test-suite source was edited.

### Which effects are present

V10 exposes shared reverb and delay, each track's sends, and a shared return
level. The UI has no factory-style six-effect rack with independent distortion,
chorus, and phaser slots. See `app/screen.c:1166`, `app/instrument.h:137`,
and `web/engine/sound.c:186`.

There is drive in the 303 engine: Off, Soft, and RAT. It is part of that
instrument, not a general distortion insert for every track. See
`app/synth/bass303.h:16`, `third_party/synth/bass303.c:10`, and
`third_party/synth/bass303.c:493`. Native disassembly also contains the separate
Soft/RAT branches with matching selectors. This does not prove every native
coefficient or sample is identical to the browser implementation.

The archive supports comparison with V10's effects. It does not establish that
these are the algorithms used by original factory V15. That equivalence remains
unverified and would need factory binary analysis or hardware recordings.

### Overdrive comparison

The available V10 models offer more structural separation than Araminta's
shared distortion path:

| Detail | V10 303 Soft | V10 303 RAT | Araminta SOFT/HARD/FUZZ |
| --- | --- | --- | --- |
| Shaping | Biased tanh, approximately `tanh(x + 0.15) - tanh(0.15)` in normalized units | Symmetric tabulated RAT saturation curve | Symmetric tanh, hard clamp, or asymmetric positive-clamp/half-tanh |
| Filtering | Filter before drive and filter after it | Drive-dependent recursive preamp, correction filter, output tone filter | Shared two-stage low-pass darkens as DRIVE rises |
| Sampling | 2x oversampling around shaping, interpolation/decimation filters | Same oversampling structure | Insert waveshaper runs at output sample rate |
| Gain | Exponential drive with inverse output compensation | Separate preamp gain/filter model | Shared quadratic drive; no equivalent inverse drive compensation |

Sources: `third_party/synth/bass303.c:230`, `:424`, `:493`, `:539`, and
`third_party/synth/bass303_state.h:4`. The small-signal bias formula describes
Soft's shaper after its input scaling, not the complete filtered transfer path.

Ran actual browser drive functions and Araminta's actual `ins_run_dist` on a
344.53125 Hz sine at 0.2 normalized full scale. Settled for 8192 samples,
then analyzed 8192. The following uses internal DRIVE 64/127 and Araminta
TONE 0, fully wet. Equal knob values do not mean equal effective gain or tone.
Harmonic ratios are relative to each output's fundamental, so output level
alone cannot explain these differences. Near -100 dBc figures are numerical
residue, not a precise audible-noise specification.

| Model | RMS, normalized FS | Second harmonic, dBc | Third harmonic, dBc |
| --- | ---: | ---: | ---: |
| V10 browser 303 Soft | 0.10511 | -21.83 | -17.13 |
| V10 browser 303 RAT | 0.28057 | -128.52 | -12.44 |
| Araminta SOFT | 0.46593 | -97.96 | -29.54 |
| Araminta HARD | 0.52171 | -80.18 | -35.10 |
| Araminta FUZZ | 0.35205 | -20.29 | -24.55 |

Thus V10 Soft has substantial even harmonics from its bias; its RAT model is
much more symmetric and emphasizes odd harmonics. Araminta SOFT is symmetric.
V10 Soft is conceptually closer to an asymmetric overdrive than to our SOFT.
This is a steady-signal comparison, not a full listening test or a claim that
one sound is better. Native Soft/RAT output measurements remain unperformed.

### Delay comparison, including turning free time

| Detail | FM1 Open V10 | Araminta |
| --- | --- | --- |
| Internal delay rate | 5512.5 Hz, one buffer step per 8 output samples | 44100 Hz |
| Free-time range | About 30.11..352.65 ms | 1..1486 ms |
| Control spacing | About 2.54 ms per internal value | 1 ms |
| Time edit | New integer read tap immediately; 8-sample output reconstruction ramp | Fractional read position slews by at most 0.25 sample per sample |
| Audible implication | Short jump between buffer positions, potentially a click or brief transient | Sustained pitch bend while the read position catches up |
| Buffer/tail | Buffer retained during knob edit | Buffer retained during knob edit |
| Tone/feedback | Two fixed input low-pass stages; feedback up to 0.8 | Adjustable color filter in feedback; different feedback range |

V10 control mapping is `delay = 166 + 14 * value`, in decimated buffer samples.
See `web/synth/mixer.c:8`, `web/engine/effects.c:64`, and `app/screen.c:1170`.
The output ramp reconstructs the decimated signal. It is not a glide between
delay times or a long crossfade between two independently running taps.

Confirmed this on native hardware code as well as browser C. The SDK archive
member `engine_startup.S.o` contains `fm1_fx_set` and `fm1_fx_render`:

- Setter stores the requested time directly.
- Native render reads `delay[(index - time) & 2047]`, advancing the index once
  for each group of eight output samples.
- Native configure computes the same `166 + 14 * value` mapping.
- Evidence retained in `native-effects-disassembly.txt`, delay tap at line 131,
  configuration at line 218, and 303 drive at line 275.

The runnable browser probe warmed the delay with a sine and changed parameter
27 to 106, approximately 98.685 to 299.320 ms. The first rendered sample
exactly matched the new read tap with its 8-sample ramp, and differed from the
old tap. The setter preserved the write index and buffer.

The Araminta probe changed exactly 100 to 300 ms. It settled after 35296 frames,
800.363 ms at block-level measurement resolution, consistent with the 800 ms
predicted by the slew limit. Therefore the effect of turning free-ms time is
not the same in these two releases.

### Validation and remaining decisions

Both diagnostic programs compiled and exited 0 using the bundled POSIX C
compiler. They call the existing DSP functions rather than reimplementing
their algorithms. Results are in `araminta-results.txt` and
`vendor-results.txt` under `build/effects-research/`.

To rerun from the workspace in PowerShell, put `build/posix/usr/bin` on PATH,
then run:

```powershell
& ./build/posix/usr/bin/cc.exe -O2 -w -Ibuild/gen -Ifirmware/src -o build/effects-research/probe_araminta.exe build/effects-research/probe_araminta.c -lm
& ./build/effects-research/probe_araminta.exe
& ./build/posix/usr/bin/cc.exe -O2 -w -Ibuild/vendor-fm1/fm1_open/third_party/synth -Ibuild/vendor-fm1/fm1_open/web/synth -Ibuild/vendor-fm1/fm1_open/sdk/include -o build/effects-research/probe_vendor.exe build/effects-research/probe_vendor.c -lm
& ./build/effects-research/probe_vendor.exe
```

No Araminta sound change was requested or made. A future design decision is
whether to keep our tape-style delay-time glide or offer V10-style immediate
time changes, and whether to adapt the idea of separate biased Soft and RAT
drive paths. Original factory V15 behavior must be investigated separately.

## Recommended drive direction after comparison

The user requested a recommendation and a switch to `dev`. Switched to existing
`dev` at `0e29afd`; the earlier `bdb53b1` merge remains on `main`. The two branches
have not been merged together. These research notes carried across the switch.

Keep the current mode choices and give the existing paths different character:

- SOFT: gentler, mildly biased saturation, with some even harmonics and modest
  gain compensation. Aim for warmth and rounded attacks over its useful range.
- HARD: sharper, symmetric clipping with deliberate filtering before and after
  clipping. Borrow the RAT model's separation of gain, clipping and tone as a
  design reference, without assuming a full pedal model is needed.
- FUZZ: stronger asymmetry and more compression so it remains distinct from
  HARD. Reserve a light gate near the aggressive end only if listening shows
  it helps; avoid chopping quiet notes throughout the range.
- Remove automatic darkening as DRIVE rises. Give TONE consistent control of
  brightness so it cannot conceal the mode differences by default.
- Keep the first implementation in the existing distortion path. Consider
  2x oversampling only after measuring aliasing and device CPU cost.

No extra RAT mode or shared DSP rewrite is proposed. FOLD stays outside this
initial sound redesign. These are recommendations, not implemented DSP changes.
Retuning existing modes will change the sound of presets that use them; account
for that before implementation. Delay changes are a separate decision.

## Default-drive follow-up

The user requested roughly 50% DRIVE so raising MIX alone produces an effect.
Set the default to 64/127, displayed as 50%, while DIST MIX stays 0. Factory
preset loading keeps this stage default instead of restoring the legacy FX()
DIST amount. The offline editor and host preset renderer mirror that default.
Existing saved user sounds and projects retain their recorded drive/mix values.
This changes defaults only; the proposed SOFT/HARD/FUZZ redesign is still pending.

Verification: a temporary diagnostic drove factory loading and the EFFECTS
MIX knob. All 68 playable factory presets started at DRIVE 64 / MIX 0. MIX 0
preserved the input exactly; turning only MIX to 64 changed the signal in both
CHAIN and SEND. The diagnostic was removed after use. Production `bash ./build.sh`,
`inschain_test`, and `node web/test_web.mjs` passed. The web check needed
workspace-relative TEMP/TMP paths to avoid the existing sample tool's parsing
of Windows drive letters.

`ui_test` reported two FM6 operator sound/undo failures. An isolated archive of
unmodified `dev` HEAD `0e29afd`, compiled with the same `-O1` flags, reproduced
both failures with identical measurements. These are pre-existing and outside
the default-drive change. Logs: `build/host/drive-default-ui.log` and
`build/host/drive-default-ui-baseline-o1.log`. No unrelated fixes were made.

Before committing, attempted the full host gate. This exposed two FUN4 fixture
failures introduced by using today's nonzero DRIVE default in synthetic old
projects. Those formats had no independent distortion MIX; their bypass value
was DRIVE 0. Pinned that historical value in the PHYS and SAMPLE PERC fixture
inputs, retaining their existing assertions and leaving runtime migration alone.
The full run also stopped at `fm4_div0_test` linking because this Windows POSIX
compiler lacks `libubsan`; later full-suite checks could not run in that pass.
After pinning the fixtures, `project_test` compiled and passed all checks;
log: `build/host/drive-default-project.log`.
