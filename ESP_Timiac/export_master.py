"""Export the actual ESP32 upload layout from PlatformIO; no device access."""
import json
from pathlib import Path
import shutil

Import("env")

def export_images(source, target, env):
    build = Path(env.subst("$BUILD_DIR"))
    images = []
    for address, source_path in env.get("FLASH_EXTRA_IMAGES", []):
        source_path = Path(env.subst(str(source_path)))
        destination = build / ("master_" + source_path.name)
        shutil.copy2(source_path, destination)
        images.append({"offset": address, "file": destination.name})
    images.append({"offset": env.subst("$ESP32_APP_OFFSET"), "file": "esp_master.bin"})
    (build / "master_flash.json").write_text(
        json.dumps({"chip": env.BoardConfig().get("build.mcu"), "flash_size": "4MB",
                    "images": images}, indent=2) + "\n", encoding="utf-8")

# Always refresh the manifest, including when compilation is already up to date.
env.AddCustomTarget("export_master", "$BUILD_DIR/${PROGNAME}.bin", export_images,
                    title="Export ESP32 flash layout", description=__doc__)
