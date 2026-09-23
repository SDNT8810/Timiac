# Timiac Octopus single-motor endurance firmware

This project contains a pinned Marlin 2.1.2.7 X-only build for one motor. It
homes against an X-min sensor, moves slowly between X0.5 and X48, and limits the
configured X travel to 48 mm. `run.py` builds and verifies
`firmware.bin`, then copies the firmware and all test G-code files to a
microSD card. The selected startup mode is installed as `/auto0.g`. `rup.py`
remains available as an alias.

## Hardware assumptions

- BIGTREETECH Octopus **V1.1** with an **STM32F446ZET6** MCU.
- One **TMC2209** installed in `MOTOR0` / X, with the UART jumper fitted.
  Firmware sets **4 microsteps**, UART address **0** (MS1/MS2 low), and
  **600 mA RMS**. Internal interpolation is enabled. `M122` reports the
  connection and driver settings after flashing.
- One motor connected to the X motor output.
- A normally-closed home switch on `X_MIN`. Use its COM and NC contacts
  between signal and GND (released LOW, pressed HIGH). The internal pull-up is
  enabled. Released must report `open`, and pressed must report `TRIGGERED`
  with `M119`.
- Motor power and active cooling appropriate to the driver and motor.
  UART now makes `X_CURRENT` effective; 600 mA RMS is a configured starting
  value, not a maximum-current setting or a verified motor rating.
- 900 X steps/mm at 4 microsteps for the replacement screw. The screw lead
  changed from 1.5 to 1 mm per revolution, so the previous 600 steps/mm is
  multiplied by 1.5 / 1. This scales the existing calibration; physical travel
  has not been independently measured. The original TB6600 firmware
  configuration did not program or read TMC2209 microsteps, so the initial
  calibration assumed its standalone setting was 8 microsteps.

If any of these differ, edit
`Marlin/marlin-2.1.2.7/Marlin/Configuration.h` and rebuild. For a lead screw,
calculate steps/mm as
`(motor steps/revolution x microsteps) / millimetres/revolution`. A wrong
steps/mm value means the mechanism will not travel the requested distance. A
wrong MCU or board
selection means this image is not suitable for the board.

## Wiring and commissioning

Do all wiring with both USB and motor power disconnected. Fit mechanical hard
stops, a fuse, a reachable physical emergency stop, and cooling for the board,
driver, motor, and power supply. An indefinitely moving machine should not be
left unattended until the short test and a supervised thermal soak have passed.

1. Insert a FAT32 microSD card in the computer and run:

   ```powershell
   python .\run.py --sd E:\
   ```

   The default `forever` mode installs the continuous homed test as
   `/auto0.g`. To install a no-motion sensor check instead, add
   `--autostart check`.

   Replace `E:\` with the reader's actual drive. If exactly one removable drive
   is present, `python .\run.py` detects it automatically. Existing files with
   the same names are preserved under `TIMIAC_BAK` before replacement.
   Some USB card readers report as fixed disks; only after checking the drive
   letter, add `--allow-fixed-drive` to permit that case.
2. Safely eject the card. With the Octopus powered off, insert it into the
   onboard slot and leave it inserted.
3. When power is applied, the Octopus bootloader flashes `firmware.bin` and
   normally renames it to `FIRMWARE.CUR`. Marlin then mounts the same card and
   automatically starts `/auto0.g`.
4. In the terminal, verify the firmware and the switch:

   ```gcode
   M115
   M119
   M122
   ```

   `M115` should identify `TIMIAC TMC2209 X 48MM 4USTEP`. `M122` should report
   a successful X driver connection and 4 microsteps. Run `M119` once with the
   switch released and once while pressing it. The required states are
   `open` when released and `TRIGGERED` when pressed. Do not issue `G28` or
   start a test if these states are reversed.

5. Start the supervised ten-cycle smoke test from the same card:

```gcode
M21
M23 SMOKE10.GCO
M24
```

The smoke file homes X, moves to X0.5, completes ten X0.5-to-X48-to-X0.5 cycles at
6.16 mm/s, and disables the motor. To make this smoke test start automatically
on boot, prepare the card with:

```powershell
python .\run.py --sd E:\ --autostart smoke
```

Only after the sensor, direction, movement, TMC2209 current, and temperatures
have been verified, prepare continuous power-on operation with:

```powershell
python .\run.py --sd E:\ --autostart forever
```

The forever file waits ten seconds, homes X, moves to X0.5, and loops
X0.5-to-X48-to-X0.5 until stopped.

Send `M524` to abort an SD test normally or `M112` for a firmware emergency
stop. Do not rely on software alone for personnel or fire safety.

The X-min switch removes the need to position the mechanism manually at zero
before every boot. It does not detect missed steps after homing. Removing or
renaming `/auto0.g` disables power-on startup.

## Motion

`FOREVER.GCO` uses `M808 L0`, Marlin's infinite SD-repeat marker. Test motion
uses 369.6 mm/min (6.16 mm/s), X acceleration is limited to 20 mm/s^2, and homing
uses 134.4 mm/min (2.24 mm/s). Both test speed and homing speed are reduced by
20% from the previous 7.7 mm/s and 2.8 mm/s settings. The commanded loop span is
47.5 mm, from X0.5 to X48, with no programmed dwell. The 0.5 mm clearance is the
loop's near-home endpoint; the homing bump remains 2 mm and there is no added
post-homing backoff. Firmware limits X to 0 through 48 mm and caps X feedrate
at 6.16 mm/s. These millimetre distances depend on the steps/mm calibration
matching the driver microstep setting described above.

Build without touching a card with:

```powershell
python .\run.py --build-only
```
