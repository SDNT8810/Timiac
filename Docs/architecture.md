# Three-ESP architecture

Protocol 7 captures the master speed setting in every prepared operation on
both bridges. The initial 50% setting can be adjusted from 20 to 100% and is
saved in NVS. Changes require a control lease and an idle machine. Bridges
inject M220 before every job and scale G4 waits to preserve nominal timing
between moving and stationary boards. The Marlin patch applies M220 to all
homing phases and restores its temporary homing multiplier on return.

Manual release Home disables automatic tracking and proximity animations.
Start clock explicitly resumes tracking. Power-on startup still homes and
starts the clock without a browser. Changing speed does not resume a parked clock.

~~~text
Phone(s) -- Wi-Fi --> Master ESP32 (192.168.4.1)
                         |                |
                      TCP 8266         TCP 8266
                         |                |
                    ESP bridge 1     ESP bridge 2
                         | UART3          | UART3
                    Octopus 1        Octopus 2
                    XYZABC (1)       XYZABC (2)
~~~

Only the master creates the access point and HTTP server. Both bridges are
stations. Protocol v6 checks the generated machine identity and identifies board 1/2 explicitly with a shared pairing key,
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

The development master enables switch telemetry in its authenticated peer
heartbeat. Each bridge polls M119 at most every 500 ms while idle; polling never
interleaves with a prepared job, movement, or homing. A report becomes visible
only after its `ok`. The existing state packets and `/api/status` carry six
`endstops` booleans per board in XYZABC order (`true` = triggered), with null
for unknown values and `endstopsAgeMs` for freshness. The master preserves the
age across bridge heartbeats, and both the API and dashboard reject readings
at least two seconds old. A restart clears all known switch readings.
The development page refreshes status every 500 ms and requires no control
lease to view switches. Release mode uses a five-second M119 health cadence.

Each idle bridge repeats M122 two seconds after its last completed driver
report in development mode, or five seconds in release mode. This includes
passing drivers. Reports publish atomically after `ok`, carry their original
age across bridge heartbeats, and expire after 15 seconds. The twelve driver
lamps hide readings during motion or disconnection. Expected current and
microsteps are generated for the browser and firmware from `machine.json`.

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

The release master separately starts a fresh Home all sequence once per boot,
as soon as both boards are healthy and idle. It uses the existing two-board
prepare/commit barrier and one `G28 X Y Z A B C` per board. Once both homes finish
and all twelve homing bits are set, it reads the current valid RTC time and starts
the clock pose. No browser, control lease, SD card, or RTC validity is required to
start homing. Stop/failure cancels startup permanently for that boot; a manual
command accepted first also replaces the pending startup. Reconnects do not rearm
it. Developer mode has no automatic startup motion. This startup behavior lives
on the master; the separate 50 mm stroke and 5 mm homing backoff require the
matching Octopus image as well.

While release clock tracking is enabled, `releaseClockMinute` remembers the last
automatic target separately from the arm pose, so a proximity animation cannot
erase an hour boundary. A forward change into the next RTC hour starts the same
Hour preset as the button, targeting the current minute and keeping clock tracking
enabled. Midnight wraps normally. The boundary is claimed when the preset starts;
completion cannot trigger it again. Busy work delays it until idle, while Stop,
manual resume and explicit RTC changes reset the baseline. Large/backward time
jumps update the pose without replaying missed dances.

The automatic hour launch executes in the ESP32 Arduino loop task, whose stack
is explicitly 8,192 bytes. Manual API actions execute in AsyncTCP's 16 KB task.
Before revision .7, nested `step = Step()` temporaries made the automatic final
pose conversion account for 8,240 bytes of linked stack frames, before other
library descendants and task overhead. This explains a reset risk specific to
the automatic launch despite passing host tests. `Step::clear()` now resets
storage in place, and a non-inlined helper allocates single-frame scratch only
when needed. The corresponding release call chain accounts for 5,168 bytes.
`export_master.py` checks the actual Xtensa ELF with `check_master_stack.py`
and reserves at least another 2 KB for overhead; the report is saved as
`master_stack.json` beside the ELF. This is a regression budget, not a complete
runtime stack measurement or a guarantee about every library path.

All uploaded lines must validate before the program becomes Ready. During
playback each move is checked against fresh homing state. Releasing motors
clears trusted position; homing is required again. Manual commands cannot
interleave with program execution.

## Clock mapping and queued animations

Physical arm order is counterclockwise: X1/Y1/Z1/A1/B1/C1 correspond to
12/11/10/9/8/7, and X2/Y2/Z2/A2/B2/C2 to 6/5/4/3/2/1. Simulator frames retain
clockwise A/B/C/D/E/H/I/J/K/L/U/V order and are mapped when producing hardware
commands. Board-qualified manual G-code still addresses the named physical motor.

Next hour and Struggle prepare complete bounded trajectories on both bridges and
commit once. Each bridge validates and queues at most 32 motion/dwell lines, sends each
after its UART acknowledgement, and uses M400/M114 only at the end. This lets
Marlin blend consecutive moves instead of emptying its planner after every frame.
The master derives matching nominal frame durations from current positions using
Marlin's XYZ-first linear feed metric and axis speed limits. Only a board with
no movement in a frame dwells; the other keeps moving. Acceleration and network
timing mean this is not an exact inter-board timing guarantee.
Struggle's two simulator dwell frames are omitted from its embedded path and
generated hardware example. Manual Next controls and Struggle pause release
RTC tracking until Start clock or Home resumes it.

Marlin's junction direction is normalized across all six axes even when the
feed-distance convention uses XYZ alone. Previously mixed XYZ/ABC moves could
produce dot products outside [-1, 1], causing false full-reversal braking.
The planner now uses 0.3 mm junction deviation, with the same S-curve acceleration
and per-axis speed/acceleration ceilings. This increases permitted corner speed;
it does not add a dwell or bypass real reversal braking. Only an idle board waits
while the other moves. Exact physical smoothness still needs installation testing.

Trajectory support is advertised separately from protocol 7 and machine identity.
Both animations reject bridges without that capability. Existing Stop, disconnect, fault, stale
command and homing checks apply throughout the queue, with no replay on recovery.
Ordinary requests retain the six-command limit and other programs retain their
per-step completion barriers.

## Command participants

Development manual commands derive their board set from the validated local
command arrays. `G28 X1` and `G90 X1 1 F180` require only Octopus 1; commands
targeting only suffix 2 require only Octopus 2. All six drivers on the selected
board must pass, and movement still requires that particular axis to be homed.
An offline, faulted or restarting unused board does not cancel this manual test.
The API exposes per-board `manualReady` and the operation's `boardMask`.
Home all, clock actions, uploaded programs and unqualified diagnostics/release
commands continue to require both boards. A program retains both participants
even when an individual step contains commands for only one board.

## One coordinated step across two boards

1. Both bridges must match the master configuration. Each Octopus must pass M115 identity and complete M122 driver checks.
2. Master sends prepare to both bridges, including an empty half when a program
   step targets only one board. Each bridge validates/stages local commands without
   sending motion to Marlin. Homing preparation runs fresh M122 checks.
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

- TCP output uses a fixed 4 KB queue per peer and advances by the number of
  bytes actually accepted. Temporary send-buffer exhaustion and partial writes
  no longer disconnect the bridge. Pending status/log/ping traffic is skipped
  instead of accumulating; current status is sent again at the next heartbeat.
  Control packets are retained in order. A send error or 15 seconds without
  transmit progress still closes the link and cancels active work. Stop discards
  unsent control packets; a partly transmitted frame forces a disconnect rather
  than sending a truncated command. Queues are cleared before reconnection.
- Connection logs distinguish the same ESP boot reconnecting from a new boot.
  Marlin's actual `start` banner is logged separately and increments the existing
  Octopus restart counter. An M115 identification reply is not a restart event.
- Peer/state timeout: 15 seconds; prepare timeout: 15 seconds.
- The bridge treats Marlin's normal `ok` and `M114` replies as health traffic.
- At 100%, all commands allow 3-minute acknowledgements and 5-minute operations.
  Limits scale inversely with speed: 6/10 minutes at 50%, 15/25 minutes at 20%.
  The browser waits 10 seconds beyond the reported operation budget.
- Marlin's compiled stepper inactivity timeout is 10 minutes. Since
  2026-10-04.1 the bridges queue M84 S0 before each operation to disable automatic
  idle shutdown: clock/Home poses must retain their trusted position indefinitely.
  Explicit M18/M84 motor release and Stop remain available.
- Bridge failures, command errors, missing acknowledgements, reboots during
  work, and link loss during active/paused programs latch a system stop.
  Master repeats the stop token until every participating or connected bridge
  acknowledges. A lost participant must return and acknowledge; an unused board
  that has never connected cannot block recovery. Readiness after Stop is checked
  on the interrupted job's participants. An unused ESP with no Octopus attached
  only has to acknowledge cancellation. A late/reconnected bridge receives the
  latest Stop before it can be selected for a new job.
- Each reachable bridge sends M410 for ordinary Stop, or M112 if a G28 is
  in flight. M410 alone cannot cancel the following homing bump moves. These
  paths do not guarantee identical stopping instants.
- Idle connection loss reconnects automatically. Interrupted programs never
  replay automatically.
- M410 leaves stock Marlin running. The bridge clears trusted position, resumes
  normal handshaking after draining old replies, and requires Home again.
  Drain retries use distinct M118 markers every ten seconds and consume the
  matching marker's `ok` before handshaking. Missing replies remain a recoverable
  offline condition; they do not imply a killed firmware. Retries never resend
  a motion job. A new UART failure still drains replies when already latched.
  A killed Octopus requires a physical reset. No remote-reset command or
  custom Marlin watchdog is installed.
- M400/M114 and steps do not detect a stalled motor or missed physical steps.
  Driver diagnostics can be queried with M122. There is no encoder feedback.

Marlin preparation extracts the clean 2.1.2.7 tag into `%TEMP%/TimiacBuild/marlin`,
then adapts the pinned Lumiac configuration with `machine.json`. The original
Lumiac files and their hashes live in `Marlin/profiles/lumiac-reference`.
The only motion-source change checks homing for the axes selected by G0/G1,
allowing independent arm tests. Runtime SD/autostart and EEPROM overrides are
disabled. The vendored working tree remains untouched. See [rebuild.md](rebuild.md).

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
