# Twelve-motor wiring and commissioning

## Master ESP32-WROOM-32: RTC and hand sensor

These are **GPIO numbers printed on the development board**, not header positions.
Both modules connect to the master ESP32; neither connects to an Octopus bridge.

| Module pin | Master ESP32 connection |
|---|---|
| DS3231 VCC | 3V3 |
| DS3231 GND | GND |
| DS3231 SDA | GPIO21 |
| DS3231 SCL | GPIO22 |
| AJ-SR04M VCC | 3V3 |
| AJ-SR04M GND | GND (shared with the master and RTC) |
| AJ-SR04M TRIG / RX | GPIO18 |
| AJ-SR04M ECHO / TX | GPIO19 |

GPIO18/19 are normally on the same development-board header side as GPIO2/4
and the RTC pins. Check the printed GPIO labels because board layouts vary.
Avoid GPIO6-11: they connect to the module's flash. GPIO2/4 also support digital
I/O, but GPIO2 is a boot-strapping pin that must be low for serial flashing;
GPIO18/19 avoid this restriction. See the
[ESP32-WROOM-32 pin and boot specifications](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf).

Leave the RTC SQW and 32K pins unconnected. Fit the appropriate RTC backup battery.
The DS3231 supports 3.3 V operation: [manufacturer datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds3231.pdf).

If the clock is unavailable, keep the RTC at 3.3 V and open
**http://192.168.4.1/api/rtc** after flashing the latest master image. This returns
cached diagnostics without interrupting motor control. The bus is explicitly
configured at 100 kHz. `stage: address_probe` with `i2cCode: 2` means address
0x68 did not acknowledge; code 5 means a timeout. `present: true` with
`stage: register_read` means the address responded but data could not be read;
check `expectedBytes` versus `receivedBytes`. The response also identifies the
actual SDA/SCL GPIOs and the sampled line levels after the transaction.
`stage: validate_time` with a lost-power message means communication works and
the date/time must be saved from the dashboard. These diagnostics do not by
themselves identify whether a failure is in the module, bus, or connections.

Configure the **AJ-SR04M in Mode 1, with R19 open/unpopulated**, for TRIG/ECHO
operation. The module manual specifies a 3-5.5 V supply, so use 3V3 for this direct
connection. It also specifies a **20 cm blind zone**; present the hand roughly
**20-49 cm** in front of the probe. See the
[AJ-SR04M manual](https://mantech.co.za/Datasheets/Products/AJ-SR04M-200925A.pdf).

If you instead power the AJ-SR04M from **5 V**, level-shift ECHO before GPIO19.
For example: `ECHO -- 10 kOhm -- GPIO19 -- 20 kOhm -- GND` divides a 5 V
echo to about 3.33 V. Do not connect a 5 V ECHO signal directly to the ESP32.
The [ESP32 electrical specifications](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
limit the high input level to VDD + 0.3 V.

The release firmware samples every 120 ms without blocking for the echo. After
homing and while idle, clear the sensor first: three readings at least 60 cm away,
or ten no-echo readings (about 1.2 seconds), arm the trigger. Three consecutive
valid readings below 50 cm then run **curious-creature.gcode** once. This detects
proximity, not the direction or shape of a hand gesture. Very close readings in
the blind zone and invalid echoes are ignored.

Gestures during homing, another movement, a pending uploaded program, a fault,
or a disconnected board are discarded. They are never queued to run later.
After the animation finishes, withdraw the hand to rearm. Stop cancels it through
the normal board-stop mechanism. No RTC is required for this animation.
The release dashboard shows the measured distance while idle and animation
progress while running.

Automatic proximity gestures are enabled by default in `master_release` and
disabled in `master`. Override `TIMIAC_PROXIMITY_ENABLED` with 0 or 1 in
`ESP_Timiac/include/config.local.h` to change this. Pin overrides are
`TIMIAC_SONAR_TRIG` and `TIMIAC_SONAR_ECHO`; rebuild after changing them.

## Per-axis commissioning baseline

Both boards use the identical six-axis image, adapted from Lumiac's working
configuration for TMC2209 UART and Timiac's mechanics. See
[lumiac-comparison.md](lumiac-comparison.md); confirm direction and calibration
on each physical mechanism before production use.

| Setting | Value |
|---|---|
| Board / MCU | BIGTREETECH Octopus V1.1 / STM32F446ZET6 |
| Motor / driver | X/Y/Z/A/B/C in G-code; Marlin internally calls the last three drivers I/J/K |
| Reported supply / chopper timing | 24 V / 7 A, CHOPPER_DEFAULT_24V |
| Current / microsteps | 800 mA RMS requested / 64 commanded, interpolated to 256 |
| Steps / travel | 11,520 steps/mm / 0–50 mm |
| Maximum feed | 8.333 mm/s = 500 mm/min |
| Acceleration | S-curve: 25 mm/s² travel, 80 mm/s² per-axis ceiling |
| Homing speed / direction | 120 mm/min, concurrent physical microswitch homing / toward each axis minimum |
| Stepper inactivity timeout | 600 seconds |
| Home input | Per-axis input in the table below, pull-up, normally closed switch to GND |
| Endstop inversion | false: released LOW, triggered HIGH |
| UART / USB | USART3 and USB, 115200 baud |
| Runtime SD / EEPROM settings | disabled |

The home endstop is at the **fully open** arm position: actuator **0 mm**.
Moving toward **50 mm closes** the arm. Keep the working motor directions and
minimum-endstop wiring; the master reverses visual clock/animation targets.
Both bridges and Octopus boards execute native actuator coordinates directly.

The active twelve-motor profile uses the reported actuator measurement on all
arms: 40 mm commanded produced 50 mm from home, so steps/mm becomes
`900 × 40 / 50 = 720` at four microsteps. At the selected 64 commanded microsteps,
the equivalent calibration is `720 × 64 / 4 = 11,520` steps/mm.
The driver interpolates internally to 256 microsteps. The 500 mm/min maximum
requires 96,000 step pulses/s; homing retains its 120 mm/min feed.
Home all uses Lumiac's independent per-motor switch stopping on both boards. Sensorless homing is removed in both modes.
Remove any MOTOR0–5 DIAG jumpers and reconnect NC switches with power off.
The generated image identifies itself
with the `TIMIAC2-` machine hash shared by all ESP images.

Flash the new `firmware.bin` to both Octopus boards, plus the updated master
and both bridge ESP images for the longer homing budgets. Home first, then
check a 10 mm move before rechecking 40 mm of actuator travel from zero.
These calculated settings still need a physical measurement after flashing.

M119 must show open when the switch is released and TRIGGERED when
actuated. Disconnecting an NC switch should also report TRIGGERED.
Verify each of the twelve switch inputs with stationary motors before running G28.
Install the driver's UART jumper; remove the DIAG jumper for a physical sensor.

### A/B/C homing troubleshooting

The latest profile restores `INVERT_I_DIR`, `INVERT_J_DIR`, and `INVERT_K_DIR`
to `false`, undoing the preceding direction reversal. This is a rollback,
not confirmation of the actual motor wiring or physical home direction.

The wiring counts the first endstop input as 1: X/Y/Z/A/B/C use the first six
inputs. A uses the 4th input (PG11 / STOP3), B the 5th (PG12 / STOP4), and
C the 6th (PG13 / STOP5). The previous PG12/PG13/PG14 mapping was one input
too high for this wiring. The current Lumiac layout uses MOTOR3/MOTOR4/MOTOR5 for A/B/C;
the motor output number does not select the endstop input.

For these physical switches, remove any DIAG3/DIAG4/DIAG5 jumpers connecting
driver diagnostic outputs to the three selected endstop inputs. Switch off
board power before changing jumpers. Keep the driver UART jumpers installed.

With all motors stationary, run M119 with each switch released and pressed.
Only the corresponding `a_min`, `b_min`, or `c_min` line should
change from `open` to `TRIGGERED`. If it does not change, check the connector,
signal/GND wiring, switch contact and DIAG jumper before attempting motion.

`echo:Bad A Endstop?` specifically means Marlin still reads the A home input
as triggered after the 5 mm homing backoff. The message confirms it recognized
axis A. This can happen if the switch is held, another arm's switch is connected
to the A input, the circuit is open, the driver/motor does not back off, or the
mechanism has not released the switch. The message alone does not distinguish
those causes. On Board 1 the A1 input is STOP3 / PG11 and its motor is now MOTOR3.

This error calls Marlin's `kill()` and requires the affected Octopus RESET
button or a power cycle. After reset, use the development switch lamps with
stationary motors: A1 should be red when released and green when pressed, and
only A1 should change. Do not hold the switch during an automatic homing
backoff. An NC switch or wire left disconnected reads triggered too.
The corrected bridge preserves this cause and shows **Reset required**;
Stop acknowledgements and later UART timeouts cannot erase it. Homing and
switch wiring still need physical verification after the fault is corrected.

Marlin's initial homing search is 1.5 times the configured 50 mm travel
(75 mm commanded). Do not wait for that search to finish if the motor moves
away from its sensor. `VALIDATE_HOMING_ENDSTOPS` stops on a missed switch;
`DETECT_BROKEN_ENDSTOP` now also stops if a switch remains triggered after
the 5 mm backoff. Neither check can establish that the physical direction
or wiring is correct.

## Three ESP-12S modules

The master has **power only**; do not wire it to either Octopus serial port.
Each bridge connects only to its own Octopus:

| Bridge signal | Octopus signal |
|---|---|
| GPIO1 / TXD0 | PD9 / USART3 RX |
| GPIO3 / RXD0 | PD8 / USART3 TX |
| GND | GND |
| VCC | regulated 3.3 V |

Signal levels are 3.3 V. Do not put 5/12/24 V on an ESP or MCU signal pin.
A bare ESP needs adequate supply decoupling and Wi-Fi burst current,
EN/reset high, GPIO0/GPIO2 high, and GPIO15 low for normal boot.
Check the ESP-12S module's built-in resistors and actual Octopus socket
orientation. The pin table identifies electrical signals, not breakout dimensions.

All ESP builds assume 4 MB flash. Use UART at 115200, 8N1.
Do not connect two TX outputs together. Disconnect the Octopus UART when
flashing a bridge with a USB-UART adapter. Pull GPIO0 low during reset to
enter flashing mode, then release it for normal operation.

An optional physically rewired setup can set TIMIAC_SERIAL_SWAP = 1:
GPIO15/TX → PD9 and GPIO13/RX ← PD8. GPIO15 must remain low during reset.
This avoids ROM boot messages on the bridge's control TX but does not match
the default UART0 socket wiring.

## Flashing order

1. Use the same firmware/two-motor/firmware.bin on both FAT32 microSD cards.
   Boot each Octopus and confirm FIRMWARE.CUR. Bootloader SD flashing still
   works although runtime SD support and auto0.g execution are disabled.
   After confirming the flash, power off, remove the card, and power on again.
2. Flash timiac_master and its filesystem; flash timiac_board1 and
   timiac_board2 onto their respective bridges. Instructions are in [README](../README.md).
3. Connect the UART links and power all boards. Join Timiac / 12345678.
4. Open http://192.168.4.1, Take control, and inspect M115/M122/M119 logs.
   Both boards must report AXIS_COUNT:6 and answer normal Marlin status queries.
5. Home each local X/Y/Z/A/B/C axis separately, then test a 1 mm move on each motor.
6. Under supervision, verify Stop and interrupted-link behavior before larger
   programs. Stop uses M410 for ordinary movement and M112 during homing;
   interrupted homing requires an Octopus reset. Keep loads supported and
   a physical motor-power cutoff available.

After an ordinary Stop, wait for both bridges to reconnect, then Home again.
A board showing `Printer halted. kill() called!` needs its reset button or a
power cycle. Do not send USB motion or settings commands while
using the network controller.

## Active axis mapping

| Master Board 1 | Master Board 2 | Local Marlin | Motor socket | Endstop counted from 1 | Home input |
|---|---|---|---|---|---|
| X1 | X2 | X | MOTOR0 | 1st | STOP0 / X-STOP / PG6 |
| Y1 | Y2 | Y | MOTOR1 | 2nd | STOP1 / Y-STOP / PG9 |
| Z1 | Z2 | Z | MOTOR2_1 / DRIVER2 | 3rd | STOP2 / Z-STOP / PG10 |
| A1 | A2 | A | MOTOR3 / DRIVER3 | 4th | STOP3 / Z2-STOP / PG11 |
| B1 | B2 | B | MOTOR4 / DRIVER4 | 5th | STOP4 / E0-DET / PG12 |
| C1 | C2 | C | MOTOR5 / DRIVER5 | 6th | STOP5 / E1-DET / PG13 |

The active mapping now matches Lumiac: DRIVER0 through DRIVER5. Z uses the
MOTOR2 channel; MOTOR2_1 and MOTOR2_2 are parallel outputs of the same driver.
Use MOTOR2_1 for the single Z actuator and leave MOTOR2_2 empty. MOTOR6/7 are
unused. UART pins are PC4, PD11, PC6, PC7, PF2 and PE4 in X/Y/Z/A/B/C order.

To change from the preceding bypass layout, disconnect all board power and USB,
then move each driver and motor cable: Z 3→2, A 4→3, B 5→4, C 6→5. X/Y remain
on MOTOR0/1. The home switches do not move. Fit TMC2209 UART jumpers at all six
occupied slots; Lumiac uses a different SPI jumper arrangement. The user is
replacing both boards with new boards, so the old damaged channels no longer
determine this layout. Counting from DRIVER0, the third driver slot is DRIVER2
and serves both MOTOR2 connectors; the fourth slot is DRIVER3 and serves MOTOR3.

Pin references are in the vendored
Marlin/src/pins/stm32f4/pins_BTT_OCTOPUS_V1_common.h.
ESP serial behavior is documented in the
[ESP8266 core reference](https://arduino-esp8266.readthedocs.io/en/3.1.2/reference.html#serial).
