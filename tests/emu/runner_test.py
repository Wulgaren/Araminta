#!/usr/bin/env python3
"""Exercise shared-boot scenarios against the real firmware and emulator runner."""
import json
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "build/emu-target/release" / ("emu_test.exe" if os.name == "nt" else "emu_test")


class SharedBootTest(unittest.TestCase):
    def test_scenarios_share_boot_and_keep_separate_measurements(self):
        scenarios = [("keys", 9000), ("stack", 6000), ("churn", 6000), ("dry", 3000)]
        result = subprocess.run(
            [str(RUNNER), "--fwsc", "build/felucca.fwsc", "--elf", "build/felucca.elf",
             "--scenarios", " ".join(f"{name}:{ms}" for name, ms in scenarios), "--clear"],
            cwd=ROOT, capture_output=True, text=True, timeout=300,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([(row["scenario"], row["ms"]) for row in rows], scenarios)
        self.assertEqual(result.stderr.count("USB configured:"), 1)
        for row in rows:
            self.assertTrue(row["strict"])
            self.assertGreater(row["cpu_pct"], 0)
            self.assertGreater(row["halves"], 0)
            self.assertLessEqual(row["halves"], row["ms"] * 44100 // 128000 + 2)
        self.assertIn("all eight latched together", result.stderr)
        self.assertNotIn("NOT engaged", result.stderr)


if __name__ == "__main__":
    unittest.main()
