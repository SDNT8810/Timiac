"""Flash the private, paired ESP32 image after verifying every file."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys


def verified_images(directory: Path):
    """Validate the complete paired release before any serial write."""
    directory = directory.resolve()
    manifest = json.loads((directory / 'master_flash.json').read_text(encoding='utf-8'))
    expected = {
        '0x1000': 'master_bootloader.bin',
        '0x8000': 'master_partitions.bin',
        '0xe000': 'master_boot_app0.bin',
        '0x10000': 'esp_master.bin',
        '0x290000': 'littlefs.bin',
    }
    entries = manifest.get('images')
    if (manifest.get('chip') != 'esp32' or manifest.get('flash_size') != '4MB'
            or manifest.get('dashboard') != 'release' or not isinstance(entries, list)
            or len(entries) != len(expected)):
        raise ValueError('Invalid paired ESP32 release manifest')
    actual = {}
    images = []
    regions = []
    for entry in entries:
        name, offset = entry.get('file'), entry.get('offset')
        if (not isinstance(name, str) or Path(name).name != name
                or not isinstance(offset, str) or not re.fullmatch(r'0x[0-9a-fA-F]+', offset)):
            raise ValueError('Invalid flash image path or offset')
        actual[offset.lower()] = name
        file = directory / name
        data = file.read_bytes()
        if not data or hashlib.sha256(data).hexdigest() != entry.get('sha256'):
            raise ValueError('File verification failed: ' + name)
        start = int(offset, 16)
        if start + len(data) > 4 * 1024 * 1024:
            raise ValueError('Image is outside the ESP32 flash: ' + name)
        regions.append((start, start + len(data)))
        images.append((offset, file))
    if actual != expected:
        raise ValueError('Unexpected paired ESP32 flash layout')
    for left, right in zip(sorted(regions), sorted(regions)[1:]):
        if left[1] > right[0]:
            raise ValueError('Flash images overlap')
    identity, url = manifest.get('machine_id'), manifest.get('remote_url')
    firmware = (directory / 'esp_master.bin').read_bytes()
    if not isinstance(identity, str) or not re.fullmatch(r'TIMIAC2-[0-9a-f]{12}', identity) or identity.encode() not in firmware:
        raise ValueError('Paired firmware machine identity is missing or invalid')
    if url != 'https://control.timiac.it' or url.encode() not in firmware:
        raise ValueError('Paired firmware does not target https://control.timiac.it')
    return manifest, images


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='Master ESP32 USB port, for example COM5')
    parser.add_argument('--check', action='store_true', help='Verify files without connecting to hardware')
    parser.add_argument('--directory', type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    directory = args.directory.resolve()
    manifest, verified = verified_images(directory)
    images = [part for offset, file in verified for part in (offset, str(file))]
    print('Verified paired master firmware: ' + manifest['machine_id'])
    if args.check:
        return
    if not args.port:
        parser.error('Provide --port COM5, replacing COM5 with the master USB port')
    if importlib.util.find_spec('esptool') is None:
        subprocess.run([sys.executable, '-m', 'pip', 'install', 'esptool>=5,<6'], check=True)
    subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32', '--port', args.port,
                    '--baud', '460800', 'write-flash', *images], check=True)
    print('Finished. Reconnect the master to its router and open https://control.timiac.it/')


if __name__ == '__main__':
    main()
