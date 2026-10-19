r"""Flash prepared Timiac development/release firmware (without rebuilding).

Examples:
  python run.py esp1_dev --port COM5
  python run.py esp2_release --port COM5
  python run.py master_dev --port COM5
  python run.py master_release --port COM10  # Paired cloud release included in the repository
  python run.py master_release --host 192.168.4.1  # Local hotspot; no cloud needed
  python run.py octopus_release --sd I:\\
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parent
RELEASE_DIR = ROOT / "firmware" / "two-motor"
CHECKSUMS_FILE = RELEASE_DIR / "SHA256.json"
MASTER_CLOUD_CONFIG = ROOT / "ESP_Timiac" / "include" / "cloud_config.h"
MASTER_LOCAL_CONFIG = ROOT / "ESP_Timiac" / "include" / "config.local.h"
DEFAULT_ESP_PORT = "COM5"
DEFAULT_SD_ROOT = Path("E:/")

DRIVE_REMOVABLE = 2
SUPPORTED_FILESYSTEMS = {"FAT", "FAT32", "EXFAT"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("verify", help="verify every firmware image and the shared machine identity without flashing")

    for mode in ("dev", "release"):
        for role in ("esp1", "esp2", "master", "octopus"):
            command = commands.add_parser(
                f"{role}_{mode}", aliases=[role] if mode == "dev" else [],
                help=f"flash {role} for {mode} mode" + (" (shared microswitch firmware)" if role != "master" else " with matching dashboard"),
            )
            command.set_defaults(role=role, mode=mode)
            if role == "octopus":
                command.add_argument("--sd", type=Path, default=DEFAULT_SD_ROOT, help=r"microSD drive root (default: E:\\)")
                command.add_argument("--allow-fixed-drive", action="store_true",
                                     help="allow the selected card reader when Windows reports it as fixed")
            else:
                transport = command.add_mutually_exclusive_group()
                transport.add_argument("--port", default=DEFAULT_ESP_PORT, help="USB serial port (default: COM5)")
                transport.add_argument("--host", help="update over local Wi-Fi instead of USB (for example 192.168.4.1)")
                command.add_argument("--baud", type=int, default=460800, help="flash baud rate (default: 460800)")
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def release_checksums() -> dict[str, str]:
    if not CHECKSUMS_FILE.is_file():
        raise FileNotFoundError(f"Missing release checksum file: {CHECKSUMS_FILE}")
    checksums = json.loads(CHECKSUMS_FILE.read_text(encoding="utf-8"))
    if not isinstance(checksums, dict):
        raise ValueError(f"Invalid checksum file: {CHECKSUMS_FILE}")
    return checksums


def verified_release_file(name: str) -> Path:
    path = RELEASE_DIR / name
    if not path.is_file() or path.stat().st_size == 0:
        raise FileNotFoundError(f"Missing release file: {path}")
    expected = release_checksums().get(name)
    actual = sha256(path)
    if expected != actual:
        raise ValueError(f"Checksum mismatch for {path.name}; rebuild the release before flashing")
    return path


def master_cloud_settings() -> dict[str, str]:
    settings = {}
    # Mirror the C++ defaults: optional local definitions override tracked values.
    for path in (MASTER_CLOUD_CONFIG, MASTER_LOCAL_CONFIG):
        if not path.is_file():
            continue
        for name, value in re.findall(
                r'^\s*#define\s+(TIMIAC_CLOUD_(?:URL|DEVICE_ID|DEVICE_KEY))\s+("(?:[^"\\]|\\.)*")',
                path.read_text(encoding="utf-8"), re.MULTILINE):
            settings[name] = json.loads(value)
    return settings


def verify_master_cloud(image: Path) -> str | None:
    settings = master_cloud_settings()
    if not any(settings.values()):
        return None
    fields = ("TIMIAC_CLOUD_URL", "TIMIAC_CLOUD_DEVICE_ID", "TIMIAC_CLOUD_DEVICE_KEY")
    if any(not settings.get(name) for name in fields):
        raise ValueError("Cloud pairing configuration is incomplete; check ESP_Timiac/include/cloud_config.h")
    if not settings["TIMIAC_CLOUD_URL"].startswith("https://"):
        raise ValueError("The cloud relay URL must use HTTPS")
    firmware = image.read_bytes()
    from tools.machine_config import load
    _, identity = load()
    if identity.encode() not in firmware:
        raise ValueError("Release master does not contain the current machine identity; run python build.py master_release")
    if any(settings[name].encode() not in firmware for name in fields):
        raise ValueError("Release master does not match the cloud pairing settings; run python build.py master_release")
    return settings["TIMIAC_CLOUD_URL"]


def verify_release() -> str:
    """Do not let a valid checksum disguise a mixture of old and new firmware."""
    from tools.machine_config import load
    _, identity = load()
    machine_path = RELEASE_DIR / "machine.json"
    if not machine_path.is_file() or json.loads(machine_path.read_text()).get("identity") != identity:
        raise ValueError("Release machine configuration is missing or stale; run python build.py all")
    for name in ("firmware.bin", "esp_board1.bin", "esp_board2.bin", "esp_master.bin", "esp_master_release.bin"):
        image = verified_release_file(name)
        if identity.encode() not in image.read_bytes():
            raise ValueError(f"{name} does not contain {identity}; rebuild all targets before flashing")
    for role in ("master", "master_release"):
        layout_path = RELEASE_DIR / f"{role}_flash.json"
        layout = json.loads(layout_path.read_text())
        expected = {
            "0x1000": f"{role}_bootloader.bin", "0x8000": f"{role}_partitions.bin",
            "0xe000": f"{role}_boot_app0.bin", "0x10000": f"esp_{role}.bin",
            "0x290000": f"{role}_littlefs.bin",
        }
        actual = {entry["offset"].lower(): entry["file"] for entry in layout["images"]}
        if layout.get("chip") != "esp32" or actual != expected or len(layout["images"]) != len(expected):
            raise ValueError(f"Unexpected ESP32 flash layout: {layout_path}")
        for name in expected.values():
            verified_release_file(name)
    print(f"Verified {identity}: microswitch Octopus, shared bridges, dev/release masters and web images.")
    cloud_url = verify_master_cloud(verified_release_file("esp_master_release.bin"))
    if cloud_url:
        print(f"Verified paired cloud release: master_release uses {cloud_url}")
    return identity


def ensure_esptool() -> None:
    if importlib.util.find_spec("esptool") is not None:
        return
    print("esptool is missing; installing it for this Python interpreter...")
    command = [sys.executable, "-m", "pip", "install"]
    if sys.prefix == sys.base_prefix:
        command.append("--user")
    command.append("esptool")
    subprocess.run(command, check=True)


def flash_esp8266(image_name: str, port: str, baud: int) -> None:
    image = verified_release_file(image_name)
    ensure_esptool()
    print(f"Flashing {image.name} to ESP-12S on {port}...")
    subprocess.run(
        [
            sys.executable, "-m", "esptool", "--chip", "esp8266", "--port", port,
            "--baud", str(baud), "write-flash", "0x00000", str(image),
        ],
        check=True,
    )


def flash_master(port: str, baud: int, release: bool = False) -> None:
    manifest_path = RELEASE_DIR / ("master_release_flash.json" if release else "master_flash.json")
    if not manifest_path.is_file():
        raise FileNotFoundError(f"Missing master flash manifest: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    images = manifest.get("images")
    if manifest.get("chip") != "esp32" or not isinstance(images, list) or not images:
        raise ValueError(f"Invalid master flash manifest: {manifest_path}")
    role = "master_release" if release else "master"
    expected = {
        "0x1000": f"{role}_bootloader.bin", "0x8000": f"{role}_partitions.bin",
        "0xe000": f"{role}_boot_app0.bin", "0x10000": f"esp_{role}.bin",
        "0x290000": f"{role}_littlefs.bin",
    }
    actual = {image.get("offset", "").lower(): image.get("file") for image in images}
    if actual != expected or len(images) != len(expected):
        raise ValueError(f"Unexpected ESP32 flash layout: {manifest_path}")

    command = [
        sys.executable, "-m", "esptool", "--chip", "esp32", "--port", port,
        "--baud", str(baud), "write-flash",
    ]
    for image in images:
        offset, name = image.get("offset"), image.get("file")
        if not isinstance(offset, str) or not re.fullmatch(r"0x[0-9a-fA-F]+", offset):
            raise ValueError(f"Invalid flash offset in {manifest_path}: {offset!r}")
        if not isinstance(name, str) or Path(name).name != name:
            raise ValueError(f"Invalid image name in {manifest_path}: {name!r}")
        command.extend((offset, str(verified_release_file(name))))

    cloud_url = verify_master_cloud(verified_release_file("esp_master_release.bin")) if release else None
    ensure_esptool()
    label = "cloud-enabled release" if cloud_url else "user release" if release else "development"
    print(f"Flashing {label} master ESP and web filesystem on {port}...")
    subprocess.run(command, check=True)
    if cloud_url:
        print(f"Finished. Connect the master to its Wi-Fi router and open {cloud_url}/")


def windows_drive_type(root: Path) -> int | None:
    if os.name != "nt":
        return None
    return int(ctypes.windll.kernel32.GetDriveTypeW(str(root)))


def windows_filesystem(root: Path) -> str | None:
    if os.name != "nt":
        return None
    filesystem = ctypes.create_unicode_buffer(32)
    ok = ctypes.windll.kernel32.GetVolumeInformationW(
        str(root), None, 0, None, None, None, filesystem, len(filesystem)
    )
    return filesystem.value.upper() if ok else None


def validate_sd_root(root: Path, allow_fixed_drive: bool) -> Path:
    root = root.resolve()
    if not root.exists() or not root.is_dir():
        raise FileNotFoundError(f"microSD root does not exist: {root}")
    if root != Path(root.anchor):
        raise ValueError(f"Refusing non-root copy target: {root}")

    drive_type = windows_drive_type(root)
    if drive_type is not None and drive_type != DRIVE_REMOVABLE:
        if not allow_fixed_drive:
            raise ValueError(
                f"Refusing non-removable drive {root}. If this is the microSD reader, "
                "repeat with --allow-fixed-drive."
            )
        print(f"Warning: Windows reports {root} as fixed; explicit override accepted.")

    filesystem = windows_filesystem(root)
    if filesystem and filesystem not in SUPPORTED_FILESYSTEMS:
        raise ValueError(f"Unsupported microSD filesystem {filesystem!r} on {root}; use FAT32.")
    return root


def backup_existing(path: Path) -> None:
    if not path.exists():
        return
    backup_dir = path.parent / "TIMIAC_BAK"
    backup_dir.mkdir(exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    backup = backup_dir / f"{path.stem}-{stamp}{path.suffix}"
    shutil.copy2(path, backup)
    print(f"Backed up {path.name} to {backup}")


def verified_copy(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise FileNotFoundError(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    backup_existing(destination)
    temporary = destination.with_name(f".{destination.name}.tmp")
    try:
        shutil.copy2(source, temporary)
        if sha256(source) != sha256(temporary):
            raise OSError(f"Verification failed while copying {source.name}")
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"Copied and verified {destination}")


def prepare_octopus_card(sd_root: Path, allow_fixed_drive: bool) -> None:
    root = validate_sd_root(sd_root, allow_fixed_drive)
    firmware = verified_release_file("firmware.bin")
    print("Homing profile: MICROSWITCH (dev and release).")
    verified_copy(firmware, root / "firmware.bin")

    print("Done. Insert the card into a powered-off Octopus, then power it on.")
    print("The board should rename firmware.bin to FIRMWARE.CUR after a successful flash.")
    print("After confirming the flash, power off and leave the card installed for future remote firmware updates.")
    print("The master still stores motion programs; the Octopus card now also stores verified remote firmware updates.")


def main() -> int:
    args = parse_args()
    verify_release()
    if args.command == "verify":
        return 0
    if getattr(args, 'host', None):
        from local_update import install_targets
        install_targets(args.host, ['master', 'dashboard'] if args.role == 'master' else [args.role], args.mode)
        return 0
    if args.role == "esp1":
        flash_esp8266("esp_board1.bin", args.port, args.baud)
    elif args.role == "esp2":
        flash_esp8266("esp_board2.bin", args.port, args.baud)
    elif args.role == "master":
        flash_master(args.port, args.baud, release=args.mode == "release")
    else:
        prepare_octopus_card(args.sd, args.allow_fixed_drive)
    return 0


def cli() -> int:
    try:
        return main()
    except (FileNotFoundError, OSError, ValueError, RuntimeError, json.JSONDecodeError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(cli())
