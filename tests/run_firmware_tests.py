"""Host tests: pip install ziglang==0.13.0, then python tests/run_firmware_tests.py."""
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / ".build/tests"
OUTPUT.mkdir(parents=True, exist_ok=True)
# Zig's optimized C++ builds define NDEBUG; keep every test assertion active.
for source in ("firmware_test", "gcode_cli"):
    target = OUTPUT / (source + (".exe" if sys.platform == "win32" else ""))
    subprocess.run([sys.executable, "-m", "ziglang", "c++", "-std=c++11", "-O1", "-UNDEBUG",
                    str(ROOT / f"tests/{source}.cpp"), "-o", str(target)], check=True)
    if source == "firmware_test":
        subprocess.run([str(target), str(ROOT / "gcodes")], check=True)
for board in (1, 2):
    target = OUTPUT / (f"uart_bridge_{board}" + (".exe" if sys.platform == "win32" else ""))
    subprocess.run([sys.executable, "-m", "ziglang", "c++", "-std=c++11", "-O1", "-UNDEBUG",
                    f"-DTIMIAC_BOARD_ID={board}", str(ROOT / "tests/uart_bridge_test.cpp"),
                    "-o", str(target)], check=True)
    subprocess.run([str(target)], check=True)
