"""Host tests: pip install ziglang==0.13.0, then python tests/run_firmware_tests.py."""
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / ".build/tests"
OUTPUT.mkdir(parents=True, exist_ok=True)
# Zig's optimized C++ builds define NDEBUG; keep every test assertion active.
for source in ("firmware_test", "gcode_cli", "octopus_session_test", "coordinator_test", "bridge_link_test"):
    target = OUTPUT / (source + (".exe" if sys.platform == "win32" else ""))
    includes = []
    if source in ("coordinator_test", "bridge_link_test"):
        json_headers = ROOT / "ESP_Timiac/.pio/libdeps/timiac_master/ArduinoJson/src"
        if not (json_headers / "ArduinoJson.h").is_file():
            raise SystemExit("Build the master once to install its pinned ArduinoJson dependency, then rerun host tests.")
        includes = ["-I", str(ROOT / "tests/host"), "-I", str(json_headers)]
    subprocess.run([sys.executable, "-m", "ziglang", "c++", "-std=c++11", "-O1", "-UNDEBUG",
                    *includes, str(ROOT / f"tests/{source}.cpp"), "-o", str(target)], check=True)
    if source != "gcode_cli":
        subprocess.run([str(target), str(ROOT / "gcodes")], check=True)
