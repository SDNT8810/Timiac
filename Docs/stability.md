# Clock stability — release 2026-10-16.1

## Allocation failures and bridge disconnects - 2026-10-19.4

The October 19 diagnostic export contains `Bridge packet rejected: NoMemory`
and `SSL - Memory allocation failed`. The decoder closed the bridge connection
on that allocation failure, which could stop an otherwise functioning clock.
Release 2026-10-19.4 reserves that parser pool, removes a cloud payload copy,
releases snapshots before TLS starts, and serializes cloud/OTA TLS sessions.
Read [the evidence, limits and local wireless installation](memory-failure-2026-10-19.md).

## Cloud connection recovery - 2026-10-19.1

The local status from October 19 confirmed firmware 2026-10-17.1 on all three
ESPs, healthy calibrated motor boards, router address 192.168.1.4 and synchronized
internet time. Its six cloud attempts failed with HTTPClient -1, most recently
after 4,444 ms. In this SDK, -1 means a transport connection failure before an
HTTP response; it does not establish whether TCP, certificate verification or
the TLS deadline failed. The firmware previously hid the underlying TLS error.

The master now gives TLS 20 seconds and TCP/HTTP eight seconds in its background
worker. HTTP/1.0 avoids chunked response framing when parsing the raw stream.
Negative HTTPClient codes include the SDK description and any TLS error. Error
changes and successful reconnection are saved in the NVS diagnostic journal.
The local Wi-Fi section displays the cloud connection/error separately.
Certificate verification remains enabled; motor control continues while the
worker waits. The longer limits address a plausible failure path, but the next
physical connection result is needed to confirm the cause.

Install only the master for this connection change, with its actual USB port:

```powershell
git pull
python run.py master_release --port COM5
```

Allow one minute for Wi-Fi, time synchronization and retries. If the remote
panel remains offline, copy the **Remote dashboard** message under **Router /
hotspot Wi-Fi**, or run `(Invoke-RestMethod http://192.168.4.1/api/status).cloud | ConvertTo-Json`.

## Earlier clock-control fixes

This release fixes two software paths that can stop an otherwise healthy clock:

- Cloud HTTPS previously ran inside the coordinator loop and its runtime lock. The installed ESP32 SDK permits a TLS handshake to wait 120 seconds, while the motor bridges require a heartbeat within 15 seconds. Cloud traffic now runs in a separate task with one bounded request mailbox and one reply mailbox. Only the coordinator applies commands. TLS has an explicit three-second handshake limit, and failed requests back off to sixty seconds. The clock and motor links continue while cloud requests wait or fail.
- An interrupted idle M122/M119 diagnostic previously discarded calibrated positions. A previously healthy idle board now fences late replies with a unique M118 marker, re-identifies the Octopus, and requires a fresh complete passing driver report before moving again. Positions remain valid through that read-only recovery. If recovery times out, a board actually restarts, a driver genuinely fails, or a motion command fails, the existing stop and position interlocks remain in force. Movement is never replayed.

The master loop also has a 12 KB stack, checked against the linked animation paths (including functions inlined by the compiler), with at least 2 KB reserved for library calls.

The master retains the last stop reason and count in NVS. Calibration, successful reconnection, and a master reboot no longer erase that history. Maintenance in the local release dashboard and remote dashboard shows **Last stop**. Status also includes master uptime, reset reason, free/minimum heap, and cloud worker connection/stack diagnostics.

The post-recovery live status inspected on October 16 showed both boards healthy, 1000 mA settings, and no active temperature warnings. The earlier fault had already been cleared, so that snapshot cannot prove which event caused the previous interruption or rule out earlier overheating.

Validation includes a production-task test that holds HTTPS blocked while the real coordinator completes a simulated day of clock poses/hourly animations, and a 48-hour diagnostic soak with 357 interrupted reports. These are accelerated host tests, not a physical 48-hour endurance certification. Thermal protection, Stop, homing timeout, and actual-reset interlocks are still tested.

Install using **Full update** at https://control.timiac.it/ after this release is pushed. Keep power and both Octopus microSD cards connected. After completion, use **Calibrate arms**, then **Show time** to resume the automatic clock. Opening/closing the arms, manual calibration, Stop, and firmware updates continue to pause the clock intentionally.
