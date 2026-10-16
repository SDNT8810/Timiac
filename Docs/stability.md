# Clock stability — release 2026-10-16.1

This release fixes two software paths that can stop an otherwise healthy clock:

- Cloud HTTPS previously ran inside the coordinator loop and its runtime lock. The installed ESP32 SDK permits a TLS handshake to wait 120 seconds, while the motor bridges require a heartbeat within 15 seconds. Cloud traffic now runs in a separate task with one bounded request mailbox and one reply mailbox. Only the coordinator applies commands. TLS has an explicit three-second handshake limit, and failed requests back off to sixty seconds. The clock and motor links continue while cloud requests wait or fail.
- An interrupted idle M122/M119 diagnostic previously discarded calibrated positions. A previously healthy idle board now fences late replies with a unique M118 marker, re-identifies the Octopus, and requires a fresh complete passing driver report before moving again. Positions remain valid through that read-only recovery. If recovery times out, a board actually restarts, a driver genuinely fails, or a motion command fails, the existing stop and position interlocks remain in force. Movement is never replayed.

The master loop also has a 12 KB stack, checked against the linked animation paths (including functions inlined by the compiler), with at least 2 KB reserved for library calls.

The master retains the last stop reason and count in NVS. Calibration, successful reconnection, and a master reboot no longer erase that history. Maintenance in the local release dashboard and remote dashboard shows **Last stop**. Status also includes master uptime, reset reason, free/minimum heap, and cloud worker connection/stack diagnostics.

The post-recovery live status inspected on October 16 showed both boards healthy, 1000 mA settings, and no active temperature warnings. The earlier fault had already been cleared, so that snapshot cannot prove which event caused the previous interruption or rule out earlier overheating.

Validation includes a production-task test that holds HTTPS blocked while the real coordinator completes a simulated day of clock poses/hourly animations, and a 48-hour diagnostic soak with 357 interrupted reports. These are accelerated host tests, not a physical 48-hour endurance certification. Thermal protection, Stop, homing timeout, and actual-reset interlocks are still tested.

Install using **Full update** at https://control.timiac.it/ after this release is pushed. Keep power and both Octopus microSD cards connected. After completion, use **Calibrate arms**, then **Show time** to resume the automatic clock. Opening/closing the arms, manual calibration, Stop, and firmware updates continue to pause the clock intentionally.
