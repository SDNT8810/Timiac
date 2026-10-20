"""Publish freshly built firmware to the pending dashboard Full update request.

Used by GitHub Actions, after every build succeeds. Never starts an unrelated
update, and never transmits a bootloader or partition table over OTA.
"""
import argparse
import hashlib
import json
import os
import re
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
    images, identities, revisions = [], set(), set()
    for target, kind, filename in IMAGES:
        data = (OUTPUT / filename).read_bytes()
        sha = hashlib.sha256(data).hexdigest()
        expected = manifest[filename] if TIMIAC else manifest['images'][filename]['sha256']
        if sha != expected:
            raise ValueError('Firmware checksum mismatch: ' + filename)
        marker = 'octopus' if kind == 'octopus' else 'master-filesystem' if kind == 'filesystem' else target
        if (PROJECT.upper() + '_OTA_ROLE:' + marker).encode() not in data:
            raise ValueError('Wrong firmware role: ' + filename)
        revision = re.search(rb'20\d\d-\d\d-\d\d\.\d{1,3}(?!\d)', data)
        identity = re.search((PROJECT.upper() + r'[12]-[0-9a-f]{12}').encode(), data)
        if not revision or not identity:
            raise ValueError('Missing firmware revision or hardware identity: ' + filename)
        revisions.add(revision[0]); identities.add(identity[0])
        images.append((target, kind, filename, data, sha))
    if len(revisions) != 1 or len(identities) != 1:
        raise ValueError('All firmware must belong to the same release and hardware identity')
    return images


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gateway')
    parser.add_argument('--update-id')
    parser.add_argument('--source-sha')
    parser.add_argument('--target', default='', help='publish only this selected component (empty publishes the full bundle)')
    parser.add_argument('--kind', default='', help='selected component image kind')
    parser.add_argument('--check-only', action='store_true', help='verify the complete bundle locally without uploading or updating hardware')
    parser.add_argument('--failed', action='store_true')
    args = parser.parse_args()
    if (args.target or args.kind) and not any((target, kind) == (args.target, args.kind) for target, kind, _ in IMAGES):
        parser.error('Select a valid component --target and --kind together')
    if args.check_only:
        if args.failed:
            parser.error('--check-only cannot be combined with --failed')
        images = checked_images()
        print('Verified complete bundle:', len(images), 'components; no upload or hardware update requested')
        return
    if not all((args.gateway, args.update_id, args.source_sha)):
        parser.error('--gateway, --update-id and --source-sha are required for publishing')
    allowed = 'https://control.timiac.it' if TIMIAC else 'https://lumiac.control.timiac.it'
    if args.gateway != allowed:
        parser.error('Use the configured HTTPS hardware gateway')
    if args.failed:
        call(args, 'failed', {'error': 'Firmware build or bundle validation failed. Open the GitHub Actions build log for details.'})
        return
    images = checked_images()  # Check ALL artifacts before sending the first one.
    if args.target:
        images = [image for image in images if image[:2] == (args.target, args.kind)]
    for target, kind, filename, data, sha in images:
        result = call(args, 'images', data, {'X-' + PROJECT + '-Target': target, 'X-' + PROJECT + '-Kind': kind})
        if result.get('sha256') != sha:
            raise ValueError('Uploaded firmware checksum mismatch: ' + filename)
        print('Verified upload:', target, kind, filename, flush=True)
    result = call(args, 'complete', {})
    print('Full update:', result['state'], flush=True)


if __name__ == '__main__':
    main()
