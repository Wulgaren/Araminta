#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check tests/emu/clickcheck.py against synthetic renders."""
import contextlib
import io
import math
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import clickcheck  # noqa: E402

FRAMES = 3000
GATE = 2000


def tone(frames: int, amplitude: int = 100) -> list[int]:
    """A quiet 440 Hz test tone."""
    return [int(amplitude * math.sin(2.0 * math.pi * 440.0 * i / 44100.0)) for i in range(frames)]


class ClickCheckTest(unittest.TestCase):
    def test_identical_renders_pass(self):
        left = tone(FRAMES, 800)
        right = [-sample for sample in left]
        for channel in (left, right):
            self.assertTrue(clickcheck.null_test(channel, channel)[0])
            self.assertTrue(clickcheck.discontinuity_test(channel, channel)[0])
            self.assertTrue(clickcheck.attack_test(channel, channel)[0])

    def test_spike_fails_discontinuity_and_null(self):
        before = tone(FRAMES)
        after = list(before)
        after[1500] += 3000

        ok, max_abs, frame, first = clickcheck.null_test(before, after)
        self.assertFalse(ok)
        self.assertEqual((max_abs, frame, first), (3000, 1500, 1500))

        ok, new_jumps, _boundary, _before_only = clickcheck.discontinuity_test(before, after)
        self.assertFalse(ok)
        self.assertIn(1500, new_jumps)

    def test_step_at_block_boundary_fails_boundary_check(self):
        before = tone(FRAMES)
        after = [sample + 500 if i >= 1280 else sample for i, sample in enumerate(before)]

        ok, _new_jumps, boundary, _before_only = clickcheck.discontinuity_test(before, after)
        self.assertFalse(ok)
        self.assertIn(1280, boundary)

    def test_moved_onset_fails_attack_check(self):
        before = [0] * 1600 + [1000 + i % 97 for i in range(600)]
        after = [0] * 1601 + [1000 + i % 97 for i in range(599)]

        ok, before_onsets, after_onsets = clickcheck.attack_test(before, after)
        self.assertFalse(ok)
        self.assertEqual(before_onsets, [1600])
        self.assertEqual(after_onsets, [1601])

    def test_one_lsb_gate(self):
        before = tone(FRAMES, 500)
        after = [
            sample + (1 if i % 2 == 0 else -1) if i >= GATE else sample
            for i, sample in enumerate(before)
        ]

        gated = clickcheck.null_test(before, after, gate_from=GATE)
        self.assertTrue(gated[0])
        self.assertEqual(gated[1], 1)
        self.assertFalse(clickcheck.null_test(before, after)[0])

    def test_wav_round_trip_and_diff(self):
        left = [0, 1, -1, 32767, -32768, 1234] + list(range(-40, 40))
        right = list(reversed(left))

        before_left = [0, 100, -100, 32767, -32768, 5]
        before_right = [10, -20, 30, -40, 50, -60]
        after_left = [5, 90, -101, -32768, 32767, 10]
        after_right = [10, -20, 31, -40, 50, -65]

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            round_path = root / "round.wav"
            clickcheck.write_wav(str(round_path), left, right)
            read_left, read_right, rate = clickcheck.read_wav(str(round_path))
            self.assertEqual(rate, 44100)
            self.assertEqual(read_left, left)
            self.assertEqual(read_right, right)

            before_path = root / "before.wav"
            after_path = root / "after.wav"
            diff_path = root / "diff.wav"
            clickcheck.write_wav(str(before_path), before_left, before_right)
            clickcheck.write_wav(str(after_path), after_left, after_right)
            with contextlib.redirect_stdout(io.StringIO()):
                status = clickcheck.main(
                    [str(before_path), str(after_path), "--diff", str(diff_path)]
                )
            self.assertEqual(status, 1)
            diff_left, diff_right, diff_rate = clickcheck.read_wav(str(diff_path))
            self.assertEqual(diff_rate, 44100)
            self.assertEqual(diff_left, [5, -10, -1, -32768, 32767, 5])
            self.assertEqual(diff_right, [0, 0, 1, 0, 0, -5])


if __name__ == "__main__":
    unittest.main()
