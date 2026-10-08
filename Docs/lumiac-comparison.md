# Lumiac configuration adapted for Timiac — 2026-09-24

The build now uses the actual two configuration files from the local Lumiac
project at commit `b250656cc6c2fd1dc9e4ab9d40bd20718aa19aaf`. An unchanged copy
is stored in `Marlin/profiles/lumiac-reference/`, together with its provenance
and SHA-256 hashes in `source.json`. Builds verify these hashes and do not need
the sibling Lumiac directory. Lumiac itself has not been edited.

`build.py` extracts the clean Marlin 2.1.2.7 engine, takes these saved Lumiac
configuration files, and applies the explicit adjustments generated from
`machine.json`. The resulting adjustments are inspectable in
`Marlin/profiles/twelve-motor/profile.json`. The configuration format version
in both projects is compatible (`02010206`).

## Motion settings

| Setting | Lumiac normal build | Timiac adapted build |
|---|---|---|
| Axes per board / G-code names | Six / X Y Z A B C | Same |
| Acceleration curve | S-curve | Same |
| Travel acceleration | 25 mm/s² | Same |
| Per-axis acceleration ceiling | 80 mm/s² | Same |
| Junction deviation | 0.003 mm | 0.3 mm, with normalized six-axis corner directions |
| Homing bump / second-pass divisor | 2 / 2 on all six axes | 5 / 2 on all six axes |
| Endstop interrupts / always enabled | On / on | Same |
| Driver mode / interpolation / hold multiplier | SpreadCycle / on / 0.5 | **StealthChop at rest, SpreadCycle above 1 mm/s** / on / 0.5 (.6 microswitch profile) |
| Driver status monitoring / stop on driver error | On / on | Same |
| Direction / home direction | Not inverted / minimum | Same |
| Microsteps | 16 | **64**, interpolated internally to 256 |
| Calibration | 533.33 steps/mm at 16 | **11,520 steps/mm at 64** |
| Maximum travel | 120 mm | 50 mm |
| Maximum feed | 260 mm/s | 500 mm/min (8.333 mm/s) |
| Homing feed | 400 mm/min | 120 mm/min with microswitches only |

Timiac's established calibration was 720 steps/mm at four microsteps. The
mechanical calibration is now stored independently as 180 full steps/mm:
`180 × 64 = 11,520`. This preserves commanded travel when changing microstep
resolution. The 500 mm/min limit reserves interrupt headroom at 64 commanded microsteps.
It does not substitute Lumiac's different
mechanics. A physical measurement is still needed to confirm the calibration
on the installed actuators. At maximum feed this requires 96,000 step pulses/s
on a single moving axis.

Home all additionally ports the current local Lumiac concurrent homing routine,
separately from the older pinned configuration baseline. Each motor stops at
its own switch within one shared seek. See [the patch provenance and tests](../Marlin/patches/README.md).

## Required hardware and controller differences

Lumiac explicitly enables `TMC_USE_SW_SPI` with six TMC5160 drivers: its driver
configuration uses **SPI**. Timiac instead uses six **TMC2209 UART** drivers per
board, each on its own socket UART pin at address zero. Timiac requests 1,000 mA RMS
with 0.11 Ω sense resistance;
Lumiac's 2,500 mA / 0.075 Ω values are not suitable
substitutes. The separate Lumiac high-power environment is not used.

Lumiac uses driver slots **0, 1, 2, 3, 4, 5**, in X/Y/Z/A/B/C order.
Timiac now uses those same slots and endstop connectors on both boards:

| Axis | Driver slot | Motor connector | Timiac UART pin | Endstop connector |
|---|---|---|---|---|
| X | DRIVER0 | MOTOR0 | PC4 | STOP0 / X-STOP / PG6 |
| Y | DRIVER1 | MOTOR1 | PD11 | STOP1 / Y-STOP / PG9 |
| Z | DRIVER2 | MOTOR2_1 or MOTOR2_2 | PC6 | STOP2 / Z-STOP / PG10 |
| A (internal I) | DRIVER3 | MOTOR3 | PC7 | STOP3 / Z2-STOP / PG11 |
| B (internal J) | DRIVER4 | MOTOR4 | PF2 | STOP4 / E0DET / PG12 |
| C (internal K) | DRIVER5 | MOTOR5 | PE4 | STOP5 / E1DET / PG13 |

MOTOR2_1 and MOTOR2_2 are two parallel outputs from DRIVER2; firmware cannot
distinguish which connector holds the motor. Use MOTOR2_1 for Timiac's single
Z actuator and leave MOTOR2_2 empty. DRIVER6/7 and MOTOR6/7 are unused.
See the [BTT Octopus hardware guide](https://global.bttwiki.com/Octopus.html).

Compared with the previous Timiac layout, move each **driver and its motor cable**
as follows with all board power and USB disconnected: Z from slot 3 to 2, A from
4 to 3, B from 5 to 4, and C from 6 to 5. X/Y and all endstop connectors stay
where they were. The earlier MOTOR2 bypass is superseded by this explicit
request to match Lumiac. The user is replacing both boards with new boards;
the earlier damaged boards are no longer the target hardware.

Driver-slot numbering starts at zero. Counting from DRIVER0, the first six
physical driver-module slots are DRIVER0, DRIVER1, DRIVER2, DRIVER3, DRIVER4,
and DRIVER5. The **third driver slot is DRIVER2**, serving both MOTOR2_1 and
MOTOR2_2. DRIVER3 is the fourth slot and serves MOTOR3. The extra MOTOR2 output
does not add a driver slot. Six independently controlled arms need six drivers.
The checked Lumiac source assigns all six channels 0–5; it cannot establish
which connectors were physically populated on the existing Lumiac assembly.

Use the **UART jumper arrangement for TMC2209**, not Lumiac's SPI jumpers for
TMC5160. Keep address zero and the physical-endstop DIAG jumpers removed.
Microsteps are set to 64 through UART, with internal interpolation to 256. Do not copy Lumiac's current or switch
contact polarity just because its socket positions match.

All physical signals are assigned
in the board header before Marlin processes pins. Lumiac's old SPI socket pin
definitions are removed from the adapted Configuration.h. Names on the master
remain X1 Y1 Z1 A1 B1 C1 X2 Y2 Z2 A2 B2 C2.

Other deliberate adaptations:

- Keep Timiac's NC switch polarity (`false`, triggered HIGH) and broken-switch
  validation. Lumiac uses the opposite switch polarity.
- Keep A/B/C as linear actuator coordinates in millimetres; Lumiac's file marks
  them rotational. Copying that flag would change feed calculations and units.
- Use the 24 V chopper timing for the planned 24 V / 400 W (16.7 A) supply;
  Lumiac uses 24 V timing. This setting does not change the supply voltage.
- Keep USART3 and USB at 115200 baud, 192-character command capacity, 600-second
  motor timeout, and homing required after boot or motor release.
- Disable Lumiac's case light, runtime SD features and custom SD-file commands.
  Timiac's master owns programs and requires explicit starts. EEPROM overrides
  remain disabled. Keep emergency serial parsing and serial overrun protection.
- Retain Timiac's G0/G1 guard that checks the commanded axes for homing, allowing
  a single homed arm to move without requiring the other arms to home first.

## Live driver checks and upload

The development dashboard has twelve driver lamps. Each connected idle board
automatically repeats M122 two seconds after its last completed report, whether
the previous check passed or failed. A full diagnostic read adds its own time.
Switch polling and dashboard refresh remain automatic. No control lease or
motion request is needed, and one board can report while the other is offline.
Release mode rechecks drivers every five seconds while idle.

Only a completed report can publish a new result. Offline, expired or moving
boards show unknown/paused readings. A responsive driver with the wrong
microstep setting shows a mismatch and blocks motion. Browser labels and bridge
expectations are generated from the same machine definition, including **64 commanded**
microsteps. A UART pass does not test a driver's motor output; no response can
also mean a bad socket, UART jumper or missing supply.

Disconnect all board power, including USB, before replacing a driver. Reconnect
power afterward; the dashboard rechecks automatically. This is not hot-swap support.

The new machine identity requires a matched update to **both Octopus boards,
both ESP bridges and the master ESP32 with its dashboard filesystem**:

```powershell
python run.py verify
python run.py octopus --sd E:\
python run.py esp1 --port COM5
python run.py esp2 --port COM5
python run.py master --port COM4
```

Repeat the Octopus command for each microSD. Use each module's actual serial
port; the COM numbers above are examples and each bridge must get its own role.
Use `master` to see the development diagnostics. Builds and host tests cannot
establish whether a physical board, motor or driver is damaged or prove homing.
