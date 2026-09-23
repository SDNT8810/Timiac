# Validation: two motors / three ESPs

## Z socket reassignment: MOTOR2 to MOTOR3 - 2026-09-23

- Reassigned Z1 and Z2 from the unreliable physical MOTOR2 socket to MOTOR3
  on both Octopus boards. The resulting six-socket layout is MOTOR0, MOTOR1,
  MOTOR3, MOTOR4, MOTOR5, MOTOR6. A/B/C retain MOTOR4/5/6 and all twelve
  public axis names remain unchanged.
- The generated Marlin profile replaces all Z channel signals after the Octopus
  board's default pin processing: step PG4, direction PC1, enable PA0, TMC2209
  UART PC7. A compiled assertion checks those five signals, one-Z-axis mode,
  MOTOR0/1 and MOTOR4/5/6, plus all three A/B/C switch inputs.
- Z1/Z2 home switches remain STOP2 / PG10. Before power-up, move each Z
  driver and motor cable to MOTOR3, fit its UART jumper, and remove the
  MOTOR3/Z2 DIAG jumper because its PG11 input is assigned to A1/A2.
- The shared Octopus image builds successfully. Four Python profile/flash
  tests, both bridge regression suites, and all 34 dashboard tests pass.
  The package hash was verified after rebuilding.

## Reported A homing fault and fault retention - 2026-09-23

- The hardware log contains `echo:Bad A Endstop?` from Board 1. In the active
  Marlin source this is the broken-endstop check after the 2 mm homing backoff;
  it calls `kill()`. This establishes the A input was still reported triggered,
  not whether the cause is wiring, motor motion, or switch release distance.
- Corrected confirmed bridge bugs: queue cancellation erased the original
  fault, and accepting Stop left the old operation barrier active. Faults now
  survive cancellation, the barrier is failed, and a killed board is not polled
  repeatedly or marked ready by a late `ok`. A fresh reset/startup handshake
  restores communication without replaying motion or marking axes homed.
- Both dashboards now show **Reset required** for a halted motor board. The
  peer report carries the original fault, with a fallback for older bridges
  that supplied only an operation error, instead of blank `Board 1:` messages.
- Host tests for both board identities replay the reported fault, the following
  kill message, Stop, late acknowledgements, five minutes without a response,
  and reset/handshake recovery. Existing firmware and all 34 Node tests pass.
- Rebuilt both bridge images, both master modes and both filesystems; verified
  all 13 binary hashes and flash manifests. All three Python tests pass.
- Physical homing remains unresolved until A1 switch and backoff behavior are
  verified on the machine. No axis remapping or endstop protection bypass was
  introduced for this fault.

## Development micro-switch lamps - 2026-09-23

- Added twelve labeled lamps: red for released, green for triggered, gray for
  missing, stale, offline, or busy readings. They use actual M119 responses,
  require no homing/control lease, and are shown only on the development page.
- Host tests pass for both bridge identities: all six independent switches,
  release transitions, malformed/missing responses, acknowledgement boundaries,
  two-second expiry, timer wrap, restart invalidation, opt-in polling and
  telemetry exclusion during preparation/homing. The existing firmware tests
  also pass with assertions enabled.
- All 34 Node tests pass, including twelve-axis lamp routing, observer access,
  stale/missing values, busy boards and disconnection. In-app browser visual
  verification is unavailable in this session (no connected browser).
- Both bridge builds, both master builds and both filesystem builds succeeded.
  Verified all 13 binary hashes and both flash layouts; the switch module is
  included only in the development filesystem. HTTP asset checks and all
  three Python tests pass. Build output is in `.build/endstop-build.log`.
- Physical switch testing still needs the rebuilt bridges and development
  master/filesystem flashed. The Octopus image requires no change for this feature.

## Restored twelve-axis G-code names - 2026-09-23

- Removed the erroneous bridge A/B/C-to-I/J/K UART translation and restored
  A/B/C parsing in M114 position reports. The master names remain
  X1 Y1 Z1 A1 B1 C1 X2 Y2 Z2 A2 B2 C2. The active Marlin profile now explicitly
  sets AXIS4/5/6_NAME to A/B/C; I/J/K remain Marlin's internal driver identifiers.
- A new host test runs the actual UartBridge with a captured serial port for
  each board identity. Before the fix it failed at `G28 A` being sent as `G28 I`.
  After the fix, all twelve individual home routes, mixed moves, release Home,
  Open, Close, Show time, and six-coordinate M114 reports pass.
- Fixed the host test compiler flags: Zig's `-O1` defined NDEBUG and disabled
  the existing assertions. Tests now use `-UNDEBUG`, and both assertion-based
  test sources refuse to compile with NDEBUG. The existing parser, pose,
  animation, malformed-input, timeout and barrier assertions also pass.
- All 32 Node tests and three Python tests pass, including the explicit
  Marlin A/B/C profile contract and the two mocked flash-package tests.
- Built the shared Octopus firmware, both bridge images, both master modes,
  and both dashboard filesystems. Verified all 13 packaged binary hashes,
  both flash manifests, and A/B/C names with rotation disabled in the actual
  generated Marlin configuration. Build output is in `.build/axis-build.log`.
- No physical flashing or motor movement has been performed. The source
  snapshot before this correction is saved in `.build/before-axis-fix-20260923`.

## Stock Marlin homing and internet clock update — 2026-09-22

- Removed the experimental Marlin UART watchdog and remote-reset/Fix code that could issue `M112` while G28 was still running. The staged Marlin directory is recreated on every build, so no old recovery sources remain in a future image.
- This revision mistakenly translated A/B/C to I/J/K on the bridge UART. That
  regression is corrected by the 2026-09-23 update above. Homing remains
  675 mm/min on every local axis, with a 180-second acknowledgement and
  operation deadline.
- The master synchronizes the DS3231 from NTP after an upstream Wi-Fi connection is available and at most every 12 hours while idle. Italy's DST-aware POSIX timezone is the default and can be overridden locally.
- Built the stock shared Octopus firmware, both bridge images, both master modes, and both dashboard filesystems. Firmware parser/native timeout tests, both flash-package tests, and all 32 Node tests passed.
- Physical homing remains to be tested with all five new images. A prior M112 halt cannot be cleared in software; restart that Octopus board once before flashing the corrected stack.
## Inverted mechanism and dashboard parity — 2026-09-22

- Built both master modes, both bridge images, the shared Octopus image, and
  both dashboard filesystems; verified all 13 binary hashes and flash manifests.
- C++ tests passed for every clock pose (1,440 times), Open/Close/Home, all
  animation endings, and native validation of all seven hardware G-code files.
  Generated animation files match built-in UART moves exactly, including the
  hour animation's 01:00 example ending; this checks against double inversion.
- All 27 Node tests passed (24 existing plus 3 development dashboard checks),
  covering inverted simulator absolute/relative conversion, shared readouts,
  clock actions, control locks, RTC saving and stale-data clearing.
- Both Python flashing tests passed. The release package includes the new
  shared `clock-panel.js` module in both master filesystems.
- No physical flashing or motion tests performed. Visual dashboard checking
  is left to the user as requested.

## Calibration and long homing update — 2026-09-21

- Rebuilt the active twelve-axis Octopus image, both master variants, both
  bridges, and both dashboard filesystems successfully.
- Generated Marlin configuration checked: all six local axes use 720 steps/mm
  and 450 mm/min homing; stepper inactivity timeout is 600 seconds.
- C++ host tests passed, including long homing deadlines across millis wrap
  and continued enforcement of the 2-second heartbeat watchdog.
- All 24 Node tests and both Python flashing tests passed. Client tests cover
  a 10-minute homing operation, eventual timeout, and the shorter move deadline.
- All 13 release SHA-256 checksums and both master flash manifests verified.
- Physical flashing, carriage calibration, and full-stroke homing remain to
  be tested on the clock. The reported stop at roughly 120 seconds has no
  captured error yet; the increased limits do not establish its root cause.

## Historical two-motor validation

Checked on 2026-09-19 in the Windows workspace.

| Check | Result |
|---|---|
| Shared X-only Marlin / STM32F446ZE_btt | Compiled successfully |
| timiac_master | Compiled successfully |
| timiac_board1 | Compiled successfully |
| timiac_board2 | Compiled successfully |
| Master LittleFS web image | Built successfully |
| C++ parser, native gate, watchdog, barrier | Passed, including 30,000 malformed-input samples |
| Node automated tests | 15 passed (5 current three-ESP tests, 10 preserved legacy tests) |
| Browser JavaScript / demo syntax | Passed |
| Five release-file SHA-256 hashes | Verified |
| Physical flashing / motor tests | Not performed |
| Browser visual verification | Unavailable: browser runtime reported no connected browsers |

## Build memory

| Image | Static RAM | Program flash |
|---|---:|---:|
| Master ESP | 37,720 / 81,920 bytes | 388,129 / 1,044,464 bytes |
| Board 1 bridge | 33,632 / 81,920 bytes | 319,705 / 1,044,464 bytes |
| Board 2 bridge | 33,632 / 81,920 bytes | 319,705 / 1,044,464 bytes |
| Shared Octopus | 8,124 / 131,072 bytes | 79,460 / 524,288 bytes |

These are linker figures, not measurements of peak runtime heap.
LittleFS uses the configured 1,024,000-byte image.

The builds use PlatformIO 6.1.19, ESP8266 platform 4.2.1 / Arduino core 3.1.2,
ArduinoJson 6.21.5, and the existing Marlin 2.1.2.7 source. The older ESP
packaging tool emits Python 3.14 escape-sequence warnings; binary generation
succeeds. Bridge builds also report unused master-only web functions.

The disk filled during compilation. Generated backup build files and the
finished Marlin intermediate build were cleaned using PlatformIO after
preserving the release firmware. Generated build directories were compressed.
The usable shared image is firmware/two-motor/firmware.bin; build.py regenerates
intermediates as needed.

## What the checks establish

- Exact input G90 X1 10 X2 35 F350 becomes local X10 / X35 at F350.
- The parser's six-axis setting maps all twelve future names to the right board.
- The active one-axis setting rejects uninstalled axes, malformed numbers,
  repeated words, line injection, invalid feed, out-of-travel targets, and
  unsupported configuration/shutdown commands.
- Failed parser calls leave modal state unchanged. Relative moves require
  known positions and remain within configured travel.
- Native command validation also protects bridge preparation.
- The shared completion barrier requires preparation/completion from both
  boards. Failed barriers cannot subsequently become complete.
- The watchdog accepts only complete exact heartbeat comment lines and handles
  the millis counter wrapping.
- HTTP tests cover exclusive control, homing, independent X routing, source
  upload validation, playback without phone polling, faults, recovery, and
  refusal to replay an interrupted program.
- The hardware client includes session/lease/sequence data and does not retry
  uncertain motion POST requests.
- The generated Marlin configuration preserves the latest 900 steps/mm,
  4 microsteps, 48 mm travel, NC endstop and 600 mA current settings.
  Source filtering explicitly includes the added watchdog implementation.

The HTTP demo runs the real C++ G-code parser but simulates motor completion.
These tests do not execute the ESP Wi-Fi/UART state machines or prove physical
motor movement, radio timing, multi-phone peak memory, driver cooling,
homing reliability, lost steps, or stop latency. Complete the commissioning
steps in [wiring.md](wiring.md) before larger movements.

## Reproduce

From Timiac:

~~~powershell
python -m pip install -r requirements.txt
python build.py
python -m pip install ziglang==0.13.0
python tests/run_firmware_tests.py
node --test tests/*.test.mjs
~~~

The original motor-tests files, run.py/rup.py, original Marlin configurations,
and simulator were not changed. The prior ESP/web/docs sources were backed up
under .build/before-three-esp before this update.
