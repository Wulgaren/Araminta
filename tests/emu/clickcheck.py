#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare emulator renders before and after a CPU change.

The emulator runner writes 44.1 kHz stereo 16-bit PCM with --wav. A CPU pull
request may skip or pause silent audio work, but the sound must not change:

  python3 tests/emu/clickcheck.py BEFORE.wav AFTER.wav [--diff DIFF.wav]
      [--gate-from FRAME] [--ratio N]
"""
import argparse
import array
import math
import sys
import wave

SAMPLE_RATE = 44100
RMS_FRAMES = 256
BLOCK_FRAMES = 128
ONSET_ZEROS = 64
INT16_MIN = -32768
INT16_MAX = 32767

Channel = list[int]


def read_wav(path: str) -> tuple[Channel, Channel, int]:
    """Read a stereo 16-bit PCM WAV into its left and right sample lists."""
    with wave.open(path, "rb") as wav:
        if wav.getnchannels() != 2:
            raise ValueError(f"{path}: expected 2 channels, got {wav.getnchannels()}")
        if wav.getsampwidth() != 2:
            raise ValueError(f"{path}: expected 16-bit samples, got {wav.getsampwidth() * 8}-bit")
        rate = wav.getframerate()
        data = wav.readframes(wav.getnframes())
    samples = array.array("h")
    samples.frombytes(data)
    if sys.byteorder == "big":
        samples.byteswap()
    return list(samples[0::2]), list(samples[1::2]), rate


def write_wav(path: str, left: Channel, right: Channel, rate: int = SAMPLE_RATE) -> None:
    """Write interleaved stereo 16-bit PCM with the usual 44-byte header."""
    samples = array.array("h")
    for left_sample, right_sample in zip(left, right):
        samples.append(left_sample)
        samples.append(right_sample)
    if sys.byteorder == "big":
        samples.byteswap()
    with wave.open(path, "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(samples.tobytes())


def diff_channels(before: Channel, after: Channel) -> Channel:
    """after - before, clamped to int16, over the shorter channel."""
    return [
        max(INT16_MIN, min(INT16_MAX, after_sample - before_sample))
        for before_sample, after_sample in zip(before, after)
    ]


def null_test(
    before: Channel, after: Channel, gate_from: int | None = None
) -> tuple[bool, int, int, int]:
    """Compare the channels sample by sample.

    Returns (ok, max_abs_diff, frame_of_max, first_diff_frame); the first diff
    frame is -1 when the channels match. Different lengths always fail. With
    gate_from, samples before it must match exactly and later samples may
    differ by one LSB.
    """
    count = min(len(before), len(after))
    ok = len(before) == len(after)
    max_abs = 0
    max_frame = -1
    first_diff = -1
    for frame in range(count):
        difference = after[frame] - before[frame]
        magnitude = abs(difference)
        if magnitude > max_abs:
            max_abs = magnitude
            max_frame = frame
        if difference != 0 and first_diff < 0:
            first_diff = frame
        if gate_from is None or frame < gate_from:
            if difference != 0:
                ok = False
        elif magnitude > 1:
            ok = False
    return ok, max_abs, max_frame, first_diff


def jumps(channel: Channel, count: int) -> Channel:
    """|x[i] - x[i-1]|, with frame 0 defined as no jump."""
    return [0] + [abs(channel[i] - channel[i - 1]) for i in range(1, count)]


def rms_curve(channel: Channel, count: int) -> list[float]:
    """RMS over the 256 frames ending at each frame, clamped to at least 1.0."""
    curve: list[float] = []
    square_sum = 0
    for frame in range(count):
        sample = channel[frame]
        square_sum += sample * sample
        if frame >= RMS_FRAMES:
            old = channel[frame - RMS_FRAMES]
            square_sum -= old * old
        window = min(frame + 1, RMS_FRAMES)
        curve.append(max(1.0, math.sqrt(square_sum / window)))
    return curve


def discontinuity_test(
    before: Channel, after: Channel, ratio: float = 8.0
) -> tuple[bool, list[int], list[int], int]:
    """Find new sample jumps in after that before did not have.

    Returns (ok, new_jumps, boundary_jumps, before_over_ratio). A frame is a
    new jump when after exceeds ratio times the local before RMS while before
    did not. Every 128-frame boundary also fails when its jump grows by more
    than 2. before_over_ratio is calibration only: how many frames of before
    alone would trip the ratio.
    """
    count = min(len(before), len(after))
    jump_before = jumps(before, count)
    jump_after = jumps(after, count)
    rms_before = rms_curve(before, count)

    new_jumps: list[int] = []
    before_over_ratio = 0
    for frame in range(1, count):
        limit = ratio * rms_before[frame]
        if jump_before[frame] > limit:
            before_over_ratio += 1
        if jump_after[frame] > limit and jump_before[frame] <= limit:
            new_jumps.append(frame)

    boundary_jumps = [
        frame
        for frame in range(BLOCK_FRAMES, count, BLOCK_FRAMES)
        if jump_after[frame] > jump_before[frame] + 2
    ]
    return not new_jumps and not boundary_jumps, new_jumps, boundary_jumps, before_over_ratio


def onsets(channel: Channel) -> list[int]:
    """Frames that start a note after at least 64 exact zeros."""
    found: list[int] = []
    zeros = ONSET_ZEROS
    for frame, sample in enumerate(channel):
        if sample == 0:
            zeros += 1
        else:
            if zeros >= ONSET_ZEROS:
                found.append(frame)
            zeros = 0
    return found


def attack_test(before: Channel, after: Channel) -> tuple[bool, list[int], list[int]]:
    """Require before and after to start their notes on the same frames."""
    before_onsets = onsets(before)
    after_onsets = onsets(after)
    return before_onsets == after_onsets, before_onsets, after_onsets


def report(name: str, check: str, ok: bool, detail: str) -> None:
    """Print one report line for a check on one channel."""
    print(f"{name:<5} {check:<14} {'PASS' if ok else 'FAIL'}  {detail}")


def report_checks(
    before: tuple[Channel, Channel, int],
    after: tuple[Channel, Channel, int],
    gate_from: int | None,
    ratio: float,
) -> bool:
    """Run every check on both channels and return whether all passed."""
    all_ok = True
    for index, name in enumerate(("left", "right")):
        before_channel = before[index]
        after_channel = after[index]

        ok, max_abs, max_frame, first_diff = null_test(before_channel, after_channel, gate_from)
        detail = (
            f"max_abs={max_abs} frame={max_frame} "
            f"first_diff={'none' if first_diff < 0 else first_diff}"
        )
        if gate_from is not None:
            detail += f" gate_from={gate_from}"
        report(name, "null", ok, detail)
        all_ok = all_ok and ok

        ok, new_jumps, boundary_jumps, before_over_ratio = discontinuity_test(
            before_channel, after_channel, ratio
        )
        report(
            name,
            "discontinuity",
            ok,
            f"new_jumps={len(new_jumps)} boundary={len(boundary_jumps)} "
            f"before_over_ratio={before_over_ratio}",
        )
        all_ok = all_ok and ok

        ok, before_onsets, after_onsets = attack_test(before_channel, after_channel)
        report(
            name,
            "attack",
            ok,
            f"before_onsets={len(before_onsets)} after_onsets={len(after_onsets)}",
        )
        all_ok = all_ok and ok
    return all_ok


def main(argv: list[str] | None = None) -> int:
    """Run the CLI and return its exit status (0 pass, 1 fail)."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("before", help="WAV rendered from unmodified firmware")
    parser.add_argument("after", help="WAV rendered from the pull request")
    parser.add_argument("--diff", help="write after - before as a stereo WAV")
    parser.add_argument("--gate-from", type=int, metavar="FRAME", help="allow one LSB from FRAME")
    parser.add_argument("--ratio", type=float, default=8.0, metavar="N", help="jump ratio (default 8)")
    args = parser.parse_args(argv)

    try:
        before = read_wav(args.before)
        after = read_wav(args.after)
    except (OSError, wave.Error, ValueError) as error:
        print(f"clickcheck: {error}")
        return 1

    if before[2] != after[2]:
        print(f"clickcheck: sample rate {before[2]} != {after[2]}")
        return 1

    ok = report_checks(before, after, args.gate_from, args.ratio)

    if args.diff:
        try:
            write_wav(
                args.diff,
                diff_channels(before[0], after[0]),
                diff_channels(before[1], after[1]),
                before[2],
            )
        except OSError as error:
            print(f"clickcheck: cannot write {args.diff}: {error}")
            return 1

    print(f"clickcheck: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
