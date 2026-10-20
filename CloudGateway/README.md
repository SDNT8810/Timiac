# Timiac remote hardware dashboard

The hardware dashboard is **https://control.timiac.it/**. Cloudflare Workers
serves the page and D1 stores hardware status and queued commands. The simulator
at `https://timiac.it/` runs separately. The master makes outbound, verified HTTPS
requests every five seconds; router port forwarding is unnecessary.

Sign in as **Andrea**, using the password supplied separately. Firebase Spark
handles password authentication without enabling Google Cloud billing. This
uses username/password login, with no Gmail sign-in or OAuth redirect settings.
The Worker accepts only the configured Firebase UID, email and password login
provider. The ESP32 uses a separate private device key.

## Remote firmware updates

The **Firmware updates** panel now updates all three ESPs and both Octopus boards.
Install the [initial five-board receiver package](../Docs/remote-updates.md) once,
and leave a writable FAT32 microSD card in each Octopus. The master needs internet;
the bridges use its local Wi-Fi and existing UART wires. The factory bootloaders
are retained. Updates pause the clock and keep it parked until Home / Start clock.

From another PC, `python remote_update.py --target all` in the project root
verifies and uploads the tracked binaries, then installs them one at a time using
the dashboard login. In the web panel, **Update one component from GitHub**
requires only a target selection. It automatically prepares/reuses the verified
build for the latest main commit and installs exactly that component; no `.bin`
file is required. Full update still deliberately installs all six components.
Manual binary uploads remain available through the existing authenticated API
and PC tools.
Apply `cloudflare/schema.sql` for a new D1 database. For an existing gateway,
apply `cloudflare/component-update.sql` once before deploying this revision.
Images are private, immutable and SHA-256 identified; no R2 bucket or new billing
account is needed. The update guide describes limits and recovery.

Full update prepares verified firmware automatically on each push to main.
Its GitHub workflow reuses completed bundles only for the exact requested
commit, verifies them again before upload, and rebuilds when the cache is
missing or invalid. Preparation jobs never contact or update hardware. See
[faster build preparation](../Docs/remote-updates.md#faster-build-preparation--2026-10-20).

The single-component request is `POST /api/devices/DEVICE/component-update`
with `{ "target": "esp1", "kind": "app" }` using the signed-in administrator
and same-origin checks. The selection is stored durably with its source SHA;
the GitHub workflow receives `component_target` / `component_kind`. The publisher
verifies the cached complete bundle before uploading only the requested image.
The gateway rejects other components and creates exactly one installation step.
The normal Stop, progress expiry, image/identity validation and exact completion
receipts still apply. Reloading the page resumes progress; failures do not replay
installation. A missing exact-commit cache needs compilation first.

## Update the master

From the project folder, run the usual command:

```powershell
python run.py master_release --port COM10
```

This selects the included `firmware/two-motor/` release, verifies every
image, machine identity and pairing key, and flashes the application and local
dashboard together. It preserves the saved router settings. Use its actual USB
port if it is not COM10. Enabling remote updates initially requires both bridge
and Octopus updates too, as described above.

The pairing defaults in `ESP_Timiac/include/cloud_config.h` and the paired
release are intentionally tracked in Git. A fresh clone or pull can flash the
master without copying any credentials. Cloudflare and Google account access
are unnecessary for flashing. Invalid hashes or mismatched pairing settings
stop the updater before it writes to the board.

To rebuild the paired release on any PC with the project requirements installed:

```powershell
python build.py master_release
python run.py verify
```

The build updates `firmware/two-motor/` and its checksums. The tracked
`cloud_config.h` supplies the cloud URL, device ID, device key and trusted CA
bundle. Optional `config.local.h` definitions can override these for another
clock. The deployed Worker's `TIMIAC_DEVICE_KEY` must match this device key.

Repository readers can impersonate hardware status using the included key;
keep the repository private when sharing with operators. Account-wide
Cloudflare keys, Google login tokens and the private gateway secret file remain
excluded from Git.

## Maintain the deployed gateway

Use Node.js 22.13 or newer. From this directory:

```powershell
npm ci
npm run check
npm test
npm run deploy
```

`wrangler.jsonc` defines the domain, D1 database, Firebase project and exact
administrator identity. Authenticate Wrangler with the Cloudflare account that
owns `timiac.it`. The deployed Worker requires secrets `TIMIAC_DEVICE_KEY` and
`FIREBASE_API_KEY`; use `wrangler secret put` or the ignored private secrets file
with `wrangler secret bulk .local/cloudflare-secrets.json`. Never commit it.
Initialize a new D1 database with `cloudflare/schema.sql` before deploying.
For an existing database from before firmware 2026-10-17.1, apply
`cloudflare/ota-progress.sql` once and `cloudflare/diagnostics-schema.sql` before
deploying. These add progress leases and the persistent diagnostic archive.
For the file-free component updater, apply this one-time additive migration:

```powershell
npx wrangler d1 execute timiac-control --remote --file cloudflare/component-update.sql
```

It adds `selected_target` and `selected_kind`; existing Full update records keep
empty selections and retain their complete-bundle behavior. Push the updated
workflow/publisher to main before deploying the gateway so GitHub accepts the
new optional workflow inputs.
Diagnostic downloads require the normal signed-in administrator session and
remain available while hardware is offline. See [recovery instructions](../Docs/ota-recovery.md).

To update the administrator password, authenticate `gcloud` for the configured
Firebase project and run `python scripts/provision_firebase.py`. It prompts for
the new password without echoing it. A fresh Firebase project needs
Authentication > Get started on the Spark plan first. The old `server.js` and
Dockerfile describe the retired Cloud Run implementation and are not used by
the deployed Worker.

## Online status

The Hand sensor panel requires master firmware **2026-10-20.4** or later. It
shows GPIO34 presence, ADC counts/voltage, whether the gesture is armed and
the current reason motion is blocked. A zero-count input is labeled below
the ADC floor rather than displayed as an exact zero voltage.

Last sensor Struggle and last hand event retain the master's local RTC time
(or an explicitly labeled age if RTC time is invalid), accepted/ignored
counts and the reason a hand arrival did not start Struggle. A sensor-triggered
program ID links the last accepted event to its running/completed/faulted state;
manually requested Struggles do not increment the sensor counter. Event history
is retained between five-second reports, so a hand can be withdrawn before the
next cloud poll. Counters and the panel history reset with the master. Diagnostic
downloads retain bounded persistent `Hand sensor:` events across restarts.

Ignored arrivals are debounced for 200 ms and counted once per held hand; a
500 ms clear interval permits another event. Identical ignored-event log writes
are limited to one per 30 seconds. Live history still updates for each event.
ADC samples themselves are not written to flash. Offline masters or failed
browser refreshes clear live presence/arming while keeping last-report history
explicitly labeled. Viewing this panel never queues motion or flash commands.

The public page remains available when the ESP32 is unplugged. After 30 seconds
without a hardware status, it reports **Master is offline** and disables
commands. After flashing, connect the master to its internet router; its first
status should arrive after internet time and TLS are ready.

If it stays offline, inspect the local dashboard at `http://192.168.4.1` and
`/api/status` for relay errors. The public page does not simulate physical
movement. Commands expire after 60 seconds and belong to one master boot
session, preventing delayed movement after reconnecting or rebooting.
