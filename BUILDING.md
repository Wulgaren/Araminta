# Building Felucca

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader) |

## Windows (Git Bash)

The build runs natively with the Windows JieLi toolchain. It needs Git for Windows,
Python 3, and the three AC79 SDK files listed below.

Install Python dependencies locally:

```powershell
python -m pip install --target build/python Pillow fonttools
```

Download the [official Windows toolchain](https://pkgman.jieliapp.com/s/win-toolchain).
Set `JIELI_TOOLCHAIN` to the directory containing `pi32/bin/clang.exe`, and `AC79_SDK`
to the SDK checkout:

```powershell
$env:JIELI_TOOLCHAIN = 'C:/JL'
$env:AC79_SDK = 'D:/path/to/fw-AC79_AIoT_SDK'
bash ./build.sh
```

For a portable setup, the defaults are `build/deps/jieli-windows/C$/JL` for the
extracted Windows toolchain and `build/deps/ac79` for the SDK. With those in place,
run `bash ./build.sh` directly. The script loads Python packages from `build/python`
when that directory exists. `JIELI_DOCKER=1` selects the Linux toolchain in Docker.

## Prerequisites (macOS / Linux)

- Python 3 with Pillow and fontTools: `pip3 install Pillow fonttools` (the UI font and icons are
  rasterised at build time)
- Docker Desktop on macOS. The Linux toolchain runs on x86-64; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).
- Emscripten (optional, `emcc`: the browser emulator, `web/emu/`).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK` on macOS / Linux; Windows defaults above).

`./build.sh --release 1.0` makes a release build: the package identity becomes `FM-1_910`
and the version string `v1.0`; the package is `build/felucca-1.0.fwsc`, and
`build/release-1.0/` holds what a release ships: the package, the app
(`felucca-1.0-app.bin`), `SHA256SUMS`, the sample attribution, `LICENSE`, `LICENSING.md` and
`LICENSES/` (the package contains Apache-2.0 SDK files, so the licence texts travel with it).

Build options (environment, `0` or `1`; defaults in `firmware/src/felucca.c`, `core.h` and `icons.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console |
| `FELUCCA_CDC_DEFAULT` | 1 | `0`: the console is built in but left out of USB from boot (as MENU > USB SERIAL OFF) |
| `FELUCCA_UAC` | 1 | USB audio input (the master output, stereo, 44.1 or 48 kHz) |
| `FELUCCA_UAC_48K` | 1 | `0`: the USB audio input at 44.1 kHz only, as up to 1.0.5.2 (`firmware/src/usb.c`) |
| `FELUCCA_UAC_AS_FIRST` | 1 | the audio control header lists the audio streaming interface before the MIDI one, so the USB audio driver of older macOS (up to 15) finds it; `0`: 1.0 to 1.5's order, byte for byte (`firmware/src/usb.c`) |
| `FELUCCA_UART` | 1 | TRS MIDI IN |
| `FELUCCA_ICONS` | 1 | parameter icons on the knob cards |
| `FELUCCA_FM4` | 0 | the retired DIGITAL engine (4-operator FM) instead of its FM6 conversion |

`FELUCCA_USB_LAYOUT` (`0` to `3`, default `0`) picks other USB descriptor layouts for testing; see
`firmware/src/usb.c`.

## The stock supply setup

`firmware/hal/fm1_power.h` applies the supply portion of stock V15's constant-folded
`power_init` / `__hw_power_init` before the guards, CPU1 and the audio start, with interrupts off and
CPU1 held. It uses the bounded P33 driver in `firmware/hal/fm1_sys.h`; no SDK power manager, sleep
timers or RTOS services are linked in, and the PLL is not changed.

The reference is the unmodified `FM-1_015` package with SHA-256
`db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a`. Its decoded boot config contains
`SYS_CLK=360000000`, `HSB_DIV=1`, `LSB_DIV=2` and no `DVDD`, `DCDC14` or `VDDIO`. Its SPL's default
branches at `0x01C03E7C..0x01C03EAC` select SYSVDD 11 / VDC14 3. The application task `app_core`
(entry `0x02004CF2`) then executes the supply sequence at `0x02004F90..0x0200509A`:

- Enable LDO13, wait, enable DCDC13, wait, disable LDO13, wait. Each wait is stock's 1,000-iteration
  `nop` / increment / branch loop, **not** 1,000 us.
- Write P33 analog registers 1, 5, 6, 7, 8 with `20`, `40`, `0C`, `A5`, `96` (hex), then set register 9
  bit 4.
- Select VDDIOM 4 (3.2 V), VDC14 7 (1.60 V), VDDIOW 3 (3.2 V). Retain the stock reads of registers
  6, 5, 5 in that order.
- Set register 0 bit 7 (`RC_250k_EN`), clear register 0x16 [5:0], set register 2 bit 6 (`VDDOK_DIS`),
  clear register 0x17 bit 5, then set SYSVDD 15 (1.38 V) **last**.
- Read registers 0, 5, 6 and 9 back. Any P33 timeout or a wrong read-back stops the sequence and keeps
  one core; MENU > ABOUT shows `RAILS 7/15 (3/11)` (VC14/SYSVDD after, and what the boot loader left)
  or `RAILS FAIL 1 CORE`.

`tests/power_test.c` replays the resolved bus transactions, every transfer timeout and the read-back
faults on the build machine.

## Code size

The app runs from XIP flash: `.text` and `.rodata` stay in flash (the XIP region in `firmware/app.ld`),
and only the flash driver (`.ram_text`) and core 1's idle loop (`.c1_text`) are copied to RAM. The
`size:` line that the build prints comes from `tools/size_fns.py`: every main-loop function (UI, editor,
stores, sequencer, LCD) carries LLVM's `minsize`, which is what the JieLi clang 4 has instead of `-Oz`,
while anything an interrupt can reach keeps `-Os`. That changes the **flash image only**, never RAM
(`.data+.bss`) or the pool. `FELUCCA_SIZE=0` builds everything at `-Os` for comparison.

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works, with no
instrument sets in the SAMPLE engine: they play a plain sine at the note's pitch instead, and the
screen says NO SAMPLE.

## Browser emulator

```
./build.sh
web/emu/build.sh
python3 -m http.server -d build/emu 8790     # open http://localhost:8790/
```

`web/emu/felucca_web.c` builds the firmware's sources as the host tests do, against a simulated FM-1, and
Emscripten compiles it to WebAssembly (`build/emu/felucca.wasm`, with `index.html`, `worklet.js` and the font in `fonts/`). The page
keeps the flash's storage sectors (projects, user presets, settings) in the browser. Not emulated: USB (audio,
MIDI out, the editor, updates), TRS MIDI and the user sample slots. A build of it is on the site:
<https://hugelton.github.io/Felucca/webapp/try/>.

## Tests

```
tests/run_tests.sh
```

Full host gate (several minutes). Argv after the script name is ignored; it always runs the whole suite. Agents: iterate and hand off per [AGENT.md](AGENT.md) (focused host binaries / web tests; full script when 3+ host binaries are in scope). `SANITIZE=0` skips the ASan/UBSan and fuzz passes; the browser emulator already skips when `emcc` is missing.

Runs the host tests and, with Node.js, the web page tests (and, with Emscripten too, the browser emulator's). Run it after `./build.sh`
(it uses `build/` and needs `AC79_SDK` set as for the build). The suites cover flash storage,
user presets, projects of every format, backup, the keys and knobs, MIDI (USB, TRS, clock,
control), USB audio, the update entry and loader, the command-line installer, the UI (the real
drawing code against stubs: every screen in every palette is rendered and checked for clipped or
overlapping text; PNGs land in `build/ui_new/`), every engine (DRUM, NOISE, PHYS, FM6, SLICE, the
DIGITAL conversion), the chord keys, the modulation matrix, the FX layer, the reverbs, the SLICER
and swing. With `DAISYSP` pointing at a DaisySP checkout, the PHYS models are also compared with
their floating-point originals; without it that test is skipped.

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

## Install

Use the web installer in Chrome or Edge:
<https://hugelton.github.io/Felucca/webapp/installer/>. It installs the released package.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/felucca.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py build/felucca.fwsc dev /tmp/felucca-site
cd /tmp/felucca-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/installer/
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).
