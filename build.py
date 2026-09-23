"""Build three ESP roles and one shared six-axis Octopus image, without flashing hardware."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import os
import tempfile
import zipfile
from tools.machine_config import generate, definitions, replace_definitions

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "Marlin/marlin-2.1.2.7"
BUILD_ROOT = Path(os.environ.get("TIMIAC_BUILD_DIR", str(Path(tempfile.gettempdir()) / "TimiacBuild"))).resolve()
STAGED = BUILD_ROOT / "marlin"
PROFILE = ROOT / "Marlin/profiles/twelve-motor/profile.json"


def prepare_marlin():
    machine, identity = generate()
    # Start from the pinned upstream tree, never the modified X-only working tree.
    BUILD_ROOT.mkdir(parents=True, exist_ok=True)
    if STAGED.resolve().parent != BUILD_ROOT or STAGED.name != "marlin":
        raise ValueError("Unsafe staging path")
    if STAGED.exists():
        shutil.rmtree(STAGED)
    archive = BUILD_ROOT / "marlin-source.zip"
    subprocess.run(["git", "-C", str(SOURCE), "archive", "--format=zip", "--output", str(archive), "2.1.2.7"], check=True)
    with zipfile.ZipFile(archive) as source:
        source.extractall(STAGED)
    for name, values in definitions(machine, identity).items():
        path = STAGED / "Marlin" / name
        path.write_text(replace_definitions(path.read_text(encoding="utf-8"), values), encoding="utf-8")
    # Apply explicit signals at the END of the board header, BEFORE pins_postprocess.
    # No late patch to MarlinConfig and no implicit I/J/K extruder socket allocation.
    board = STAGED / "Marlin/src/pins/stm32f4/pins_BTT_OCTOPUS_V1_1.h"
    pins = ["\n// Timiac generation 2: all active sockets explicitly assigned."]
    for axis in machine["axes"]:
        key = axis["marlin"]
        mapping = {"STEP_PIN": "step", "DIR_PIN": "dir", "ENABLE_PIN": "enable",
                   "CS_PIN": "uart", "SERIAL_TX_PIN": "uart", "SERIAL_RX_PIN": "uart",
                   "MIN_PIN": "endstop"}
        for suffix, field in mapping.items():
            pins += [f"#undef {key}_{suffix}", f"#define {key}_{suffix} {axis[field]}"]
    board.write_text(board.read_text(encoding="utf-8") + "\n".join(pins) + "\n", encoding="utf-8")
    shutil.copy2(ROOT / "tests/marlin_motor_socket_mapping.cpp", STAGED / "Marlin/src/timiac_contract.cpp")
    print(f"Prepared {identity} from clean Marlin 2.1.2.7: {STAGED}", flush=True)
    return STAGED


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=("all", "master", "master_release", "board1", "board2", "marlin"), nargs="?", default="all")
    parser.add_argument("--prepare-only", action="store_true", help="generate the six-axis Marlin source without compiling")
    args = parser.parse_args()
    generate()
    if args.prepare_only:
        prepare_marlin()
        return
    release = ROOT / "firmware/two-motor"
    release.mkdir(parents=True, exist_ok=True)
    if args.target in ("all", "marlin"):
        project = prepare_marlin()
        subprocess.run([sys.executable, "-m", "platformio", "run", "-d", str(project),
                        "-e", "STM32F446ZE_btt"], check=True)
        shutil.copy2(project / ".pio/build/STM32F446ZE_btt/firmware.bin", release / "firmware.bin")
        print(f"Octopus firmware (same image for both): {project / '.pio/build/STM32F446ZE_btt/firmware.bin'}")
    if args.target in ("all", "master", "master_release", "board1", "board2"):
        command = [sys.executable, "-m", "platformio", "run", "-d", str(ROOT / "ESP_Timiac")]
        if args.target != "all":
            command += ["-e", f"timiac_{args.target}"]
        subprocess.run(command, check=True)
        roles = ("master", "master_release", "board1", "board2") if args.target == "all" else (args.target,)
        for role in roles:
            shutil.copy2(ROOT / f"ESP_Timiac/.pio/build/timiac_{role}/firmware.bin", release / f"esp_{role}.bin")
    for role in ("master", "master_release"):
        if args.target not in ("all", role):
            continue
        subprocess.run([sys.executable, "-m", "platformio", "run", "-d", str(ROOT / "ESP_Timiac"),
                        "-e", f"timiac_{role}", "-t", "export_master"], check=True)
        master_build = ROOT / f"ESP_Timiac/.pio/build/timiac_{role}"
        layout = json.loads((master_build / "master_flash.json").read_text(encoding="utf-8"))
        for image in layout["images"]:
            if image["file"] == "esp_master.bin":
                image["file"] = f"esp_{role}.bin"
            else:
                source = master_build / image["file"]
                image["file"] = image["file"].replace("master_", role + "_", 1)
                shutil.copy2(source, release / image["file"])
        subprocess.run([sys.executable, "-m", "platformio", "run", "-d", str(ROOT / "ESP_Timiac"),
                        "-e", f"timiac_{role}", "-t", "buildfs"], check=True)

        shutil.copy2(master_build / "littlefs.bin", release / f"{role}_littlefs.bin")
        # Stock ESP32 4 MB default.csv: filesystem begins at 0x290000.
        layout["images"].append({"offset": "0x290000", "file": f"{role}_littlefs.bin"})
        (release / f"{role}_flash.json").write_text(json.dumps(layout, indent=2) + "\n", encoding="utf-8")
    checksums = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(release.glob("*.bin"))}
    (release / "SHA256.json").write_text(json.dumps(checksums, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
