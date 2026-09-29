"""Run the patched Marlin junction normalization on mixed XYZ/ABC directions."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def upstream(name):
    return subprocess.check_output([
        "git", "-C", str(ROOT / "Marlin/marlin-2.1.2.7"), "show",
        f"2.1.2.7:Marlin/src/module/{name}",
    ]).decode("utf-8")


class SixAxisJunctionTests(unittest.TestCase):
    def test_actual_marlin_normalization_does_not_create_false_reversals(self):
        with tempfile.TemporaryDirectory(prefix="timiac-junctions-") as folder:
            directory = Path(folder)
            planner = directory / "Marlin/src/module/planner.cpp"
            planner.parent.mkdir(parents=True)
            planner.write_text(upstream("planner.cpp"), encoding="utf-8")
            subprocess.run(["git", "apply", str(ROOT / "Marlin/patches/six-axis-junctions.patch")],
                           cwd=directory, check=True)
            source = planner.read_text(encoding="utf-8")
            start = source.index("    xyze_float_t unit_vec =")
            end = source.index("    // Skip first block", start)
            (directory / "junction_direction.inc").write_text(source[start:end], encoding="utf-8")
            header = upstream("planner.h")
            start = header.index("FORCE_INLINE static void normalize_junction_vector")
            end = header.index("}", start) + 1
            (directory / "junction_normalize.inc").write_text(header[start:end], encoding="utf-8")
            executable = directory / "junctions.exe"
            subprocess.run([sys.executable, "-m", "ziglang", "c++", "-std=c++11", "-O1", "-UNDEBUG",
                            "-I", str(directory), str(ROOT / "tests/six_axis_junction_test.cpp"),
                            "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
