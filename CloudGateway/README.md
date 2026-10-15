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
the dashboard login. The web panel also accepts the individual `.bin` files.
Apply `cloudflare/schema.sql` to D1 before deploying this gateway revision.
Images are private, immutable and SHA-256 identified; no R2 bucket or new billing
account is needed. The update guide describes limits and recovery.

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

To update the administrator password, authenticate `gcloud` for the configured
Firebase project and run `python scripts/provision_firebase.py`. It prompts for
the new password without echoing it. A fresh Firebase project needs
Authentication > Get started on the Spark plan first. The old `server.js` and
Dockerfile describe the retired Cloud Run implementation and are not used by
the deployed Worker.

## Online status

The public page remains available when the ESP32 is unplugged. After 30 seconds
without a hardware status, it reports **Master is offline** and disables
commands. After flashing, connect the master to its internet router; its first
status should arrive after internet time and TLS are ready.

If it stays offline, inspect the local dashboard at `http://192.168.4.1` and
`/api/status` for relay errors. The public page does not simulate physical
movement. Commands expire after 60 seconds and belong to one master boot
session, preventing delayed movement after reconnecting or rebooting.
