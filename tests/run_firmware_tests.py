"""Host tests: pip install ziglang==0.13.0, then python tests/run_firmware_tests.py."""
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / ".build/tests"
OUTPUT.mkdir(parents=True, exist_ok=True)
# Zig's optimized C++ builds define NDEBUG; keep every test assertion active.
for name in ("firmware_test", "gcode_cli", "octopus_session_test", "coordinator_test", "coordinator_release_test", "bridge_link_test"):
    source = "coordinator_test" if name == "coordinator_release_test" else name
    target = OUTPUT / (name + (".exe" if sys.platform == "win32" else ""))
    includes = ["-DTIMIAC_RELEASE=1"] if name == "coordinator_release_test" else []
    if source in ("coordinator_test", "bridge_link_test"):
        json_headers = ROOT / "ESP_Timiac/.pio/libdeps/timiac_master/ArduinoJson/src"
        if not (json_headers / "ArduinoJson.h").is_file():
            raise SystemExit("Build the master once to install its pinned ArduinoJson dependency, then rerun host tests.")
        includes += ["-I", str(ROOT / "tests/host"), "-I", str(json_headers)]
    subprocess.run([sys.executable, "-m", "ziglang", "c++", "-std=c++11", "-O1", "-UNDEBUG",
                    *includes, str(ROOT / f"tests/{source}.cpp"), "-o", str(target)], check=True)
    if source != "gcode_cli":
        subprocess.run([str(target), str(ROOT / "gcodes")], check=True)

# Lumiac's per-motor latch must handle every switch order and bounce without
# stopping the other motors, and never complete with a missing switch.
subprocess.run([sys.executable, "-m", "ziglang", "c++", "-std=c++14", "-O1",
                "-fconstexpr-steps=100000000", "-c", str(ROOT / "tests/concurrent_homing_contract.cpp"),
                "-o", str(OUTPUT / "concurrent_homing_contract.o")], check=True)
print("Concurrent homing: all 720 switch orders, bounce, and missing-switch checks passed")
