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

## Update the master

From the project folder, run the usual command:

```powershell
python run.py master_release --port COM10
```

This automatically selects `CloudGateway/.local/firmware/`, verifies every
image, machine identity and pairing key, and flashes the application and local
dashboard together. It preserves the saved router settings. Only the master
needs this update; use its actual USB port if it is not COM10.

The paired package is private and excluded from Git. Include that folder and
`CloudGateway/scripts/flash_cloud_master.py` when transferring the project to
the clock's operator. A configured installation refuses to flash if the paired
package is missing or invalid. A clone without pairing uses the shared local
release until provisioned.

To rebuild the paired release on this provisioned PC:

```powershell
python build.py master_release
python run.py verify
```

The build exports the paired image into `.local/firmware/` and refreshes
`.local/Timiac-cloud-master.zip`. It keeps device secrets out of the shared
`firmware/two-motor/` folder. `ESP_Timiac/include/config.local.h` must supply the
cloud URL, device ID, device key and `kCloudflareRootCa` certificate bundle from
`ESP_Timiac/src/cloudflare_certificate.h`. The matching key is kept in
`.local/cloudflare-secrets.json` and installed as a Worker secret.

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
