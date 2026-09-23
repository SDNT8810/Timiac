"""Build three ESP roles and one shared six-axis Octopus image, without flashing hardware."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "Marlin/marlin-2.1.2.7"
STAGED = ROOT / ".build/marlin-two-motor"
PROFILE = ROOT / "Marlin/profiles/twelve-motor/profile.json"


def prepare_marlin():
    profile = json.loads(PROFILE.read_text(encoding="utf-8"))
    # Recreate the staged Marlin tree for every build.  A previous recovery
    # experiment added source files here; `copytree(..., dirs_exist_ok=True)`
    # would otherwise preserve them even after they were removed upstream.
    shutil.rmtree(STAGED, ignore_errors=True)
    shutil.copytree(SOURCE, STAGED, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns(".pio", ".git", ".venv", "__pycache__"))
    for name in ("Configuration.h", "Configuration_adv.h"):
        text = (SOURCE / "Marlin" / name).read_text(encoding="utf-8")
        for key, value in profile[name].items():
            pattern = rf"^[ \t]*(?://)?#define[ \t]+{re.escape(key)}\b[^\n]*"
            replacement = f"// TIMIAC: {key} disabled" if value is None else f"#define {key} {value}"
            text, count = re.subn(pattern, lambda _: replacement, text, flags=re.MULTILINE)
            if count != 1:
                raise ValueError(f"Expected one {key} definition in {name}, found {count}")
        if name == "Configuration.h":
            text += "\n// Explicit Timiac profile pin mapping.\n"
            text += "\n".join(f"#define {key} {value}" for key, value in profile.get("pins", {}).items()) + "\n"
        (STAGED / "Marlin" / name).write_text(text, encoding="utf-8")
    # Octopus defines X/Y/Z socket pins in its board header, after
    # Configuration.h is read. Apply profile socket reassignment after all
    # board pin defaults and their post-processing, but before Marlin derives
    # its final axis conditions and instantiates the TMC drivers.
    post_pins = profile.get("post_pins", {})
    if post_pins:
        overlay = STAGED / "Marlin" / "src" / "inc" / "timiac_post_pins.h"
        lines = ["#pragma once", "// Generated from Marlin/profiles/twelve-motor/profile.json."]
        for key, value in post_pins.items():
            lines += [f"#ifdef {key}", f"#undef {key}", "#endif", f"#define {key} {value}"]
        overlay.write_text("\n".join(lines) + "\n", encoding="utf-8")
        config = STAGED / "Marlin" / "src" / "inc" / "MarlinConfig.h"
        text = config.read_text(encoding="utf-8")
        needle = '#include "../pins/pins.h"'
        if text.count(needle) != 1:
            raise ValueError("Expected one Marlin pin-header include for the Timiac socket override")
        text = text.replace(needle, needle + '\n#include "timiac_post_pins.h"')
        config.write_text(text, encoding="utf-8")
    # Keep Marlin stock. The attempted remote-reset/watchdog overlay could
    # inject M112 while an ordinary G28 was still in progress.
    print(f"Prepared shared six-axis source: {STAGED}", flush=True)
    return STAGED


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=("all", "master", "master_release", "board1", "board2", "marlin"), nargs="?", default="all")
    parser.add_argument("--prepare-only", action="store_true", help="generate the six-axis Marlin source without compiling")
    args = parser.parse_args()
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
