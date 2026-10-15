"""Publish freshly built firmware to the pending dashboard Full update request.

Used by GitHub Actions, after every build succeeds. Never starts an unrelated
update, and never transmits a bootloader or partition table over OTA.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
TIMIAC = (ROOT / 'ESP_Timiac').is_dir()
PROJECT = 'Timiac' if TIMIAC else 'Lumiac'
IMAGES = [
    ('esp1', 'app', 'esp_board1.bin'), ('esp2', 'app', 'esp_board2.bin'),
    ('octopus1', 'octopus', 'firmware.bin'), ('octopus2', 'octopus', 'firmware.bin'),
    ('master', 'app', 'esp_master_release.bin'), ('master', 'filesystem', 'master_release_littlefs.bin'),
] if TIMIAC else [('octopus', 'octopus', 'octopus.bin'), ('esp12', 'app', 'esp12.bin'), ('esp32', 'app', 'esp32.bin')]
OUTPUT = ROOT / ('firmware/two-motor' if TIMIAC else 'firmware')


def call(args, endpoint, data, headers=None):
    binary = isinstance(data, bytes)
    request = urllib.request.Request(args.gateway.rstrip('/') + '/build/v1/full-updates/' + args.update_id + '/' + endpoint,
        data=data if binary else json.dumps(data).encode(), headers={
            'User-Agent': 'Clock-Firmware-Builder/1.0',
            'Content-Type': 'application/octet-stream' if binary else 'application/json',
            'X-Firmware-Build-Key': os.environ['FIRMWARE_BUILD_KEY'],
            'X-Firmware-Source': args.source_sha, **(headers or {}),
        })
    with urllib.request.urlopen(request, timeout=180) as response:
        return json.load(response)


def checked_images():
    manifest = json.loads((OUTPUT / ('SHA256.json' if TIMIAC else 'manifest.json')).read_text())
    images = []
    for target, kind, filename in IMAGES:
        data = (OUTPUT / filename).read_bytes()
        sha = hashlib.sha256(data).hexdigest()
        expected = manifest[filename] if TIMIAC else manifest['images'][filename]['sha256']
        if sha != expected:
            raise ValueError('Firmware checksum mismatch: ' + filename)
        images.append((target, kind, filename, data, sha))
    return images


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gateway', required=True)
    parser.add_argument('--update-id', required=True)
    parser.add_argument('--source-sha', required=True)
    parser.add_argument('--failed', action='store_true')
    args = parser.parse_args()
    allowed = 'https://control.timiac.it' if TIMIAC else 'https://lumiac.control.timiac.it'
    if args.gateway != allowed:
        parser.error('Use the configured HTTPS hardware gateway')
    if args.failed:
        call(args, 'failed', {'error': 'Firmware build or bundle validation failed. Open the GitHub Actions build log for details.'})
        return
    images = checked_images()  # Check ALL artifacts before sending the first one.
    for target, kind, filename, data, sha in images:
        result = call(args, 'images', data, {'X-' + PROJECT + '-Target': target, 'X-' + PROJECT + '-Kind': kind})
        if result.get('sha256') != sha:
            raise ValueError('Uploaded firmware checksum mismatch: ' + filename)
        print('Verified upload:', target, kind, filename, flush=True)
    result = call(args, 'complete', {})
    print('Full update:', result['state'], flush=True)


if __name__ == '__main__':
    main()
