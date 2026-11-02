# FM-1 in Ableton Live

A Control Surface script for Live 12 that turns the FM-1's sixteen knobs into the selected device's
parameters, in the spirit of the [KeyLab Essential hack](https://github.com/creaktive/KeyLab_Essential_Hacks):
the knobs follow the blue hand instead of being mapped one by one.

Requires the v3.7 firmware (or later), which sends the knobs on USB MIDI.

## What it maps

KNOB 1–4 of each FM-1 track is one page of four parameters of the device selected on the selected
track. ALGORITHM selects the FM-1 track, so it selects the page:

| FM-1 track | Sends | Controls |
|---|---|---|
| 1 | CC 16–19, channel 1 | parameters 1–4 |
| 2 | CC 20–23, channel 2 | parameters 5–8 |
| 3 | CC 24–27, channel 3 | parameters 9–12 |
| 4 | CC 28–31, channel 4 | parameters 13–16 |

On a Rack those are its sixteen Macro Controls (show them with the Macro view selector in Live). On
any other device they are the parameters in Configure order: the first eight work as they do with the
blue hand, the next eight are the second bank.

The parameters come from the same banks Live's own scripts use, so reassigning one is the usual Live
work: rename or remap a Rack macro, or add / reorder parameters in a device's Configure panel. The
script reads them when you select the device.

## Install

Copy the `FM-1` folder into Live's `MIDI Remote Scripts` directory:

- **macOS:** `/Applications/Ableton Live 12 Suite.app/Contents/App-Resources/MIDI Remote Scripts/`
  (the edition name may be `Live 12 Standard` or `Live 12 Intro`).
- **Windows:** `C:\ProgramData\Ableton\Live 12 Suite\Resources\MIDI Remote Scripts\`

macOS will not let you write there as your user; copy with `sudo cp -R` or from Finder with an admin
account. Quit Live first, then restart it.

Open **Preferences → Link, Tempo & MIDI**. Under **Control Surface**, pick **FM-1**; set **Input** to
the FM-1's MIDI port. The output can stay `None`. If Live shows the old cached names from earlier
firmware ("Felucca" or "FM 1"), pick whichever entry is the FM-1; the port name does not matter to the
script, the CCs do.

Keep **Track** enabled for that input if you want the keys to play Live as before. The script itself
does not need **Remote**, but the manual MIDI mappings you already made do.

## Manual mapping still works

A Control Surface and the MIDI map are separate in Live. With the script selected, the sixteen CCs
drive the selected device's parameters **and** any manual mapping you made on the same CCs, so a knob
would move both. To go back to hand mapping, set **Control Surface → None** (your mappings are still
in the set) and turn **Remote** back on. To use both, disable the script; nothing else changes.

## Caveats

- **ROUT must be CH1-4** (the default). With CH5-8 or another block the firmware sends on channels
  5–8 and the script, which listens on 1–4, does not see the knobs. Change `CHANNEL_BASE` in `fm1.py`
  for a different block.
- The script binds parameters directly, so Live does not draw the blue hand on them. The device still
  has to be selected (on the selected track) for its parameters to be the ones controlled.
- No transport, mixer or scene controls: the FM-1 does not send MIDI for its buttons. The knobs, the
  pitch bend (PRESETS) and the mod wheel (SELECT) are what v3.7 puts on the wire.
- This first version is written against Live 12's `_Framework` / `_Generic` and has not been run in
  Live here. If it fails to load, Live's log (`~/Library/Preferences/Ableton/Live 12.x/Log.txt`)
  names the line; send it over.
