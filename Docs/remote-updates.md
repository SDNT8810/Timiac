# Remote updates for all five boards — 2026-10-15.5

After one initial installation, update the master ESP32, both ESP-12 bridges,
and both Octopus boards through **https://control.timiac.it/**. Only the master
needs internet; it forwards the other images over the clock's own Wi-Fi.
No PC beside the clock or extra control wires are needed for routine updates.

## Initial installation in Italy — once

Follow [instruction.md](../instruction.md) using this complete package:

1. Turn the system off. Update both Octopus cards with
   `python run.py octopus_release --sd I:\` (use each card's actual drive).
   Boot each board, confirm `firmware.bin` became `FIRMWARE.CUR`, then turn it off.
   **Reinsert the cards and leave them installed.** Use writable FAT32 cards.
2. Flash each ESP-12 separately through its 3.3 V programming adapter:
   `python run.py esp1_release --port COM5`, then
   `python run.py esp2_release --port COM5` (use the actual COM ports).
3. Flash the master with `python run.py master_release --port COM10`.
4. Reconnect the usual UART/power wires. Start the complete clock and connect
   the master to its internet router. Check both boards, the RTC and hand input.
5. Sign in at **https://control.timiac.it/**. Firmware updates should report ready.
   Perform the first remote update while someone is still beside the clock,
   and confirm Home and Start clock work afterward.

The existing ESP and Octopus bootloaders are used. Initial ESP flashing includes
the normal ESP bootloader/layout; the new application enables its update receiver.
The Octopus factory 32 KB SD bootloader is preserved. It installs a verified image
from the card after an automatic reset. Do not replace the Octopus bootloader or
change its BOOT jumper for this procedure.
An older Octopus application must receive the initial SD update before remote
updates can work; a bootloader change alone does not add this receiver.

## Routine update from Canada

On your PC, pull a complete, verified firmware package and run:

```powershell
git pull
python remote_update.py --target all
```

Enter the same **Andrea** dashboard password when prompted. The command checks
the local package, signs in over HTTPS, updates both bridges, both Octopus boards,
the master application, then its local dashboard, and waits for each result.
It stops on an error and never automatically retries a failed flashing job.

For one component use `--target esp1`, `esp2`, `octopus1`, `octopus2`, or `master`.
`master` includes its matching dashboard; `dashboard` updates that filesystem alone.
No Cloudflare token or account credentials are needed to perform a normal update.

You can also use **Firmware updates** on the remote dashboard. Choose the target,
select its file below and press **Install firmware**, one component at a time.

| Dashboard target | File in `firmware/two-motor/` |
|---|---|
| Master ESP32 — application | `esp_master_release.bin` |
| Master ESP32 — local dashboard | `master_release_littlefs.bin` |
| ESP-12 bridge 1 | `esp_board1.bin` |
| ESP-12 bridge 2 | `esp_board2.bin` |
| Octopus 1 or Octopus 2 | `firmware.bin` (same image for both) |

Wait for **complete** before the next update. Keep power connected. Updates pause
the clock, cancel queued motion, and wait for the connected bridges to acknowledge
Stop. The clock remains parked across an update reboot. When finished, select
**Home**, then **Show time / Start clock**. Starting the clock restores the normal
automatic startup behavior for subsequent power cycles.

Keep the mechanism clear and secure any load that relies on motor holding torque:
an Octopus reset temporarily disables its motor outputs.

## Verification and recovery

The cloud accepts only authenticated administrator uploads and checks file size,
board role, revision and image headers. Files are immutable and SHA-256 identified.
Downloads require the clock's device key over verified HTTPS. Bridge updates use
a per-boot challenge and HMAC-SHA256 covering the target, size, hash and job metadata.
Authorization headers carry an HMAC rather than a plaintext key. Paired firmware
images contain the existing shared device key; the local transfer uses the clock's
WPA2 Wi-Fi. This is not an independent public-key firmware-signing system.

Both the master and receiving bridge check SHA-256 before activation. Octopus
images are first staged on their bridge, then transferred in numbered, CRC-checked
blocks to `timiac.upd` on the microSD. Marlin checks the entire file and its SD
readback before renaming it to `firmware.bin` and resetting into the factory loader.
An incomplete upload stays under a non-boot filename. Command retries do not write
the same block twice. A later attempt first clears any abandoned staging session.

Only one update or motion command may be queued. Update requests expire if not
started promptly and belong to one master boot session. A delivered job is not
replayed after a disconnect. **Stop** cancels transfer/staging before activation;
once flash activation or the bootloader copy has started, let it finish.

Routine OTA preserves the partition table, bootloaders and saved router settings.
The master filesystem is a separate update; stopping it or losing power can damage those
local web files, but the cloud updater can replace them again if the app still runs.
ESP32 application slots retain the previous image during download. The stock ESP
bootloaders do not provide a tested automatic health rollback for a bad application.
USB/SD recovery can still be required after power loss during flash copying or if
an installed application fails to boot or connect to Wi-Fi. A Marlin fatal halt
still requires resetting that Octopus before it can process an update.

This update supports firmware for the existing machine identity and flash layout.
A package that changes the mechanical/hardware identity is rejected; install such
a matched hardware migration locally. Firmware bootloaders themselves are not
replaced remotely.

Bridge maintenance addresses are `192.168.4.241:3232` and `192.168.4.242:3232`,
outside the normal AP DHCP lease range. They have no direct internet service.
The existing tracked clock pairing key is also used for update authentication;
repository access therefore includes access to that pairing material.

All boards compile and automated transfer/authentication/failure tests are run.
An end-to-end update on the physical clock has not been performed here.

## Gateway maintenance

For this initial cloud deployment, apply the additional tables before publishing:

```powershell
cd CloudGateway
npx wrangler d1 execute timiac-control --remote --file cloudflare/schema.sql
npm run check
npm test
npm run deploy
```

Firmware is stored in 64 KiB chunks in the existing D1 database; no new cloud
service or billing account is required. The existing login protects upload and
installation endpoints. Unused artifacts and finished receipts older than a week
are pruned on upload. See [CloudGateway/README.md](../CloudGateway/README.md).

References: [ESP32 OTA partitions](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html),
[ESP8266 updater](https://arduino-esp8266.readthedocs.io/en/3.1.1/ota_updates/readme.html),
[BTT Octopus SD bootloader](https://github.com/bigtreetech/docs/blob/master/docs/Octopus.md),
[Cloudflare D1 limits](https://developers.cloudflare.com/d1/platform/limits/).
