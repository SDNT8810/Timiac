# Timiac: twelve motors, three ESP modules

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

The Octopus build follows the latest existing single-motor configuration:
**Octopus V1.1 / STM32F446, TMC2209 UART, X/MOTOR0, 600 mA RMS,
4 microsteps, 720 steps/mm, 0–48 mm travel, 11.67 mm/s (700 mm/min)
maximum speed, 20 mm/s² acceleration**. The minimum home input uses the
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

The Marlin source is generated in .build/marlin-two-motor. The ready-to-flash
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
python run.py master --port COM5          # Development dashboard
python run.py master_release --port COM5  # User dashboard
~~~

Both serve **http://192.168.4.1/** on the same Timiac Wi-Fi network. The user
dashboard contains Home, Open, Close, Show time, a live RTC readout, and a
date/time setting box. Stop uses a non-latching quick-stop; wait for both boards
to report back, then Home before moving again. Control is acquired automatically,
with one device controlling at a time. The development G-code and
program endpoints are disabled in release mode.

Development mode includes the same clock actions, RTC time setting, live hand
distance and automatic hand gestures, alongside manual control and G-code
uploads. Its buttons require **Take control**. Both dashboards share clock and
sensor display code and use the same firmware action/preset implementations.

- **Home:** homes all twelve axes to **0 mm**, which is physically fully open.
  Required before moving after startup or a motor-board restart.
- **Open:** moves all arms to **0 mm**, retaining their homed state.
- **Close:** moves all arms to the configured maximum stroke (currently **48 mm**).
- **Show time:** reads the DS3231 and moves to that time's pose once per press.
  Actuator targets are **24 mm** for the hour arm, **0 mm** for the minute arm,
  and **48 mm** for all other arms (closed).
  Minutes use five-minute steps, rounded down. When hour and minute overlap,
  the fully open minute arm takes precedence. The physical mechanism is inverted
  relative to the simulator's extension coordinates. It does not start an
  automatic clock animation or periodic arm movement.
- **Struggle:** runs `Simulator/gcodes/circular-struggle.gcode`, including both
  circular passes and the final 10% extension. It requires homing but not an RTC.
- **Next 5 min:** advances the arm pose to the next five-minute boundary, just
  like the simulator's Next 5 min control (13:58 becomes 14:00).
- **Next hour:** adds one hour, runs `Simulator/gcodes/hour-movement.gcode`, and
  replaces its example 01:00 ending with the selected time (13:35 becomes 14:35).

The next-time controls advance from the last arm time, or the RTC if no arm time
is selected. Repeated presses continue advancing; midnight wraps normally.
They **do not change the DS3231**. The large clock continues showing RTC time;
the smaller Arms label shows the selected pose. Show time returns to the RTC pose.

Animations run on the master and continue if the phone disconnects. Each step
waits for both boards; Stop or a board fault cancels the remaining sequence.
The dashboard shows progress and disables other movement controls until completion.
Home all axes before starting either animation.

The animation source files are converted automatically when building the master.
The old `A B C D E H I J K L U V` axis order becomes
`X1 Y1 Z1 A1 B1 C1 X2 Y2 Z2 A2 B2 C2`. Simulator extension is converted to
actuator position as `stroke × (1 − extension / 100)`: 100% becomes 0 mm,
0% becomes 48 mm. Feeds scale by stroke/100 and are capped at the hardware limit.
Ready-to-run examples using the current 48 mm stroke and 700 mm/min limit are
generated in `gcodes/circular-struggle.gcode`, `gcodes/hour-movement.gcode`, and
`gcodes/curious-creature.gcode`.
Edit the simulator source files and rebuild to change these animations.

Manual moves, board-qualified hardware G-code, bridge UART commands, and Octopus
position reports all remain millimetres from the existing home endstop. They
are already actuator coordinates and receive no additional inversion. Positive
relative motion closes an arm; negative motion opens it. Homing direction,
endstop wiring and 720 steps/mm calibration remain unchanged. Homing now uses
675 mm/min (50% above the preceding 450 mm/min setting), with a 180-second
acknowledgement/completion budget. Marlin uses its normal serial command path;
there is no custom host watchdog or remote-reset command.
Reload the updated hardware animation files before uploading older programs.
`gcodes/small-wave.gcode` now uses board-qualified hardware names and first closes
all arms; the 1 mm smoke test remains a small actuator move from home and back.

The clockwise arm order, starting at **12 o'clock**, is
**X1, Y1, Z1, A1, B1, C1, X2, Y2, Z2, A2, B2, C2**. At 03:30, A1 is halfway
open and X2 fully open. Mount/wire the arms in this order.

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
has valid time. Home/Open/Close still work without an RTC. Booting or flashing
does not overwrite the RTC, and no movement starts on boot.

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

## Hand proximity: AJ-SR04M

In both master modes, an approaching hand at **20-49 cm** starts
`Simulator/gcodes/curious-creature.gcode`, with the same twelve-axis mapping,
48 mm stroke scaling and hardware feed limits as the other animations.
The converted manual example is `gcodes/curious-creature.gcode`.

Both dashboards show **Hand distance** in centimetres below the clock,
refreshing about once per second, including before homing and during movement.
Readings below 50 cm show **Near**; others show **Clear**. Missing or invalid
readings show **No valid echo**, and disconnecting clears the displayed value.

Use **GPIO18 for TRIG/RX**, **GPIO19 for ECHO/TX**, **3V3 for VCC**, and **GND**.
The sensor must use **Mode 1 (R19 open)**. For the RTC, SDA remains **GPIO21**
and SCL remains **GPIO22**. See the full [wiring guide](Docs/wiring.md), including
the ECHO level divider required if the ultrasonic module is powered from 5 V.

Home all axes, clear the sensor beyond 60 cm (or wait about 1.2 seconds with no
echo), then bring the hand within 50 cm. Three consecutive near readings trigger
one animation. It needs no phone or RTC. Withdraw the hand after completion
before triggering again; an object held close does not repeatedly start it.
Gestures during other movement or a fault are ignored. Stop remains available.
The AJ-SR04M's documented 20 cm blind zone means sensing closer than 20 cm
cannot be relied on.

Automatic gestures are enabled in both master modes by default. Configure
`TIMIAC_PROXIMITY_ENABLED`, `TIMIAC_SONAR_TRIG`, and `TIMIAC_SONAR_ECHO` in
`ESP_Timiac/include/config.local.h` and rebuild to override these defaults.

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
2. Power both Octopus boards and all three ESPs. Connect your phone to
   **Timiac / 12345678** and open **http://192.168.4.1**.
3. Wait for both board cards to show Ready, then press **Take control**.
   Multiple phones can observe; one phone at a time can issue commands.
   Every connected phone can press Stop.
4. Use Firmware, Drivers, and Endstops to verify both boards. Check every
   local X/Y/Z/A/B/C sensor reports open when released and TRIGGERED when actuated.
5. Home and test **one axis at a time**, checking its direction and sensor
   operation. Do not use the all-axis test until all twelve axes are commissioned.
6. After Stop or a recoverable communication fault, wait for both boards to
   report back, then Home. Interrupted motion is never replayed.

No homing or movement starts on boot. A physical power cutoff is still
needed; software shutdown can release motor torque.

## Board status and faults

Both dashboards distinguish **ESP offline**, **Octopus offline**, **Homing**,
**Moving**, **Stopped**, **Home required**, and **Ready**. Fault details identify
the affected board. The ESP bridges retry Wi-Fi/TCP connections automatically.
A temporary bridge-link loss during G28 lets the local Marlin homing command
continue; an interrupted ordinary motion stays stopped.

Stop and automatic bridge aborts send Marlin's non-latching quick-stop `M410`.
The bridge clears trusted positions, reconnects, and requires Home before the
next movement. A board already showing `Printer halted. kill() called!` was
stopped by an old `M112` and still needs one final power cycle.
Homing commands and the overall Home operation have a 180-second limit;
ordinary operations retain their existing limits.

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

## Remote dashboard at timiac.it

The included [CloudGateway](CloudGateway/README.md) provides a remote release
dashboard without opening a router port. The master makes a certificate-checked
outbound HTTPS connection to Cloud Run; administrators sign in with Google and
the service checks their Gmail address against an allow-list. Remote access is
disabled until its Cloud Run URL, per-device secret, and CA certificate are set
in the master's untracked `config.local.h`. Provide the Cloudflare and Google
Cloud credentials when ready to deploy `timiac.it`.

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
`A`, `B`, and `C`; each bridge changes them to Marlin's `I`, `J`, and `K` only
when it writes its local UART command.

| Timiac names | Local Marlin | Octopus motor socket |
|---|---|---|
| X1 / X2 | X | MOTOR0 |
| Y1 / Y2 | Y | MOTOR1 |
| Z1 / Z2 | Z | MOTOR2 |
| A1 / A2 | I | MOTOR4 |
| B1 / B2 | J | MOTOR5 |
| C1 / C2 | K | MOTOR6 |
