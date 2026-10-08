#pragma once
#include "../src/cloudflare_certificate.h"

// Deployment pairing settings are intentionally tracked for this clock.
// Repository readers can impersonate its hardware status using this key.
#ifndef TIMIAC_CLOUD_URL
#define TIMIAC_CLOUD_URL "https://control.timiac.it"
#endif
#ifndef TIMIAC_CLOUD_DEVICE_ID
#define TIMIAC_CLOUD_DEVICE_ID "clock-01"
#endif
#ifndef TIMIAC_CLOUD_DEVICE_KEY
#define TIMIAC_CLOUD_DEVICE_KEY "KOf8Yt7QzgCS8kWt59HE-u0YYqfYJOvPO6qYe8OiF_g"
#endif
#ifndef TIMIAC_CLOUD_CA_CERT
#define TIMIAC_CLOUD_CA_CERT kCloudflareRootCa
#endif
