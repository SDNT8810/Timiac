# Validation: two motors / three ESPs

## TCP send-buffer handling — 2026-09-24.4

The reported failure occurred just after M400/`ok`, at M114. Live read-only
status confirmed master revision .3, bridge revision .2, the same bridge boot
ID before/after reconnection, increasing bridge uptime, and an Octopus restart
counter of 1. Subsequent status captures did not reproduce the reported periodic
idle restart; access later became unavailable. No remote motion was requested.
The log's repeated `External System` reason was from the original ESP boot.

The old sender required a complete packet to fit/write at once and closed TCP
otherwise. The ESP8266's local core implementation confirms that available
send space can be temporarily exhausted while the connection remains open.
The updated sender queues control frames and advances only accepted bytes.
Status and log traffic cannot grow the queue indefinitely. Persistent stalls
retain the 15-second limit, and Stop/reconnection cannot replay queued commands.

Production coordinator and bridge host tests cover zero send space, short
writes, idle telemetry bursts, M114/completion reporting, real write failure,
send-progress timeout, Stop cancelling unsent/partial commands, and accurate
reconnect versus boot logging. Queue tests cover compaction, byte ordering,
capacity limits and timer wrap. Physical operation after flashing is unverified.

All four ESP applications and both master filesystems built successfully;
logs are `.build/link-buffer-*-build.log`. All C++ host tests, 43 Node tests and
eight Python checks passed. `run.py verify` passed for all 13 binaries, and
all four packaged ESP applications contain revision `2026-09-24.4`.
`git diff --check` passed. The Octopus firmware image was not rebuilt or changed.

## Independent manual board testing — 2026-09-24.3

Machine identity and Octopus image remain `TIMIAC2-ed9f017c2e53`. The manual
dashboard and API derive readiness from the selected board, and the coordinator
prepares/commits/waits only for the validated command's participants. Clock
actions and uploaded programs retain their two-board barrier. The development
master application and filesystem are the only required uploads for existing
2026-09-24.2 installations.

Regression coverage exercises all twelve Home selected/Move selected routes
with the other board absent, disabled clock/Home all controls, homing state,
control lease, driver readiness, program reservation and stop gating. The new
host harness compiles the production `runtime.h`, `uart_bridge.h` and
`peer_link.h` with ArduinoJson and simulated network adapters. It exercises
actual prepare/commit/state packets, one-board completion, unrelated-board
fault/disconnection, coordinated completion, lost-participant cancellation,
Stop acknowledgement, an unused bridge with its Octopus offline, late-board
Stop delivery and refusal to replay interrupted work. It does not emulate radio
timing or motor hardware.

- All four ESP applications and both master web filesystems built successfully;
  logs: `.build/independent-board-*-build.log`.
- C++ parser/barrier, Octopus kernel and production coordinator host tests passed.
- All 43 Node tests and eight Python checks passed.
- `python run.py verify` verified all 13 binary hashes and both flash layouts.
  Packaged ESP images contain revision `2026-09-24.3`; the development filesystem
  contains the independent controls and readiness field.
- `git diff --check` passed. No hardware flashing or physical homing was performed.

## Match Lumiac's physical sockets — 2026-09-24

Package at this stage: `TIMIAC2-ed9f017c2e53`, ESP revision `2026-09-24.2`.
The user's latest request supersedes the earlier MOTOR2 bypass. Both boards
now map X/Y/Z/A/B/C to DRIVER0/1/2/3/4/5, matching Lumiac's Configuration.h
and Octopus board header. The endstops stay on STOP0/1/2/3/4/5. MOTOR2_1 and
MOTOR2_2 share DRIVER2; the wiring instructions use MOTOR2_1 for Z.
TMC2209 UART and 32 microsteps remain in effect. The user subsequently confirmed
that both boards are being replaced with new boards; earlier MOTOR2 hardware
faults are no longer the basis for the active layout.

The shared Octopus image, both bridges, both masters and both web filesystems
built successfully (`.build/lumiac-sockets-build.log`). Compile-time checks
verify all six resolved STEP/DIR/ENABLE/UART and endstop assignments. All 41
Node tests, eight Python checks and the C++ controller/parser tests passed;
`python run.py verify` verified all 13 images and both master flash layouts.
Dashboard tests also check all twelve visible socket labels against the new
mapping. Labels now use generated machine data instead of fixed HTML values.
`git diff --check` passed. Lumiac's working tree remains unchanged. No boards
were flashed and no motor movement was performed, so physical homing is unverified.

## Lumiac baseline, 32 microsteps and live drivers — 2026-09-24

Built package identity `TIMIAC2-6ac336fa0a01`, ESP application revision
`2026-09-24.1`. Both original Lumiac configuration files were captured from
commit `b250656cc6c2fd1dc9e4ab9d40bd20718aa19aaf`, hashed and adapted as described
in [lumiac-comparison.md](lumiac-comparison.md). The build uses TMC2209 UART,
32 microsteps and 5,760 steps/mm on all six axes, and Lumiac's S-curve motion
profile. Compile-time checks cover the resolved six-axis driver types, motion
and UART pins, switch pins, calibration, current, microsteps, names, driver
mode and retained protections after Marlin pin postprocessing.

- Clean staged Marlin build, both bridge builds, both master builds and both
  dashboard filesystems: passed. Log: `.build/lumiac-32-build.log`.
- C++ host controller/parser tests: passed, including all twelve homing routes,
  rejection of a four-microstep driver report, automatic healthy/failing driver
  rechecks, replacement recovery, partial-report isolation and reboot clearing.
- Node tests: 41 passed, including twelve independent live driver lamps,
  failed-to-passing updates, stale/offline readings and generated 32-step labels.
- Python tests: eight passed, including the calibrated pulse count, pinned
  baseline adaptations and firmware package validation.
- `python run.py verify`: passed for all 13 images, matching machine identities
  and both master flash layouts. `git diff --check`: passed.

No hardware was flashed or moved during this update. A browser rendering check
was unavailable because the in-app browser backend was unavailable; dashboard
behavior was exercised through the DOM tests. These checks do not establish
whether a driver/board is damaged or whether physical homing now succeeds.
The existing 12 V chopper setting is retained pending supply confirmation.

## Live connection investigation - 2026-09-23

Read-only requests to the running master's `/api/status` confirmed that no
Board 1 bridge had joined during the observed boot. Board 2 repeatedly connected
and disconnected, usually about 18 seconds apart, while reporting a matching
Octopus identity but no completed driver report. The available telemetry does
not establish whether the cause is firmware role, bridge restart, serial
traffic or network loss. No motion or configuration requests were sent.

Application revision `2026-09-23.3` adds bridge boot identity/IP, application
revision, uptime, reset reason, duplicate-role rejection logs, link-loss reason,
and the active UART command with its last reply. This is a diagnostic update;
it does not claim to resolve the observed disconnect. Hardware identity,
protocol and the Octopus firmware stay unchanged. The status UI distinguishes
a bridge which has never joined from one whose last report is stale.

## UART recovery follow-up - 2026-09-23

The screenshot reports Z1/MOTOR3 with no UART response and Board 2 with an
acknowledgement timeout. Corrected the bridge path which incorrectly escalated
a missing drain reply to a permanent Marlin halt. Recovery keeps motion blocked,
retries only M410/M400/M118, and requires a distinct marker plus its own `ok`
before a fresh firmware and driver check. Confirmed Marlin halts remain latched.
Driver rows now hide cached settings when UART fails and label configured
current explicitly. Regression checks cover these reported conditions and
late replies. Firmware pin assignments and the Octopus image are unchanged.

## Generation 2 rebuild — 2026-09-23

The current implementation and physical commissioning limits are described in
[rebuild.md](rebuild.md). Entries below this section are historical records.
The rebuild starts from the clean Marlin tag, explicitly uses SpreadCycle on
all six drivers, and compiles pin/current/microstep/mode assertions into the
Octopus build. The allocation-free UART kernel replaces the old bridge queue.
It verifies firmware and driver reports, splits homing by axis, drains replies
after Stop, and retains hard faults until reset. Protocol 5 refuses mismatched
ESP builds. Both master modes, both bridges and the shared Octopus image build.
`python run.py verify` checks all 13 images and both ESP32 flash layouts.

Host kernel tests cover all twelve parser-to-UART routes, preflight publication,
wrong firmware, partial reports, each failed driver, mixed driver modes, delayed
acknowledgements, Stop, first-fault retention and switch telemetry. The previous
Arduino-mock UART tests have been replaced by these tests of the actual kernel.
Dashboard tests include the new per-axis driver table. Physical motor tests
have not been performed; a clean compile is not evidence of physical homing.

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
