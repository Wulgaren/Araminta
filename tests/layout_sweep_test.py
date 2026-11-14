# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the sweep CLI with stand-ins for its expensive build and emulator commands."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class LayoutSweepTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="felucca-sweep-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "tests").mkdir()
        shutil.copyfile(Path(__file__).with_name("layout_sweep.sh"), self.root / "tests/layout_sweep.sh")
        self.write("build.sh", """#!/bin/sh
set -e
[ "${FAIL_BUILD:-0}" != 1 ] || exit 1
mkdir -p "$FELUCCA_OUT"
if [ ! -f "$FELUCCA_OUT/felucca.o" ]; then
    echo compiled >> compile.log
    echo object > "$FELUCCA_OUT/felucca.o"
fi
""")
        self.write("build/emu-target/release/emu_test", """#!/bin/sh
set -e
scenarios=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --scenarios) scenarios=$2; shift 2 ;;
        --scenario) single=$2; shift 2 ;;
        --ms) ms=$2; shift 2 ;;
        --clear) shift ;;
        *) shift 2 ;;
    esac
done
[ -n "$scenarios" ] || scenarios="$single:$ms"
echo boot >> boots.log
for s in $scenarios; do
    printf '{"scenario":"%s","ms":%s,"cpu_pct":80.0,"late":0,"halves":100}\n' "${s%%:*}" "${s##*:}"
done
exit "${FAIL_RUN:-0}"
""")
        self.write("bin/python3", '#!/bin/sh\nexec "' + sys.executable.replace("\\", "/") + '" "$@"\n')
        self.env = dict(os.environ, LAYOUTS="0", PATH=str(self.root / "bin") + os.pathsep + os.environ["PATH"])
        self.shell = shutil.which("sh")
        if self.shell is None:
            self.fail("layout_sweep_test requires sh")

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")
        path.chmod(0o755)

    def sweep(self, *scenarios, **env):
        return subprocess.run(
            [self.shell, "tests/layout_sweep.sh", *scenarios], cwd=self.root,
            env=dict(self.env, **env), text=True, capture_output=True, timeout=20,
        )

    def test_repeated_sweep_reuses_build(self):
        for _ in range(2):
            result = self.sweep("stack:6000")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / "compile.log").read_text().splitlines(), ["compiled"])

    def test_failed_build_cannot_reuse_previous_results(self):
        result = self.sweep("stack:6000")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.sweep("stack:6000", FAIL_BUILD="1")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("stack: cpu_pct median", result.stdout)

    def test_scenarios_share_one_runner_per_layout(self):
        result = self.sweep("dry:3000", "stack:6000")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("dry: cpu_pct median", result.stdout)
        self.assertIn("stack: cpu_pct median", result.stdout)
        self.assertEqual((self.root / "boots.log").read_text().splitlines(), ["boot"])

    def test_runner_failure_rejects_even_complete_json(self):
        result = self.sweep("stack:6000", FAIL_RUN="7")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("stack: cpu_pct median", result.stdout)

    def test_missing_scenario_is_rejected(self):
        self.write("build/emu-target/release/emu_test", """#!/bin/sh
printf '%s\\n' '{"scenario":"dry","ms":3000,"cpu_pct":80.0,"late":0,"halves":100}'
""")
        result = self.sweep("dry:3000", "stack:6000")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("results do not match", result.stdout)


if __name__ == "__main__":
    unittest.main()
