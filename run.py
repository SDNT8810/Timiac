"""Flash the prepared Timiac release or prepare an Octopus microSD card.

Examples:
  python run.py esp1
  python run.py esp2
  python run.py master --port COM4
  python run.py master_release --port COM5
  python run.py octopus
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
GCODES_DIR = ROOT / "gcodes"
DEFAULT_ESP_PORT = "COM5"
DEFAULT_SD_ROOT = Path("E:/")

DRIVE_REMOVABLE = 2
SUPPORTED_FILESYSTEMS = {"FAT", "FAT32", "EXFAT"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("verify", help="verify every firmware image and the shared machine identity without flashing")

    for name, help_text in (
        ("esp1", "flash esp_board1.bin to bridge 1"),
        ("esp2", "flash esp_board2.bin to bridge 2"),
        ("master", "flash all master ESP images, including its web filesystem"),
        ("master_release", "flash the user dashboard and DS3231-enabled master"),
    ):
        command = commands.add_parser(name, help=help_text)
        command.add_argument("--port", default=DEFAULT_ESP_PORT, help="USB serial port (default: COM5)")
        command.add_argument("--baud", type=int, default=460800, help="flash baud rate (default: 460800)")

    octopus = commands.add_parser("octopus", help="copy firmware.bin and example G-code files to a microSD card")
    octopus.add_argument("--sd", type=Path, default=DEFAULT_SD_ROOT, help=r"microSD drive root (default: E:\\)")
    octopus.add_argument(
        "--allow-fixed-drive",
        action="store_true",
        help="allow the selected card reader when Windows reports it as fixed",
    )
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
    print(f"Verified {identity}: both Octopus boards, both ESP bridges, both master modes and web images.")
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

    ensure_esptool()
    print(f"Flashing {'user release' if release else 'development'} master ESP and web filesystem on {port}...")
    subprocess.run(command, check=True)


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
    verified_copy(firmware, root / "firmware.bin")

    if not GCODES_DIR.is_dir():
        raise FileNotFoundError(f"Missing example G-code directory: {GCODES_DIR}")
    gcode_files = sorted(path for path in GCODES_DIR.rglob("*") if path.is_file())
    if not gcode_files:
        raise FileNotFoundError(f"No G-code files found in {GCODES_DIR}")
    for source in gcode_files:
        verified_copy(source, root / "gcodes" / source.relative_to(GCODES_DIR))

    print("Done. Insert the card into a powered-off Octopus, then power it on.")
    print("The board should rename firmware.bin to FIRMWARE.CUR after a successful flash.")
    print("The copied G-code files are reference files; run them through the master web panel.")


def main() -> int:
    args = parse_args()
    verify_release()
    if args.command == "verify":
        return 0
    if args.command == "esp1":
        flash_esp8266("esp_board1.bin", args.port, args.baud)
    elif args.command == "esp2":
        flash_esp8266("esp_board2.bin", args.port, args.baud)
    elif args.command in ("master", "master_release"):
        flash_master(args.port, args.baud, release=args.command == "master_release")
    else:
        prepare_octopus_card(args.sd, args.allow_fixed_drive)
    return 0


def cli() -> int:
    try:
        return main()
    except (FileNotFoundError, OSError, ValueError, json.JSONDecodeError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(cli())
