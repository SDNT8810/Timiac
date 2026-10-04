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
            # Apply the production speed patch to pinned upstream sources, then
            # execute its real feed calculation and scoped G28 capture too.
            for name in ("module/motion.h", "module/motion.cpp", "gcode/calibrate/G28.cpp", "core/utility.h"):
                path = "Marlin/src/" + name
                source = subprocess.check_output(["git", "-C", str(ROOT / "Marlin/marlin-2.1.2.7"),
                                                   "show", "2.1.2.7:" + path]).decode()
                target = directory / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(source, encoding="utf-8", newline="\n")
            subprocess.run(["git", "apply", str(ROOT / "Marlin/patches/motion-speed.patch")], cwd=directory, check=True)
            motion = (directory / "Marlin/src/module/motion.h").read_text()
            feed = motion[motion.index("extern uint8_t timiac_homing_percent;"):motion.index("feedRate_t get_homing_bump_feedrate")]
            (directory / "homing_feedrate.inc").write_text(feed)
            g28 = (directory / "Marlin/src/gcode/calibrate/G28.cpp").read_text()
            capture = next(line for line in g28.splitlines() if "REMEMBER(timiac_home_speed" in line)
            (directory / "homing_speed_capture.inc").write_text(capture)
            utility = (directory / "Marlin/src/core/utility.h").read_text()
            restorer = utility[utility.index("template<typename T>\nclass restorer"):utility.index("#define RESTORE(N)")]
            (directory / "restorer.inc").write_text(restorer)
            executable = directory / "homing.exe"
            subprocess.run([sys.executable, "-m", "ziglang", "c++", "-std=c++11", "-O1", "-UNDEBUG",
                            "-I", str(directory), str(ROOT / "tests/concurrent_homing_test.cpp"),
                            "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
