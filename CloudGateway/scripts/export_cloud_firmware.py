"""Package a paired master privately; never replace the shared release images."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[2]
local = ROOT / 'CloudGateway/.local'
build = ROOT / 'ESP_Timiac/.pio/build/timiac_master_release'
destination = local / 'firmware'
destination.mkdir(parents=True, exist_ok=True)
manifest = json.loads((build / 'master_flash.json').read_text())
device_key = json.loads((local / 'cloudflare-secrets.json').read_text())['TIMIAC_DEVICE_KEY']
firmware = (build / 'firmware.bin').read_bytes()
if device_key.encode() not in firmware or b'https://control.timiac.it' not in firmware:
    raise ValueError('This firmware is not paired with the deployed cloud relay')
partitions = (build / 'partitions.bin').read_bytes()
filesystem = None
for offset in range(0, len(partitions) - 31, 32):
    magic, kind, subtype, start, size, label, flags = struct.unpack('<HBBII16sI', partitions[offset:offset + 32])
    if magic == 0x50AA and kind == 1 and subtype == 0x82:
        filesystem = (start, size)
        break
if not filesystem or (build / 'littlefs.bin').stat().st_size > filesystem[1]:
    raise ValueError('Filesystem does not fit the actual ESP32 partition table')
for image in manifest['images']:
    source = build / ('firmware.bin' if image['file'] == 'esp_master.bin' else image['file'])
    shutil.copy2(source, destination / image['file'])
manifest['images'].append({'offset': hex(filesystem[0]), 'file': 'littlefs.bin'})
shutil.copy2(build / 'littlefs.bin', destination / 'littlefs.bin')
identity = re.search(r'machineId\[\] = "([^"]+)"', (ROOT / 'shared/machine_generated.h').read_text())
if not identity:
    raise ValueError('Machine identity is unavailable')
manifest['machine_id'] = identity.group(1)
manifest['dashboard'] = 'release'
manifest['remote_url'] = 'https://control.timiac.it'
for image in manifest['images']:
    image['sha256'] = hashlib.sha256((destination / image['file']).read_bytes()).hexdigest()
(destination / 'master_flash.json').write_text(json.dumps(manifest, indent=2) + '\n')
shutil.copy2(ROOT / 'CloudGateway/scripts/flash_cloud_master.py', destination / 'flash_cloud_master.py')
instructions = """# Timiac remote master update

1. Unzip this folder on the Windows PC connected to the clock.
2. Connect only the master ESP32 by USB. Open Device Manager > Ports and note its COM number.
3. Open a terminal in this folder and run:
   python flash_cloud_master.py --port COM5
   Replace COM5 with the actual USB port. Python must be installed.
4. If it cannot connect, hold BOOT on the ESP32 while it starts connecting, then release BOOT.
5. Wait for Finished. Reconnect the master to its Wi-Fi router.
6. Open https://control.timiac.it/ and sign in with the account supplied separately.
   The first online status should arrive within about 30 seconds once internet time is synchronized.

Only the master needs this update. Do not erase all flash; this updater preserves the saved Wi-Fi settings.
Home still holds the arms at Home until the next command. Current stays at 1000 mA RMS.
When unplugged, the web page stays available and reports Master is offline within 30 seconds.

This folder contains the clock's private pairing key inside the firmware. Share it only with the clock's operator.
"""
(destination / 'instruction.md').write_text(instructions)
with zipfile.ZipFile(local / 'Timiac-cloud-master.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for file in destination.iterdir():
        if file.is_file():
            archive.write(file, file.name)
print('Private master update exported: CloudGateway/.local/Timiac-cloud-master.zip')
