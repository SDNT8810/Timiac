# Idle motor shutdown fix - 2026-10-04.1

The clock can leave one whole Octopus at identical coordinates for over ten
minutes. Marlin's 600-second idle timer disables that board's motors and clears
its trusted position. Repeating the same G1 and polling M122/M114 do not keep
the planner active. A simulated clock starting at 12:00 reproduces rejection at
12:15: board 1 last moves at 12:05. A subsequent command then reaches the
HOME_AFTER_DEACTIVATE protection and the coordinator cancels automatic tracking.

Each bridge now queues `M84 S0` before homing or movement. This disables automatic
idle shutdown without moving motors or changing holding current. Explicit
M18/M84 release, Stop, driver checks and communication timeouts still apply.
Manual Home stays held indefinitely, including with the dashboard closed.

For the currently installed **TIMIAC2-1416cea7c0a8** / protocol 7 package, only
the two bridges need flashing (connect each module separately):

```powershell
python run.py verify
python run.py esp1_release --port COM5
python run.py esp2_release --port COM5
```

Use the adapter's actual port. Power-cycle after both uploads so normal release
startup homes and starts the clock. No Octopus SD-card update is needed. The
masters were rebuilt for a consistent revision, but the fix executes in the
bridges and works with the existing .8 master. Hardware was not flashed here.

# Home hold and global speed - 2026-09-29.8

Manual Home now leaves the release clock paused at home, including across minute
and hour boundaries and proximity triggers. Start clock resumes tracking.
The power-on automatic Home-then-clock sequence is unchanged.

The release speed slider ranges from 20% to 100%, with a 50% initial default.
It is saved in master NVS and shared by all motion sources. 100% keeps the
previous per-movement speeds; 50% halves them. The slider is available while
idle, and changing it does not resume a parked clock. Both bridges capture the
same setting before committing a synchronized move. Homing release, seek and
single-axis bump speeds also scale; movement deadlines scale inversely.

Install the full matched **TIMIAC2-1416cea7c0a8** / protocol 7 package:

```powershell
python run.py verify
python run.py octopus_release --sd I:\
python run.py esp1_release --port COM5
python run.py esp2_release --port COM5
python run.py master_release --port COM5
```

Repeat the Octopus card flash for both boards, and connect each ESP separately.
Use the actual card drive and adapter port. The master command includes the web
filesystem. Development aliases use the same Octopus and bridge images;
`master_dev` installs the development dashboard. No hardware was flashed during
this update. The previous master-only instructions below are historical.

# Hour-change reset fix - 2026-09-29.7

The automatic hour dance could overflow the ESP32's 8 KB Arduino loop stack
while generating the final clock pose. The manual Next hour request runs on the
HTTP task's larger stack, explaining why it could work while the automatic path
reset the master. Resetting command buffers in place and isolating single-frame
scratch storage removes this excessive stack use. Master export now checks the
actual linked animation frames and requires at least 2 KB of additional margin.

If the matching 50 mm motor package **TIMIAC2-409426dd9f18** is installed, upload
only the release master:

```powershell
python run.py master_release --port COM5
```

The same buffer fix is in `master_dev`. Both master uploads include their web
filesystem. Octopus and bridge images, machine identity, 50 mm travel, 800 mA,
500 mm/min, and 5 mm homing release are unchanged. No SD-card update is needed
for this fix. The full installation instructions below apply when upgrading
from an older motor package.

# 50 mm motor package - 2026-09-29.6

All twelve arms now use 50 mm maximum travel, reduced from 52 mm. Close,
clock poses, animations and both dashboards follow the same limit.
Install the complete matched package below, including both Octopus boards.

Release now runs the Next hour dance on a forward RTC hour boundary, including
midnight. It targets the new current time, keeps automatic tracking enabled,
and does not repeat the dance after completion. A boundary covered by a short
proximity animation runs after that animation finishes. Startup, manual resume
and setting the time establish a fresh baseline; Stop/fault cancels queued work.

Marlin now normalizes the full six-axis corner direction, avoiding false full
reversals when A/B/C move alongside XYZ. Junction deviation is 0.3 mm instead of
0.003 mm to blend successive moves with less braking. The existing 25 mm/s²
travel acceleration, 80 mm/s² per-axis ceiling and 500 mm/min maximum remain.

Both master modes now use the installed physical arm order: X1/Y1/Z1/A1/B1/C1
mark 12/11/10/9/8/7; X2/Y2/Z2/A2/B2/C2 mark 6/5/4/3/2/1. Clock poses and
generated animation G-code share this mapping. Maximum stroke and Close are now
50 mm. The homing release and single-axis bump increase from 2 mm to 5 mm.
Next hour and Struggle preload continuous trajectories into the bridges without
full-machine stops between frames. Struggle omits its two simulator pauses.
The master still owns every path. Release gains Next 5 min, Next hour and Struggle;
Open is removed from both dashboards.

Update both Octopus boards, both bridges and the selected master mode:

```powershell
python run.py octopus_dev --sd I:\
python run.py esp1_dev --port COM5
python run.py esp2_dev --port COM5
python run.py master_dev --port COM5
# Or choose the release master:
python run.py master_release --port COM5
```

Connect each ESP separately. Repeat the Octopus card flash for both boards.
Octopus and bridge `_release` aliases use the same images. Both master uploads
include their matching web filesystem. The new corner blending requires the
**TIMIAC2-409426dd9f18** identity on all boards and a patched Marlin planner.
This update therefore needs Octopus flashing too. After confirming
`FIRMWARE.CUR`, power off and remove the card; no runtime SD card is used.
Current remains 800 mA, with 64 microsteps interpolated to 256 and 500 mm/min feed.
Release retains automatic Home all at boot; developer mode retains manual startup.

# Previous release-master update - 2026-09-29.2

Release now automatically homes all twelve arms once both boards are healthy and
idle, then shows the current RTC time. Developer startup remains manual. The
master starts this sequence without a browser; Stop/fault cancels it without retry.
Homing does not require a valid RTC, but the following time pose does.

For the already installed **TIMIAC2-f5b8d948e714** motor package, upload only:

```powershell
python run.py master_release --port COM5
```

The command also installs the matching web filesystem. No Octopus SD-card or
ESP bridge changes are needed. Only the release master advances to revision .2.

# Motor package installation - 2026-09-29.1

Use 800 mA, 64 commanded microsteps interpolated to 256, and concurrent Home all in both modes.
When upgrading from older motor settings, reflash all five boards as shown in [README](../README.md), including the
master web filesystem, and reload the dashboard. Old 250/600/1,414 mA or
32- or native-256-microstep packages are incompatible. Historical incremental-upload advice
below does not apply when upgrading the motor package.

# Current dev/release package

See [README](../README.md) for the eight `_dev` / `_release` upload commands.
Both modes require microswitches; sensorless firmware is retired. The master
ESP32 owns motion programs and the Octopus card is only used for flashing.
Revision 2026-09-29.6 sets all drivers to 800 mA RMS requested current and
64 commanded microsteps interpolated to 256, with Lumiac's concurrent Home all. Update both Octopus
boards, both bridges and master together for identity TIMIAC2-409426dd9f18.
Older revision notes below describe their historical behavior.

# Timiac generation 2 rebuild

## Backup

The complete pre-rebuild workspace is saved in
`BackUp/before-rebuild-20260923-213040/Timiac.zip`.
All 10,370 files, including Git history, vendored sources, build caches and
old firmware, were read back from the archive and checked against the adjacent
`SHA256.json`. `VERIFIED.txt` records completion. Extract into a separate
directory to inspect or restore the old project.

## The confirmed mismatches

The old build inherited an X-only, modified Marlin configuration. X/Y used
SpreadCycle but Z/A/B/C inherited StealthChop. This exactly divides the axes
reported as working and failing. It is a confirmed configuration difference,
not proof that every motor stall is caused by firmware. A motor can still
vibrate because of a coil connection, load, supply, driver or mechanical fault.

The old bridge also considered a bare `ok` sufficient to identify the Octopus,
so a wrong firmware image could appear ready. Its Stop handler cancelled the
ESP queue without sending any stop command to the running motor board.

The rebuild replaces the Octopus configuration pipeline and UART controller
with a single machine definition and an allocation-free, host-tested state
machine. It updates the master/bridge protocol, readiness checks, per-axis
homing guard, dashboard diagnostics and release validation. Existing clock,
gesture, Wi-Fi, authenticated cloud, simulator and dashboard presentation
components are retained. The Marlin motion engine and Arduino libraries are
upstream dependencies, not rewritten drivers.

## One machine definition

`machine.json` defines all six physical channels, driver parameters, travel
and speed. `tools/machine_config.py` generates the ESP constants, readable
Marlin configuration and dashboard/package machine descriptions. The build
starts from the clean vendored Marlin **2.1.2.7 tag**, with the pinned working
Lumiac configuration adapted to Timiac. See [lumiac-comparison.md](lumiac-comparison.md)
for the source and every deliberate difference. All pins are assigned before
Marlin pin postprocessing.

| Public local axis | Driver slot and motor output | Switch input |
|---|---|---|
| X | MOTOR0 | STOP0 / PG6 |
| Y | MOTOR1 | STOP1 / PG9 |
| Z | MOTOR2 (use MOTOR2_1) | STOP2 / PG10 |
| A | MOTOR3 | STOP3 / PG11 |
| B | MOTOR4 | STOP4 / PG12 |
| C | MOTOR5 | STOP5 / PG13 |

The same table applies to both boards and now matches Lumiac. MOTOR2_1 and
MOTOR2_2 share DRIVER2; use MOTOR2_1 for Z and leave its parallel output empty.
MOTOR6/7 are unused. Move both drivers and cables to the matching sockets with
all power disconnected. This replaces the earlier MOTOR2 bypass; both boards
are being replaced with new boards. Fit the UART jumper for every occupied
driver slot; MS1/MS2 select UART address zero. Leave DIAG-to-endstop jumpers
removed when using the physical switches. The six switches remain on STOP0-5;
they do not shift with the motor socket.

Every axis now explicitly uses TMC2209 UART, 800 mA, 64 commanded microsteps interpolated to 256,
StealthChop holding with SpreadCycle above 1 mm/s (since quiet update .6),
24 V chopper timing for the reported 24 V / 7 A supply, 11,520 steps/mm, 50 mm travel,
120 mm/min concurrent microswitch homing and 500 mm/min maximum feed.
Sensorless homing is unavailable in both modes.
Lumiac's S-curve acceleration
uses 25 mm/s² travel acceleration, an 80 mm/s² per-axis ceiling, and 0.3 mm
junction deviation. The calibration is stored as 180 full steps/mm and scaled
with microsteps so changing resolution preserves physical travel.
Endstop validation and broken-switch checks remain enabled. EEPROM overrides
are disabled so an old saved calibration cannot replace these settings.

Marlin's G0/G1 homing guard checks the axes actually named in the command.
This allows a homed A to be tested independently while the other arms remain
unhomed. Both the bridge and Marlin still reject movement on an unhomed axis.

## Readiness, homing and Stop

Protocol 6 requires a matching generated identity on master and both bridges.
Each bridge verifies the Octopus identity and six-axis count using M115,
then checks a complete M122 report: UART connection, configured current,
microsteps and driver mode. A driver failure keeps motion unavailable and is
shown per axis on the development dashboard. Idle driver checks repeat two
seconds after each completed report in development mode, whether passing or
failing (five seconds in release mode); switch lamps remain available. The
twelve driver lamps clear stale readings and do not require a control lease.
The current column is explicitly **Set current**: M122 reports the firmware's
configured value, not a measured motor current. A driver with no UART response
shows unknown settings, since cached values or zero reads cannot confirm them.

M122 calls its last three driver columns **I/J/K**. This is a Marlin diagnostic
label only. All motion commands remain **X/Y/Z/A/B/C** and all public names
remain **X1 Y1 Z1 A1 B1 C1 X2 Y2 Z2 A2 B2 C2**.

Homing performs another driver check during preparation. Development manual
commands prepare and wait only for the boards they target. You can home and
move X1 through C1 while Octopus 2 is disconnected, and vice versa. Home all,
clock actions and uploaded programs still require both bridges to finish
preparation before either is committed. The bridge preserves `G28 X Y Z A B C` as one command. Octopus uses the
Lumiac concurrent seek: each motor stops at its own switch. A pressed switch
first releases 5 mm; a switch that stays pressed or never triggers fails the
operation. Single-axis and partial-group commands retain standard Marlin homing. One UART command is outstanding at
a time, and every job ends in M400/M114. A homing operation has 300 seconds
from commit (180 seconds per UART acknowledgement), independently of the ESP heartbeat timer.

Stop sends **M410** for ordinary motion, invalidates all positions, and drains
old replies through M400 and an M118 marker before re-identifying the Octopus.
Each drain attempt uses a different marker and consumes its following `ok`.
A missing drain reply keeps the board offline and retries this stop sequence
every ten seconds; it does not invent a Marlin halt. Timeout messages identify
the unanswered command. Lost commands and cleared homing state are not restored.
Stop during a running G28 sends **M112**: stock Marlin's blocking homing
routine cannot be cancelled safely by M410 alone. This case truthfully shows
**Reset required**. There is no wired remote-reset signal in the documented
hardware, so a killed Octopus requires its reset button or a power cycle.
Idle link loss reconnects automatically. Active link loss past 15 seconds
cancels work; interrupted motion is never automatically replayed.

Wi-Fi commit is approximate synchronization, not a simultaneous hardware
trigger. Driver checks and M114 cannot establish actual shaft motion or
detect all missed steps; the machine has no position encoders.

## Build and install this generation

```powershell
python build.py all
python run.py verify
```

Marlin intermediates are generated under `%TEMP%\TimiacBuild\marlin` to avoid
filling the project drive; `TIMIAC_BUILD_DIR` can override that build root.
The outputs remain in `firmware/two-motor`. The flashing commands retain their
old names. `run.py` verifies all image checksums, matching identities and ESP32
flash layouts before any flash or card copy.

For this generation update **all five physical controllers**:

```powershell
python run.py octopus --sd E:\
python run.py esp1 --port COM5
python run.py esp2 --port COM5
python run.py master --port COM4
```

Prepare and boot **each Octopus card** with the first command; confirm each
board renames `firmware.bin` to `FIRMWARE.CUR`. The ESP commands use example
ports: select the actual adapter port and connect one bridge at a time.
Disconnect a bridge from the Octopus UART while programming it. Use the
development `master` image for commissioning; it includes the switch lamps
and driver table. `master_release` remains available for the user dashboard.

After flashing, confirm all twelve driver rows pass. Press each switch by
hand while idle and check that only its matching lamp turns green. Home
Z1 and Z2 individually, then A1/A2/B1/B2/C1/C2. If a motor only vibrates,
stop that test and capture its driver row and first fault. A green driver
table confirms communication and register settings, not mechanical movement.

## Independent manual testing — 2026-09-24.3

If the Lumiac socket layout and 32-microstep firmware are already installed
(`TIMIAC2-ed9f017c2e53`), this update needs only the development master:
`python run.py master --port COM5` (replace COM5 with the master ESP32 port).
This flashes its application and web filesystem. Current Octopus and bridge
images remain compatible; no SD-card update is needed for this change.

Reload the dashboard, take control, select X1, and use **Home selected**.
Only Octopus 1 must be connected with all six driver checks passing. Move
selected axis becomes available after that axis homes. Repeat for Y1 through
C1. The note below the buttons explains why a selected axis is unavailable.
The top clock Home and **Home all 12 axes** still require both boards.
Stop and connection loss cancel work without automatic replay; homing interrupted
by Stop still needs an Octopus reset.

## Wi-Fi send-buffer fix — 2026-09-24.4

The earlier bridge could close its TCP connection whenever a whole state
packet did not fit immediately in the socket's send buffer. UART log bursts
around M114 completion made this possible even after a successful home. The
master then correctly cancelled the uncertain command and the bridge ran its
M115 recovery handshake, which looked like a board reboot. The connection log
also repeated the ESP's original boot reason on every reconnection.

Both ESP platforms now retain partial output and retry the remaining bytes
without blocking the controller loop on buffer availability. Telemetry is
bounded, genuine link failures still cancel work, and queued commands cannot
survive Stop or reconnection. Boot reasons are logged only for a new ESP boot.

For this update flash **the development master and bridge 1** with `master`
and `esp1`, using each module's actual USB port. Flash bridge 2 with `esp2`
before connecting it later. Do not flash esp1 and esp2 successively onto the
same physical module. The Octopus image, sockets, 32 microsteps and generated
machine identity have not changed; no Octopus SD update is needed.

## Follow-up: Z1 UART failure and Board 2 timeout

Historical record from the earlier MOTOR2 bypass layout. For the current
Lumiac layout, use the socket table above: Z is on MOTOR2 and A is on MOTOR3.
Migrating from that older layout requires updating both Octopus boards and all
three ESPs; the ESP-only upload guidance below applies to that earlier follow-up.

The reported dashboard shows five responding Board 1 drivers and **Z1 / MOTOR3
No response**. This isolates the reported driver-communication failure; it does
not prove that any arm can physically home. With all power, including USB,
disconnected, check that MOTOR3 contains the Z TMC2209 module, seated correctly,
and has its UART jumper set like the responding slots. Moving only the motor
cable does not move its driver module. Keep the current socket and axis map.
Follow the [BTT Octopus UART jumper guide](https://github.com/bigtreetech/docs/blob/master/docs/Octopus.md#uart-mode).

Board 2's **Octopus acknowledgement timed out** does not establish a firmware
halt or identify a failed motor. The earlier bridge incorrectly turned a later
missing stop acknowledgement into a permanent reset requirement. That path now
recovers as a communication fault. If Board 2 remains offline, check its power
and ESP-to-Octopus serial connection before attempting motion.

This follow-up changes the bridges and dashboard only. Rebuilt images are in
`firmware/two-motor`; run `python run.py verify`, then flash `esp1`, `esp2` and
the development `master` using the commands above and the actual ports. The
Octopus image is unchanged, so boards already running this generation do not
need another SD-card update. Reload the dashboard after flashing the master.
The user-mode `master_release` package is also rebuilt.

## Follow-up: bridge absent or driver check never completes

Application revision **2026-09-23.3** adds diagnostics without changing the
machine identity or Octopus image. Flash each physical ESP with its own role:
`esp1` on Octopus 1, `esp2` on Octopus 2, and `master` on the ESP32. When using
one USB adapter and port, physically change modules between the two bridge
commands; issuing both commands to one module leaves it running only `esp2`.

The development build label includes the application revision. The status API
adds each bridge's boot ID, address, software revision, uptime, and UART detail.
Connection logs include its boot reason and distinguish packet timeouts from
closed TCP connections; duplicate board roles are explicitly logged. A driver
check in progress shows its active command, elapsed wait and most recent reply.
Keep the master on Timiac Wi-Fi for read-only status capture while diagnosing.
These observations help identify the failure; they are not proof of a fix or
of physical motor operation.

## Validation limits

The Octopus build compiles assertions for the actual six-axis pin map,
driver currents, microsteps, UART addresses, SpreadCycle and homing protection.
Host tests execute the new UART kernel and all twelve parser-to-kernel routes.
They cover mismatched firmware, truncated driver reports, each disconnected
driver, the old mixed driver mode, delayed homing acknowledgements, Stop,
late replies, reset recovery, fault retention and switch telemetry.
Recovery tests include repeated missing drain replies, superseded markers,
the marker's own acknowledgement, timeout after a driver fault, and preserving
a confirmed halt through subsequent serial errors.
Dashboard tests cover lamps, driver faults, control fencing, program playback
and the preserved clock/Wi-Fi features. All firmware targets and both web
filesystems compile, and the package verifies.

These are software and build checks. No hardware flashing or physical motor
test has been performed by the assistant during this rebuild.
