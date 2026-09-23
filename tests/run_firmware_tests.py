"""Host tests: pip install ziglang==0.13.0, then python tests/run_firmware_tests.py."""
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / ".build/tests"
OUTPUT.mkdir(parents=True, exist_ok=True)
# Zig's optimized C++ builds define NDEBUG; keep every test assertion active.
for source in ("firmware_test", "gcode_cli", "octopus_session_test"):
    target = OUTPUT / (source + (".exe" if sys.platform == "win32" else ""))
    subprocess.run([sys.executable, "-m", "ziglang", "c++", "-std=c++11", "-O1", "-UNDEBUG",
                    str(ROOT / f"tests/{source}.cpp"), "-o", str(target)], check=True)
    if source != "gcode_cli":
        subprocess.run([str(target), str(ROOT / "gcodes")], check=True)
