# Timiac remote gateway

This Cloud Run service gives `timiac.it` a remote release dashboard without
opening a port on the router. The master makes an outbound, certificate-checked
HTTPS request every five seconds. Cloud Run stores the last status and queues
Home, Open, Close, Show time, Stop, and Set time requests until the master is
idle enough to accept them.

The public URL is protected twice: Google ID tokens are verified by the service,
and only email addresses in `TIMIAC_ADMIN_EMAILS` are allowed. The device uses a
separate secret in `X-Timiac-Device-Key`; the browser never receives it.

## Credentials and configuration

Before deployment, provide:

1. A Google Cloud project where the deployment account can enable APIs, create
   Firestore in Native mode, deploy Cloud Run, create Secret Manager secrets,
   and grant the Cloud Run runtime service account `roles/datastore.user`. A
   Project Owner role covers those operations. The Google OAuth web client
   needs the authorized JavaScript origin `https://timiac.it`.
2. The OAuth client ID, allowed administrator Gmail addresses, a random device
   ID, and a randomly generated device key of at least 32 random bytes.
3. The Cloudflare API token/account details and the domain zone for `timiac.it`.

The browser uses Google Identity Services and submits a Google ID token to the
gateway. It does not use an OAuth redirect flow, so the callback URL and OAuth
client secret are not used by this project. They may remain in Google Cloud,
but do not put the client secret or an application `AUTH_SECRET` in this
repository or the Cloud Run configuration.

`config.local.h` on the master needs this shape:

```cpp
#define TIMIAC_CLOUD_URL "https://YOUR-CLOUD-RUN-URL"
#define TIMIAC_CLOUD_DEVICE_ID "clock-01"
#define TIMIAC_CLOUD_DEVICE_KEY "a-long-random-secret"
```

The remote relay is disabled unless all three settings are present. It uses the
built-in Google Trust root for a standard Cloud Run endpoint and never uses an
insecure TLS connection. `TIMIAC_CLOUD_CA_CERT` remains available only if a
future deployment uses another trusted origin.

## Deploy to Cloud Run

From this directory, after authenticating `gcloud`, enable Cloud Run, Cloud
Build, Artifact Registry, and Firestore. Give the Cloud Run runtime service
account the `roles/datastore.user` role, then deploy from source:

```powershell
gcloud run deploy timiac-gateway --source . --region europe-west8 --allow-unauthenticated --set-env-vars GOOGLE_OAUTH_CLIENT_ID=YOUR_CLIENT_ID,TIMIAC_DEVICE_ID=clock-01,TIMIAC_ADMIN_EMAILS=admin1@gmail.com,admin2@gmail.com --set-secrets TIMIAC_DEVICE_KEY=timiac-device-key:latest
```

The service must allow unauthenticated invocation so that the master can make
its outbound request; application code verifies both the device key and each
Google ID token. Store the device key in Secret Manager and do not put it in a
command history or source file.

Deploy the Worker in `cloudflare/worker.js` with `ORIGIN_URL` set to the Cloud
Run URL, and route it to `timiac.it/*`. The worker keeps the Cloud Run host
header for the origin while Cloudflare terminates public TLS for `timiac.it`.
Cloudflare Access with Google can be added as another login gate, but the
gateway's email allow-list remains the enforcement point.

After setting `config.local.h`, rebuild and flash the selected master image.
Flash only the master for this remote feature; the bridges and Octopus images do
not need a change. The dashboard reports the relay state under `/api/status`.
