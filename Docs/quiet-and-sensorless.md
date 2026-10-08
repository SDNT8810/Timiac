# Microswitch homing only — 2026-09-29.6

Sensorless homing has been retired. Both dev and release builds use the existing
normally closed microswitches, at 120 mm/min. Home all moves six arms on each
board together using Lumiac's routine; each stops at its own switch. Initially
pressed switches release 5 mm before the shared approach (previously 2 mm).
Home selected uses standard single-axis homing with a 5 mm bump.
The old `--sensorless` flag and `marlin_sensorless` target are unavailable.
Bridges reject sensorless firmware identities and M914 tuning commands.

If converting back from DIAG homing:

1. Disconnect board power, including USB.
2. Remove MOTOR0–5 DIAG jumpers and reconnect the NC endstop switches.
3. Build with `python build.py all`, then use the dev or release upload commands
   in [README](../README.md) to update both Octopus boards, both bridges and master.
4. Confirm `FIRMWARE.CUR`, power off and remove each SD card. The master stores
   and streams motion sequences; the card has no runtime role.
5. In dev mode verify each switch lamp changes when pressed before homing.

Quiet holding and normal-speed SpreadCycle remain: 1,000 mA requested running/homing current,
50% holding multiplier (approximately 500 mA at rest), 64 commanded microsteps interpolated to 256, and a
1 mm/s hybrid threshold. Maximum motion feed is 500 mm/min; homing is 120 mm/min.
The planned 24 V / 400 W (16.7 A) supply uses the 24 V chopper timing preset.

## Concurrent homing: endstop did not release

This fault means a switch still reads triggered after the initial 5 mm move
away from home. It does not establish a board failure or a supply reset.
The Octopus diagnostic update lists the affected public axes on the same line:
`Error:Six-arm homing: endstop did not release [axes: A C]`.
With a Board 2 prefix this means A2 and C2. It also logs which switches were
initially pressed and names any interrupted release moves or missing seek hits.
The current package uses 50 mm travel and 5 mm backoff: flash both Octopus
boards, both bridges and the chosen master mode together. The master upload
includes the matching dashboard filesystem.

After correcting the cause, reset the halted Octopus. Before homing, use the
development Endstops panel with stationary arms: released switches should read
open/red, and pressing one should change only its own indicator to triggered/green.
An open or disconnected normally closed circuit also reads triggered. Check
signal/GND, NC contacts, the matching STOP connector, and removed DIAG jumpers
with power disconnected. If the switch indication follows the physical switch
but the arm buzzes or does not move away, insufficient torque or a
mechanical/driver issue is possible. The 5 mm release is fixed; a switch that
stays triggered still faults without automatic retries.
