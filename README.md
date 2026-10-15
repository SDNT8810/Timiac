**Current master (2026-10-15.3):** the hand input now uses the Heschen NPN NC infrared switch on GPIO27 through a three-resistor 24 V interface. Existing GPIO18/19 traces remain unused. See the [complete master wiring diagram](Docs/master-wiring.md). Animations remain 30/10/30 mm then time for Struggle, and 30% slower open-then-time for Next Hour. Only the master needs flashing; current remains 1,000 mA RMS.


# Timiac: twelve motors, three ESP modules

**15-minute clock stop fix (2026-10-04.1):** the bridges now disable Marlin's
automatic motor idle shutdown with `M84 S0` before each operation. A clock pose
can leave an entire Octopus motionless for more than its old 10-minute limit;
unchanged G1 targets and diagnostic traffic do not reset that timer. This could
invalidate homing and stop tracking at the next five-minute update. Manual Home
also retains its holding torque until an explicit motor release.

The idle-shutdown fix is included in the current 1,000 mA package. Install all
five matching board images when upgrading from 800 mA; see [installation steps](instruction.md).

**Home hold and speed control (2026-09-29.8):** manual Home in the release
dashboard now parks the arms until another movement command. Start clock
resumes automatic time tracking. Power-on startup still homes and starts the
clock automatically.

The release dashboard has a **20–100% movement speed slider**, initially 50%.
100% is the previous speed for each movement (up to 500 mm/min travel and
120 mm/min homing). The saved setting applies to both boards, including dances,
five-minute/hour changes, automatic clock movements, and all homing phases.
Change speed while idle; it applies to the next movement and survives reboot.

**Reflash both Octopus boards, both ESP bridges and the selected master** using
the installation commands below. The matched package is
**TIMIAC2-03b3b6e145e4**, protocol 7. This update includes a Marlin homing-speed
patch; earlier master-only upload instructions apply only to earlier revisions.

**Hour-change reset fix (2026-09-29.7):** the automatic dance no longer creates
nested temporary command buffers that exceed the ESP32 loop stack. Both master
builds reset buffers in place and pass a compiled stack-budget check. For an
installed matching **TIMIAC2-409426dd9f18** / 50 mm package, upload only
`python run.py master_release --port COM5` (or `master_dev` for development).
The Octopus and bridge images are unchanged; no SD-card update is needed.

**50 mm travel (2026-09-29.6):** all twelve arms now have a 0-50 mm
position range. Close sends every arm to 50 mm in both dev and release modes.
Clock poses and generated animations use the same 50 mm stroke.

**Automatic hour dance and corner blending (2026-09-29.5):** release now runs
the Next hour dance once when the RTC crosses an hour, including midnight,
then continues following the clock. At 01:59 → 02:00 it dances to 02:00.
Ordinary five-minute updates still move directly to their pose. Startup and
manual time changes establish the current pose without an extra hour dance.

Marlin now normalizes corner directions across all six moving axes and uses
0.3 mm junction deviation instead of 0.003 mm, reducing corner braking during
Next hour and Struggle. Acceleration and maximum speed stay unchanged; genuine
direction reversals still slow down. Reflash the matched package below.

Both master
modes use the installed counterclockwise wiring: X1 is 12, Y1 is 11, through C2
at 1. Next 5 min and Next hour advance clockwise to the correct physical arms.
The maximum actuator stroke is **50 mm**. Next hour and Struggle each queue one
continuous path per bridge; Struggle omits its former simulator pauses.

Install developer mode (connect each ESP separately):

```powershell
python run.py octopus_dev --sd I:\
python run.py esp1_dev --port COM5
python run.py esp2_dev --port COM5
python run.py master_dev --port COM5
```

Install release mode:

```powershell
python run.py octopus_release --sd I:\
python run.py esp1_release --port COM5
python run.py esp2_release --port COM5
python run.py master_release --port COM5
```

Repeat the Octopus card flash for both motor boards. The Octopus and bridge
images are the same in both modes; each master command includes its matching
web filesystem. The SD card is only needed to flash an Octopus and can be
removed after `FIRMWARE.CUR` is written. The matching identity is
**TIMIAC2-03b3b6e145e4**.

**Release startup:** at power-up, the master
waits for both motor boards to be healthy and idle, homes all twelve arms together,
then displays the current RTC time and continues tracking it. No browser is needed.
Developer mode still starts manually. Stop or a homing fault cancels automatic
startup; reconnecting a board does not retry it. If the RTC is invalid, homing
still runs and the clock pose waits for valid time.

**Motor settings:** all twelve arms request **1,000 mA RMS**
for running and microswitch homing, with **64 commanded microsteps** over UART
and **internal interpolation to 256**. Calibration is **11,520 steps/mm**; travel
is 0-50 mm. Maximum feed is **500 mm/min** (96,000 step pulses/s); homing
is **120 mm/min at 100%** (60 at the default 50%). The holding multiplier remains 0.5 (approximately 500 mA at rest).
Driver chopper timing now uses the 24 V preset for the planned 24 V / 400 W (16.7 A) supply.

**Home all** now uses Lumiac's concurrent six-arm routine on each Octopus:
all six seek together and each stops at its own physical switch. The master
prepares both bridges before starting both groups; Wi-Fi is not a guarantee
of identical start times. Home selected retains ordinary single-axis homing.
Pressed switches first release by 5 mm; a stuck or missing switch fails homing.

The development driver table now shows the values received in Marlin's M122
report, including numerical mismatches, rather than substituting browser
constants. These are configured values, not measured coil current.
When upgrading from older motor settings, reflash **both Octopus boards, both bridges, and the chosen master mode**;
the master command also installs its web filesystem. Reload the dashboard.
The matching machine identity is **TIMIAC2-03b3b6e145e4**.

The requested current is now 1,000 mA, superseding the earlier 1,400 mA setting. The Board 2 C2 release fault remains physically unverified;
new homing errors name the affected axes, and switch protection stays enabled.

**Generation 2 rebuild:** see [the rebuild and installation guide](Docs/rebuild.md).
The original project is fully archived under `BackUp/`. Machine configuration
now comes from `machine.json`; all six TMC2209 drivers use quiet holding
and SpreadCycle above 1 mm/s in the normal microswitch image.
The new bridge checks firmware identity and every driver before motion.
When migrating to generation 2, update **both Octopus boards, both bridges and the master** together. Verify
the prepared package with `python run.py verify`.

**Dev and release modes (2026-09-28.1):** both use **normally closed
microswitches only**. Sensorless firmware, tuning commands, and dashboard controls
have been removed. If you tried DIAG homing, power off, remove the MOTOR0–5
DIAG jumpers, and reconnect the NC endstop switches before homing.

The master ESP32 owns all clock poses, animation sequences, and uploaded G-code.
The bridges relay validated commands; Octopus executes the motor steps and
endstop checks. The Octopus SD card is used **only to install firmware**.
After `firmware.bin` becomes `FIRMWARE.CUR`, power off, remove the card, and
power on again. No G-code files are copied to the card or read at runtime.

From the `Timiac` directory, install developer mode:

```powershell
python run.py octopus_dev --sd I:\
python run.py esp1_dev --port COM5
python run.py esp2_dev --port COM5
python run.py master_dev --port COM5
```

Or install release mode:

```powershell
python run.py octopus_release --sd I:\
python run.py esp1_release --port COM5
python run.py esp2_release --port COM5
python run.py master_release --port COM5
```

Repeat the Octopus flash for both boards; connect each ESP separately on COM5.
Octopus and each bridge use the same image in both modes. The master has separate
firmware and matching web filesystems. Unsuffixed commands remain dev aliases.
After this matched update, switching modes only needs the master upload.

**Dev:** the existing dashboard, manual controls, G-code uploads, driver/switch
diagnostics, and test animations, with microswitch homing.
**Release:** Home, Close, Start clock, Next 5 min, Next hour, Struggle, Stop,
time and Wi-Fi settings.
Release starts with automatic Home all, then continuous RTC tracking. Manual
Home also enables tracking once homing finishes. Start clock resumes
tracking; Close, Next 5 min, Next hour, Struggle, Stop and faults pause it. Arms update at five-minute
boundaries and return to current time after a hand gesture, with no phone or
SD card required. Every release-master boot starts a fresh homing sequence;
interrupted commands and programs are never replayed. After Stop or fault recovery,
Home again if needed, or Start clock when all arms remain homed. Developer mode
requires manual Home after power-up. The RTC must be valid to display time.
Development command/program endpoints and detailed RTC diagnostics are unavailable
in release mode. Internal driver, endstop, Stop and connection checks remain active.

`python build.py all` rebuilds all images; `python run.py verify` checks the package.
The eight mode names also work as individual `build.py` targets (build only).

**Connection stability update (2026-09-24.4):** update the master and installed
ESP bridges for the TCP send-buffer fix. With only Octopus 1 connected, use
`python run.py master --port COM5` and `python run.py esp1 --port COM5`, replacing
the port and physically connecting the correct module for each command.
Update bridge 2 as `esp2` when adding it later. No Octopus SD update is needed.
This fixes a software path that disconnected after status/log bursts; see
[the update details](Docs/rebuild.md#wi-fi-send-buffer-fix--2026-09-244).

**Independent manual testing (introduced in 2026-09-24.3):** reload the page,
take control, and use **Home selected** on X1 through C1 while
Octopus 2 is disconnected, or vice versa. Move selected axis requires that
axis to be homed. Home all, clock actions and uploaded programs need both boards.

The **master ESP** creates Wi-Fi **Timiac**, password **12345678**, and serves
the control panel at **http://192.168.4.1**. “Timiac” is the Wi-Fi network name
(SSID); there is no separate Wi-Fi username.

Two ESP-12S modules join the hotspot automatically. **Bridge 1** sends
commands to X/Y/Z/A/B/C on Octopus 1; **Bridge 2** sends commands to the
same six local axes on Octopus 2.
The master has no Octopus UART connection. Each bridge has a fixed identity,
so reconnecting or receiving a different IP address cannot swap the motors.

All twelve axes are enabled: **X1 Y1 Z1 A1 B1 C1 / X2 Y2 Z2 A2 B2 C2**.

## Ready-to-flash files

All release files are in [firmware/two-motor](firmware/two-motor):

| File | Destination |
|---|---|
| **firmware.bin** | **Both Octopus boards: the exact same file on both microSD cards** |
| esp_master.bin | Master ESP32: development dashboard |
| esp_master_release.bin | Master ESP32: user dashboard |
| esp_board1.bin | ESP-12S attached to Octopus 1 |
| esp_board2.bin | ESP-12S attached to Octopus 2 |
| master_littlefs.bin | Master ESP's web files; use the uploadfs command below |
| master_release_littlefs.bin | User dashboard web files (flashed by `run.py master_release`) |
| SHA256.json | File integrity hashes |

For each Octopus, copy **firmware.bin** to the root of a FAT32 microSD,
insert the card with the board powered off, then power on. Check that the
bootloader renamed it to **FIRMWARE.CUR**. Do not put an ESP binary on these
cards. Runtime SD programs and autostart are disabled in this new firmware.

The Octopus build starts from clean Marlin 2.1.2.7 and the explicit six-axis machine definition:
**Octopus V1.1 / STM32F446, TMC2209 UART, MOTOR0/1/2/3/4/5, 1,000 mA RMS setting,
64 microsteps interpolated to 256, 11,520 steps/mm, 0-50 mm travel, 500 mm/min maximum
maximum speed, 25 mm/s² S-curve acceleration** (80 mm/s² axis ceiling).
The build adapts the pinned Lumiac six-arm configuration; see
[the comparison and upload instructions](Docs/lumiac-comparison.md).
The minimum home input uses the
existing **normally closed switch** configuration. See [wiring.md](Docs/wiring.md).

## Build and upload the three ESPs

From this Timiac directory:

~~~powershell
python -m pip install -r requirements.txt
python build.py
~~~

There are three separate ESP build targets sharing common source:

~~~powershell
python build.py master
python build.py master_release
python build.py board1
python build.py board2
python build.py marlin
~~~

The Marlin source is generated in `%TEMP%/TimiacBuild/marlin`. The ready-to-flash
release files are created in `firmware/two-motor`.

## Flash the ready-to-flash release

`run.py` flashes the already-built release files; it does not rebuild them.
Connect one ESP-12S at a time in bootloader mode. Both bridge commands default
to **COM5**, so unplug the first bridge before connecting the second:

~~~powershell
python run.py esp1
python run.py esp2
~~~

Use `--port` when the USB-UART adapter is on a different COM port, for example
`python run.py esp1 --port COM6`. The master ESP is an ESP32 and also needs its
LittleFS web files, which are flashed together by:

~~~powershell
python run.py master --port COM4
~~~

## User dashboard and DS3231 clock

The two master modes have separate firmware and web filesystem images. From
the `Timiac` directory, select the dashboard when flashing:

~~~powershell
python run.py master_dev --port COM5      # Development dashboard
python run.py master_release --port COM5  # User dashboard
~~~

Both serve **http://192.168.4.1/** on the same Timiac Wi-Fi network. The user
dashboard contains Home, Close, Start clock, Next 5 min, Next hour, Struggle, a live RTC readout, and a
date/time setting box. Stop uses a quick-stop for ordinary motion; interrupted
homing requires an Octopus reset. Wait for both boards
to report back, then Home before moving again. Control is acquired automatically,
with one device controlling at a time. The development G-code and
program endpoints are disabled in release mode.

Development mode includes the same clock actions, RTC time setting, live hand
distance and automatic hand gestures, alongside manual control and G-code
uploads. Its buttons require **Take control**. Both dashboards share clock and
sensor display code and use the same firmware action/preset implementations.

The development dashboard also shows twelve **Micro switches** lamps labeled
**X1 Y1 Z1 A1 B1 C1 / X2 Y2 Z2 A2 B2 C2**. A released switch is **red**;
a triggered switch is **green**. Press a switch by hand while the boards are
idle to identify it, with updates arriving about once per second or faster.
This display works before homing and without taking control. **Gray** means
the reading is unavailable, stale, disconnected, or paused during motion.
Switch states come from Marlin's M119 reports, independently of axis positions.
An open circuit on the normally closed switch wiring also reports triggered.

To install the lamps, rebuild and flash **both ESP bridges and the development
master including its filesystem** (`python run.py esp1`, `python run.py esp2`,
`python run.py master --port COM5`, substituting the actual ports). The existing
Octopus firmware already supports these reports. Automatic switch polling is
enabled by the development master; the release dashboard has no switch panel.

- **Home:** homes all twelve axes to **0 mm**, which is physically fully open.
  Release runs this automatically at master startup; dev requires the button.
  Required again after a motor-board restart or loss of homing state.
- **Close:** moves all arms to the configured maximum stroke (**50 mm**).
- **Show time (dev) / Start clock (release):** reads the DS3231 and moves to the current pose.
  Actuator targets are **25 mm** for the hour arm, **0 mm** for the minute arm,
  and **50 mm** for all other arms (closed).
  Minutes use five-minute steps, rounded down. When hour and minute overlap,
  the fully open minute arm takes precedence. The physical mechanism is inverted
  relative to the simulator's extension coordinates. Dev moves once per press;
  release continues following the RTC.
- **Struggle:** all twelve arms share the same opening targets: **30 mm, 10 mm,
  30 mm, then current time**, without a programmed pause. The first three poses
  correspond to native actuator coordinates 20, 40, 20 mm. In the final move,
  other arms close fully while the hour/minute arms take their clock positions.
  Vector feed remains capped at 400 mm/min and travel acceleration at 20 mm/s².
  Homing and a valid RTC are required. The current time is captured when the
  command is accepted, and the arms retain that final pose with tracking paused.
- **Next 5 min:** advances the arm pose to the next five-minute boundary, just
  like the simulator's Next 5 min control (13:58 becomes 14:00).
- **Next hour:** adds one hour, runs `Simulator/gcodes/hour-movement.gcode`, and
  opens all arms together, then closes directly into the selected time
  (13:35 becomes 14:35). The minute arm stays open and the hour arm moves halfway.
  Both moves run 30% slower (350 mm/min per-axis ceiling before the speed slider).
  Removing the separate all-closed pose keeps the software timing bounds within
  40 seconds at 50% and 20 seconds at 100%.

The next-time controls advance from the last arm time, or the RTC if no arm time
is selected. Repeated presses continue advancing; midnight wraps normally.
They **do not change the DS3231**. The large clock continues showing RTC time;
the smaller Arms label shows the selected pose. Show time returns to the RTC pose.

Animations run on the master and continue if the phone disconnects. Next hour
and Struggle are one prepared trajectory per board, with only one final M400/M114 completion
check. The master calculates the whole path; bridges queue and relay it. Nominal
frame durations match across boards; a stationary board waits while the other
moves. There are no full-machine dwell frames. Marlin still controls acceleration
and blending, so exact timing and physical smoothness need hardware verification.
Trajectory feed is normalized to motor speed; the vector feed may exceed 500 mm/min
while each motor stays within its own limit. Every job restores normal acceleration
before motion, including after an interrupted Struggle. Built-in paths bracket
their acceleration settings. The old Struggle G4 pauses are omitted; other animations and uploaded programs retain their step barriers.
Stop or a board fault cancels the remaining sequence.
The dashboard shows progress and disables other movement controls until completion.
Home all axes before starting either animation.

The animation source files are converted automatically when building the master.
The old `A B C D E H I J K L U V` axis order becomes
`X1 C2 B2 A2 Z2 Y2 X2 C1 B1 A1 Z1 Y1`. Simulator extension is converted to
actuator position as `stroke × (1 − extension / 100)`: 100% becomes 0 mm,
0% becomes 50 mm. Feeds scale by stroke/100 and are capped at the hardware limit.
Ready-to-run examples using the current 50 mm stroke and 500 mm/min limit are
generated in `gcodes/circular-struggle.gcode`, `gcodes/hour-movement.gcode`, and
`gcodes/curious-creature.gcode`.
Edit the simulator source files and rebuild to change these animations.

Manual moves, board-qualified hardware G-code, bridge UART commands, and Octopus
position reports all remain millimetres from the existing home endstop. They
are already actuator coordinates and receive no additional inversion. Positive
relative motion closes an arm; negative motion opens it. Homing direction,
endstop wiring and physical calibration remain unchanged (now 11,520 steps/mm
at 64 commanded microsteps, formerly 720 at four). Homing now uses
120 mm/min, with a 180-second acknowledgement budget and a 300-second
whole-operation budget. Marlin uses its normal serial command path;
there is no custom host watchdog or remote-reset command.
Reload the updated hardware animation files before uploading older programs.
`gcodes/small-wave.gcode` now uses board-qualified hardware names and first closes
all arms; the 1 mm smoke test remains a small actuator move from home and back.

The clockwise arm order, starting at **12 o'clock**, is
**X1, C2, B2, A2, Z2, Y2, X2, C1, B1, A1, Z1, Y1**. At 03:30, A2 is halfway
open and X2 fully open. The installed board order is:

| Board 1 arm | Clock position | Board 2 arm | Clock position |
|---|---|---|---|
| X1 | 12 | X2 | 6 |
| Y1 | 11 | Y2 | 5 |
| Z1 | 10 | Z2 | 4 |
| A1 | 9 | A2 | 3 |
| B1 | 8 | B2 | 2 |
| C1 | 7 | C2 | 1 |

Connect the DS3231 to the **master ESP32**, using 3.3 V-compatible I2C levels:

| DS3231 | Master ESP32 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |

SQW and 32K are unused. Fit the backup battery appropriate for your RTC module.
The pins can be overridden with `TIMIAC_RTC_SDA` and `TIMIAC_RTC_SCL` in
`ESP_Timiac/include/config.local.h`, then rebuild both master modes.

Enter the local date/time, or press **Use phone time** to fill the form, then
**Save time**. The master writes the DS3231 and reads it back before confirming.
The dashboard's time comes from the RTC, independently of the phone. Missing
hardware or lost RTC power is reported; Show time stays disabled until the RTC
has valid time. Home/Close still work without an RTC. Booting or flashing
does not overwrite the RTC. Release homes on boot and displays time once the RTC
is valid; developer mode starts without movement.

When the saved router/hotspot has internet access, the master synchronizes the
DS3231 from NTP after it joins and then every 12 hours while idle. The default
timezone is Italy, including daylight-saving changes. Override
`TIMIAC_NTP_TIMEZONE` in `config.local.h` with a POSIX timezone rule when the
clock is installed elsewhere. Manual saving remains available if internet is
unavailable. Dates from 2000 through 2099 are accepted. RTC access uses
[Adafruit RTClib's DS3231 driver](https://adafruit.github.io/RTClib/html/class_r_t_c___d_s3231.html).

After source changes, rebuild the relevant images before using `run.py`:

~~~powershell
python build.py master
python build.py master_release
~~~

Flashing either mode replaces the master's web filesystem and stored program
file. The RTC and the two bridge/Octopus firmwares do not need reflashing when
switching dashboard modes.

## Hand proximity: Heschen E3F-DS30B2 NPN NC

The new infrared switch uses **GPIO27** through a **4.7 kΩ / 68 kΩ / 10 kΩ**
interface for the existing regulated 24 V supply. Keep the current 24 V → 3.3 V
converter for the ESP32 and RTC. Brown sensor wire goes to 24 V, blue to common
ground, and black to the resistor interface. Never connect black directly to
GPIO27. Old ultrasonic GPIO18/19 PCB traces stay unused. See the complete
[diagram, wire table and voltage checks](Docs/master-wiring.md).

This is an on/off photoelectric switch, not LiDAR. Its advertised sensing
range is adjustable around 10–30 cm, but it has no software beam-angle control
or measured-distance output. Both dashboards show **Detected / Clear** instead
of centimetres. Check the optical path and sensitivity with an actual hand.

After homing, clear the sensor for 500 ms, then present a hand for 200 ms to run
`curious-creature.gcode` once. Withdrawing the hand after completion rearms it.
Motion, Stop, faults and missing motor boards discard pending gestures. In
release mode, gestures require the clock to be running; manual Home keeps it
parked. Dev mode also supports gestures when idle/homed. The sensor does not
recognize gesture direction or shape, and NC wire/power failure can mimic a hand.

The defaults are `TIMIAC_PROXIMITY_ENABLED=1`, `TIMIAC_PROXIMITY_PIN=27`, and
`TIMIAC_PROXIMITY_ACTIVE_HIGH=1`. The divider requires an ordinary GPIO input
without an internal pull-up. Rebuild after changing configuration.

## Prepare an Octopus card

To prepare an Octopus card in **E:**, use:

~~~powershell
python run.py octopus
~~~

This verifies and copies `firmware.bin` to the card root and copies the
reference files from `gcodes/` to `E:\gcodes\`. Existing destination files are
backed up to `TIMIAC_BAK`. If Windows reports the card reader as a fixed drive,
repeat with `python run.py octopus --allow-fixed-drive`.

Use a 3.3 V USB-UART programmer. Replace COM4/COM5/COM6 with your actual ports.
Disconnect each bridge from its Octopus UART while flashing.

ESP-12S bridge builds assume 4 MB flash. Credentials and active-axis limits
are in [config.h](ESP_Timiac/include/config.h); optional config.local.h
overrides must match on all three modules.

## First movement

The axis-name correction requires flashing **both ESP bridges**. The master
keeps `X1 Y1 Z1 A1 B1 C1 X2 Y2 Z2 A2 B2 C2`; each Octopus receives local
`X Y Z A B C` commands unchanged. Marlin's `I/J/K` driver identifiers are
internal; `AXIS4_NAME`, `AXIS5_NAME`, and `AXIS6_NAME` explicitly select
`A/B/C` for G-code and position reports. All prepared images are in
`firmware/two-motor`. If an Octopus has an older or experimentally remapped
image, flash the shared `firmware.bin` to both boards too.

1. Connect each bridge's GPIO1/TX to its Octopus PD9/RX, GPIO3/RX to PD8/TX,
   and GND to GND. Use a stable 3.3 V supply. UART is **115200 baud**.
2. For a manual test, power the master and the selected Octopus and its bridge.
   For clock actions/programs, power both boards and both bridges. Connect to
   **Timiac / 12345678** and open **http://192.168.4.1**.
3. Wait for the selected board's six driver checks to pass, then press **Take control**.
   Multiple phones can observe; one phone at a time can issue commands.
   Every connected phone can press Stop.
4. Use the live driver and microswitch panels to verify the connected board. Check every
   local X/Y/Z/A/B/C sensor reports open when released and TRIGGERED when actuated.
5. Home and test **one axis at a time**, checking its direction and sensor
   operation. Do not use the all-axis test until all twelve axes are commissioned.
6. After Stop or a recoverable communication fault, wait for the participating boards to
   report back, then Home. Interrupted motion is never replayed.

Developer mode starts without movement; release automatically homes all arms and
displays the current time once both boards are ready. A physical power cutoff is still
needed; software shutdown can release motor torque.

## Board status and faults

Both dashboards distinguish **ESP offline**, **Octopus offline**, **Homing**,
**Moving**, **Stopped**, **Home required**, and **Ready**. Fault details identify
the affected board. The ESP bridges retry Wi-Fi/TCP connections automatically.
A missed packet does not interrupt homing. A bridge-link loss past the
15-second heartbeat deadline cancels active work on the reachable boards.

Stop and automatic bridge aborts send Marlin's quick-stop `M410` for ordinary
motion. An in-flight `G28` requires `M112` and a physical reset, because M410
alone cannot cancel the following homing bump moves.
The bridge clears trusted positions, reconnects, and requires Home before the
next movement. A board showing `Printer halted. kill() called!` requires
its reset button or a power cycle after the fault is checked.
At 100%, homing and ordinary commands allow 180-second acknowledgements and
300-second operations. Both limits scale inversely with the speed setting.

## Router or phone hotspot connection

Expand **Router / hotspot Wi-Fi** in either dashboard, enter the **2.4 GHz**
network name and password, and press **Save & connect**. In development mode,
take control first. Settings persist across restarts in the master ESP32's NVS;
the API does not return the password. **Forget** removes the saved credentials.

The master uses AP+STA mode: the Timiac hotspot and `http://192.168.4.1` remain
available to phones and both ESP bridges while the master joins the router.
The status shows the upstream address, which can also serve the dashboard.
The upstream network must use a subnet other than `192.168.4.x`.

Connecting/retrying waits for idle. A connection attempt temporarily blocks new
motion; retries back off to 60 seconds. ESP32 shares one channel between its AP
and station, so joining a router may briefly reconnect hotspot clients; bridges
rejoin automatically. See [Espressif's AP/STA channel behavior](https://docs.espressif.com/projects/esp-idf/en/v4.4.2/esp32/api-guides/wifi.html).
This supplies the master with an upstream route. When that route has internet
access, the DS3231 is synchronized from NTP while the clock is idle. It does
not route internet traffic for the ESP bridges.

## Remote hardware dashboard at control.timiac.it

The [CloudGateway](CloudGateway/README.md) serves the hardware dashboard at
`https://control.timiac.it/` using Cloudflare Workers and D1. Sign in with the
configured username and password. The master ESP32 connects outward over
verified HTTPS every five seconds; no router port needs opening. The simulator
at `https://timiac.it/` remains separate.

The normal update command automatically uses the paired cloud release:

```powershell
python run.py master_release --port COM10
```

Replace `COM10` with the master USB port if needed. The cloud pairing defaults
are included in `ESP_Timiac/include/cloud_config.h`, and the paired firmware is
included in `firmware/two-motor/`. A fresh pull needs no credential copying and
no Cloudflare or Google login to flash the clock. `python build.py master_release`
rebuilds this same release. Flashing checks the image hashes, machine identity,
cloud URL and pairing key before writing.

The clock's device key is intentionally included in Git. Repository readers can
impersonate its status. Keep this repository private when sharing it with the
clock's operators. Cloudflare account keys and Google account tokens remain
outside the repository.

The public page stays available when the ESP32 is unplugged. It reports
**Master is offline** and disables commands after the last status is 30 seconds
old. Internet access alone does not enable the relay: the master must be
flashed with its paired release.

## Your command format

Your exact example is supported:

~~~gcode
G90 X1 10 Y1 20 A2 35 F350
~~~

The master translates this to:

| Octopus 1 | Octopus 2 |
|---|---|
| G90 | G90 |
| G1 X10.000 F350.000 | G1 X35.000 F350.000 |

Each bridge also establishes millimetres/absolute mode and waits for
M400/M114 completion and fresh health telemetry before reporting Done.

This is a **Timiac extension**: native Marlin G90 changes positioning mode;
G0/G1 performs movement. Board-qualified names never reach Marlin.
See [Marlin G90](https://marlinfw.org/docs/gcode/G090.html) and
[linear moves](https://marlinfw.org/docs/gcode/G000-G001.html).

Supported master commands:

~~~gcode
G28 X1 Y1 Z1 A1 B1 C1 X2 Y2 Z2 A2 B2 C2 ; home all axes
G90                        ; absolute mode
G1 X1 10 Y1 20 A2 35 F350   ; A2 becomes local I on Octopus 2
G91 X1 -1 C2 -1 F180        ; relative shorthand, checked against known positions
G4 P500                    ; dwell on both boards
M119                       ; endstops on both boards
M114                       ; reported positions
M115                       ; firmware identification
M122                       ; driver diagnostics
M400                       ; wait for moves
M18                        ; release motors; requires homing again
~~~

Use a space between the name and value: **X1 10**, not X110.
F is feed rate in mm/min. Limits are checked before transmitting.
Unsafe configuration commands, invalid syntax, uninstalled axes, and
out-of-range targets are rejected.

Open a .gcode file in the panel, press **Check & upload**, review its
translation, then **Run uploaded program**. The entire upload must validate
before it can run. Editing the text requires another upload. Programs run
from the master's LittleFS and continue if the phone disconnects.
Pause takes effect at a completed step boundary.

Both bridges prepare each step before the master commits it. The next step
waits for both motors to finish. **Wi-Fi does not provide exact simultaneous
starts or equal move durations**; a 10 mm move and 35 mm move at the same
feed finish at different times. See [architecture.md](Docs/architecture.md).

## Tests and local demo

~~~powershell
python -m pip install ziglang==0.13.0
python tests/run_firmware_tests.py
cd WebApp
npm test
npm start
~~~

The demo at http://127.0.0.1:4001 uses simulated motors and the same C++ parser
as the master. It does not emulate radio/UART timing or physical stops.
The older server.js, control.js and simulator tests remain as legacy tools.

Builds and host tests passed; the physical boards have not been flashed or
tested here. Browser visual verification was unavailable. See the
[validation record](Docs/validation.md).

## Axis mapping

Each board runs the same six-axis Marlin image. Dashboard and G-code names stay
`A`, `B`, and `C` all the way to the Octopus UART. Marlin internally calls the
three driver channels `I`, `J`, and `K`, while its configured G-code names are
`A`, `B`, and `C`.

| Timiac names | Local Marlin | Octopus motor socket |
|---|---|---|
| X1 / X2 | X | MOTOR0 |
| Y1 / Y2 | Y | MOTOR1 |
| Z1 / Z2 | Z | MOTOR2_1 (DRIVER2) |
| A1 / A2 | A | MOTOR3 |
| B1 / B2 | B | MOTOR4 |
| C1 / C2 | C | MOTOR5 |

This is Lumiac's socket order. MOTOR2_1 and MOTOR2_2 share one driver; leave
MOTOR2_2 empty for the single Z actuator. Endstops remain STOP0 through STOP5.
The previous MOTOR2 bypass is superseded; the user is replacing both boards
with new boards. See [the wiring and changes from the previous layout](Docs/lumiac-comparison.md).

## Clone the project

```powershell
git clone --recurse-submodules https://github.com/SDNT8810/Timiac.git
cd Timiac
```

For an existing clone, run `git submodule update --init --recursive` before
rebuilding. Marlin is pinned to upstream 2.1.2.7; the active build starts from
that clean tag and applies Timiac's configuration and motion patches.

The pre-existing edits in the local Marlin working folder are preserved in
`Marlin/patches/legacy-working-tree.patch`. This is an archival snapshot; the
active build does not apply it. To reproduce those legacy working-folder edits,
run `git -C Marlin/marlin-2.1.2.7 apply ../patches/legacy-working-tree.patch`
after initializing the clean submodule.
