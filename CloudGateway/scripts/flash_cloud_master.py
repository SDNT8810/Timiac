"""Flash the private, paired ESP32 image after verifying every file."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='Master ESP32 USB port, for example COM5')
    parser.add_argument('--check', action='store_true', help='Verify files without connecting to hardware')
    parser.add_argument('--directory', type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    directory = args.directory.resolve()
    manifest = json.loads((directory / 'master_flash.json').read_text())
    if manifest.get('chip') != 'esp32' or not manifest.get('images'):
        raise ValueError('Invalid ESP32 flash manifest')
    images = []
    regions = []
    for image in manifest['images']:
        name, offset = image['file'], image['offset']
        if Path(name).name != name or not re.fullmatch(r'0x[0-9a-fA-F]+', offset):
            raise ValueError('Invalid flash image path or offset')
        file = directory / name
        data = file.read_bytes()
        if hashlib.sha256(data).hexdigest() != image['sha256']:
            raise ValueError('File verification failed: ' + name)
        start = int(offset, 16)
        if start + len(data) > 4 * 1024 * 1024:
            raise ValueError('Image is outside the ESP32 flash: ' + name)
        regions.append((start, start + len(data)))
        images.extend([offset, str(file)])
    for left, right in zip(sorted(regions), sorted(regions)[1:]):
        if left[1] > right[0]:
            raise ValueError('Flash images overlap')
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
