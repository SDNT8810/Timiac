"""Exercise the exact routine added by the Lumiac port, not a rewritten model."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ConcurrentHomingTests(unittest.TestCase):
    def test_release_seek_missing_switches_and_cancellation(self):
        patch = (ROOT / "Marlin/patches/concurrent-homing.patch").read_text()
        helper_patch = patch.split("+++ b/Marlin/src/module/spider_homing.h\n", 1)[1]
        helper = "\n".join(line[1:] for line in helper_patch.splitlines() if line.startswith("+")) + "\n"
        self.assertEqual(helper, (ROOT / "shared/spider_homing.h").read_text())
        motion = patch.split("--- a/Marlin/src/module/motion.h")[0]
        added = "\n".join(line[1:] for line in motion.splitlines() if line.startswith("+") and not line.startswith("+++"))
        start = added.index("    static uint8_t spider_pressed_endstops()")
        end = added.index("  #endif // SPIDER_CONCURRENT_HOMING", start)
        with tempfile.TemporaryDirectory(prefix="timiac-homing-") as folder:
            directory = Path(folder)
            (directory / "concurrent_homing_routine.inc").write_text(added[start:end])
            executable = directory / "homing.exe"
            subprocess.run([sys.executable, "-m", "ziglang", "c++", "-std=c++11", "-O1", "-UNDEBUG",
                            "-I", str(directory), str(ROOT / "tests/concurrent_homing_test.cpp"),
                            "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
