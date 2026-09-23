# Three-ESP architecture

~~~text
Phone(s) -- Wi-Fi --> Master ESP32 (192.168.4.1)
                         |                |
                      TCP 8266         TCP 8266
                         |                |
                    ESP bridge 1     ESP bridge 2
                         | UART3          | UART3
                    Octopus 1 / X    Octopus 2 / X
                       X1 motor         X2 motor
~~~

Only the master creates the access point and HTTP server. Both bridges are
stations. Protocol v4 identifies board 1/2 explicitly with a shared pairing key,
boot identity, and master session. Connection order and IP address do not
determine axis routing. Duplicate board IDs are refused. The pairing key
separates bridge traffic from ordinary clients; it is not transport encryption.

## G-code and jobs

shared/gcode.h is the master parser and UART command validator.
It accepts X1 through C2 naming, with kAxisCount = 6 enabling all twelve axes.
The master removes the board suffix and routes each command to its bridge.
The bridge sends local X/Y/Z/A/B/C letters unchanged to the Octopus UART and
reads the same letters in M114 reports. Marlin uses I/J/K internally for the
last three drivers, with AXIS4/5/6_NAME explicitly set to A/B/C for G-code.
G90/G91 with targets are custom move shorthands. Source relative
coordinates are checked and converted to explicit absolute local G1 moves.

Native actuator zero is the fully open home position; maximum travel is closed.
Clock poses and simulator animations invert extension once on the master:
`position = travel × (1 − extension/100)`. Hardware example files contain this
conversion already. Neither bridges nor Marlin invert their commands again;
homing continues toward the existing minimum switches. Both master dashboards
share RTC/distance readout code, action endpoints, preset generation, and the
same default-enabled hand sensor.
Feed, travel, syntax, installed axes and duplicate parameters are validated.

The master API accepts raw gcode on /api/command. A request carries the boot
session, control-lease client/fence, monotonically increasing seq, and a unique
id. Repeated IDs with matching contents return receipts; conflicting IDs or
old sequences are rejected. Motion POST requests are not automatically retried
by the browser. Observers can poll; one phone holds a 15-second renewable
control lease. Stop is available without a lease.

Program upload uses /api/program/begin, sequential /append calls, and /start.
Each append sends a raw gcode line. The master maintains separate modal state
for the upload, validates/translates it, and stores local command arrays in
/program.jsonl. Limit: 4096 lines and 512 KB translated data. An unfinished
upload expires after 60 seconds and cannot run. Programs are not restored
or started after master reboot.

All uploaded lines must validate before the program becomes Ready. During
playback each move is checked against fresh homing state. Releasing motors
clears trusted position; homing is required again. Manual commands cannot
interleave with program execution.

## One step across two boards

1. Both bridges and both Octopus boards must answer normal Marlin status traffic.
2. Master sends prepare to both bridges, including an empty half if only one
   motor is targeted. Each bridge validates/stages local commands without
   sending them to Marlin.
3. Once both acknowledge preparation, master sends commit to both.
4. Each bridge sets G21/G90, streams one native line at a time, waits for its
   acknowledgement, and finishes with M400 then M114.
5. The final `M114` position report and `ok` acknowledgement complete the step.
   Only after both completions may the master issue another step.

Wi-Fi delivery, UART acknowledgement, Marlin scheduling, and acceleration
create start skew. Commit is not an atomic hardware trigger: one board can
start before the other receives its packet. Equal feed does not imply equal
durations for different distances. Precise synchronized motion needs an
additional timing/trigger design and hardware measurements.

## Faults and restart

- Peer/state timeout: 15 seconds; prepare timeout: 4 seconds.
- The bridge treats Marlin's normal `ok` and `M114` replies as health traffic.
- Homing (G28) acknowledgement and whole homing operation: 180 seconds.
  Ordinary commands retain 3-minute acknowledgements and 5-minute operations.
  The browser waits 10 seconds beyond the reported operation budget.
- Marlin stepper inactivity timeout: 10 minutes (previously 120 seconds).
- Bridge failures, command errors, missing acknowledgements, reboots during
  work, and link loss during active/paused programs latch a system stop.
  Master repeats the stop token until both bridges acknowledge.
- Each reachable bridge sends M410 for Stop or a confirmed motion fault. These
  paths do not guarantee identical stopping instants.
- Idle connection loss reconnects automatically. Interrupted programs never
  replay automatically.
- M410 leaves stock Marlin running. The bridge clears trusted position, resumes
  normal handshaking, and requires Home again. No remote-reset command or
  custom Marlin watchdog is installed.
- M400/M114 and steps do not detect a stalled motor or missed physical steps.
  Driver diagnostics can be queried with M122. There is no encoder feedback.

Marlin preparation modifies only .build/marlin-two-motor, applies the twelve
axis profile, and disables runtime SD autostart. The original sources are
preserved. The staged directory is recreated for each build.

The local demo invokes the actual C++ parser with simulated positions.
It is not an emulator of the ESP TCP/UART state machines or stop latency.

## Upstream Wi-Fi

The master runs AP+STA with Timiac at 192.168.4.1. `/api/wifi` requires the same
session, owner, and fence as other settings; it atomically saves SSID/password
as one NVS entry, or clears them with `forget`. Status never serializes the
password. Connection attempts run from the main loop while no motion or upload
is active, with 15-second attempt windows and capped backoff.
New motion is blocked during association. Internet reachability is not probed;
the status exposes router association and its assigned IP only. When connected,
NTP updates the DS3231 at most once every 12 hours while idle, using the
configured POSIX timezone rule.
