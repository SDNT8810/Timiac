# Cooler motion validation - 2026-10-08.1

Matched package **TIMIAC2-03b3b6e145e4**, protocol 7, trajectory capability 2.
All twelve axes request 1,000 mA RMS for running/homing and retain 50% holding.

Validation completed:

- Shared Octopus, both bridges, dev/release masters and both web filesystems
  build successfully. `python run.py verify` validates all package images.
- 15 Python tests and 48 dashboard/control tests pass. Host firmware, both
  production coordinator modes and bridge tests pass, including a simulated
  24-hour clock cycle and all 720 homing switch orders.
- Motion checks test Hour and Struggle from fully open, halfway and fully closed
  positions across midnight, overlapping hands and different clock targets.
  Both boards have matching nominal frame durations, all motor speeds remain
  bounded, and final poses match the requested time or Struggle band edge.
- Struggle vector feed is capped at 400 mm/min (80% of the former 500), with
  20 mm/s? acceleration. Every commanded pose is within 15-35 mm. Timing checks
  include a conservative full-stop acceleration allowance at each frame plus
  0.4 seconds of communication margin: <=15 seconds at 50% speed.
- Hour uses three poses: all open, all closed, then time. Normalized vector feed
  keeps each motor <=500 mm/min; equivalent checks meet <=40 seconds at 50%
  and <=20 seconds at 100%. Slower slider settings take longer.
- Bounded M204 controls are allowed only in trajectories; ordinary command
  limits remain intact. Tests cover normal acceleration restoration, Stop,
  reset, legacy current rejection and specific thermal warning/derating faults.
- Simulator production build passes. Whitespace checks pass for this change;
  the independently edited Timiac_Prompt.md is excluded from that check.

No hardware was flashed. Temperature, missed steps, torque, physical timing and
power draw still require commissioning. Thermal protection was retained: this
project has no external temperature input or 80?C cutoff, and the TMC2209 does
not support a 95?C threshold. Its datasheet default internal warning/shutdown
thresholds are approximately 120?C/143?C.

Install all five boards using [instruction.md](../instruction.md), with the
24 V / 400 W supply and active cooling. Historical results follow below.

# 1,400 mA RMS package: 2026-10-06.1

Package **TIMIAC2-3827cd14016b**, protocol 7, requests 1,400 mA RMS for all
running/homing axes and retains the 0.5 holding multiplier (approximately 700 mA).
Generated Marlin configuration confirms all six currents on each Octopus, the
same homing currents, and retained thermal monitoring, 50 mA current reduction,
reporting and shutdown protection.

Validation completed:

- Shared Octopus, both ESP-12 bridges, development/release ESP32 masters and both
  dashboard filesystems built successfully; the Marlin compile-time contract
  checks 1,400 mA on all six driver channels.
- All 15 Python tests and 48 web/control tests passed.
- Host firmware parser, Octopus session, both coordinator modes and bridge tests
  passed, including all 720 concurrent homing switch orders. The Octopus session
  additionally rejects the previous 800 mA current setting and thermal reduction.
- `python run.py verify` passed for every prepared image and matching machine
  identity; `git diff --check` passed.

Prepared images are in `firmware/two-motor`. Update both Octopus boards, both
bridges and the selected master together, following [instruction.md](../instruction.md).
Fit the planned 24 V / 400 W supply and cooling before commissioning.
Hardware was not flashed, and torque, actual coil current, temperatures and
power consumption have not been measured on the physical machine.

# Idle clock regression: 2026-10-04.1

The coordinator/UART regression simulates Marlin's 600-second inactivity policy.
With the bridge hold setup suppressed, a 12:00 start fails at 12:15 when one
board's unchanged pose leaves its planner idle. With `M84 S0`, the production
release scheduler, trajectory builder and UART queues complete a simulated
24-hour cycle, including 24 hourly dances and midnight, with no browser lease
or manual wake-up. A separate day-long run parks at Home and remains held.
UART tests also preserve explicit M18/M84 release and its rehoming requirement.

This is simulated motion, not a physical 24-hour endurance test. The installed
clock was not reachable at 192.168.4.1, so no live fault log was captured and no
hardware was flashed. Machine identity stays **TIMIAC2-1416cea7c0a8** / protocol 7;
the existing Octopus image remains compatible. Update both bridges for this fix.

# Home hold and speed control: 2026-09-29.8

Package **TIMIAC2-1416cea7c0a8**, protocol 7. All five firmware targets and both
web filesystems build; host firmware tests, 15 Python tests and 48 dashboard
and control tests pass. Master export checks the compiled stack budget.

Tests cover manual Home holding across minute/hour boundaries, explicit clock
resume, saved speed settings, invalid JSON values, busy/paused machines, storage
failure, equal captured speed on both bridges, M220/G4 sequence at 20/50/100%,
and extended finite completion deadlines. The actual patched Marlin feed
calculation, scoped G28 speed capture, and concurrent homing routine run with
simulated switches at all three speeds, including cancellation and restoration.

No hardware was flashed and no physical motor test was performed. The in-app
browser was unavailable; dashboard behavior has automated DOM checks but no
visual review. Install both Octopus boards, both bridges and the selected master.

# Previous master fix: 2026-09-29.7 - hour-change stack overflow

The user clarified that both bridges appeared offline when the hour arrived.
The screenshot then showed release startup pending, consistent with a master
restart. Inspection of the .6 release ELF found 8,240 bytes of nested stack
frames on the automatic dance's final pose conversion, exceeding its 8,192-byte
loop stack before additional overhead. The manual button runs on the 16 KB
AsyncTCP stack. No hardware reset log was available to confirm the reset reason.

Command buffers now clear in place instead of creating large temporary Steps.
Single-frame scratch is isolated from full-trajectory callers. Both master
exports check their actual linked stack frames with a 2 KB overhead reserve.

Validation:

- The guard rejects the old release disassembly. The new release automatic-hour
  path accounts for 5,168 bytes, leaving 3,024 bytes for other overhead; queued
  animation execution accounts for 4,016 bytes in both master modes.
- Both master firmware builds and export-time stack checks passed.
- All C++ host suites passed; both coordinator modes were rerun successfully
  after the final scratch-buffer change. Hour-boundary tests cover all 24 hours
  from actual :55 clock poses, midnight, delayed work, and Stop/no replay.
- `python run.py verify` passed the complete matched package.
- Octopus and both ESP bridge images retain their pre-fix hashes. The machine
  identity remains **TIMIAC2-409426dd9f18**; no motor settings changed.

Logs: `.build-hour-reset-release.log`, `.build-hour-reset-master.log`,
`.test-hour-reset-host.log`, `.test-hour-reset-coordinator.log`.
No physical device was flashed or tested. Install only the chosen master and
its web filesystem when the matching 50 mm motor package is already installed.

# Previous package: 2026-09-29.6 - 50 mm maximum travel

Prepared **TIMIAC2-409426dd9f18**, protocol 6, for both dashboard modes.
All twelve axes now have a 0-50 mm range. Close sends every arm to 50 mm;
the halfway clock position is 25 mm. Embedded animations, hardware G-code
examples, manual controls and both dashboards use the same stroke.

Validation:

- `python build.py all` and `python run.py verify` passed for the complete
  matching Octopus, bridge, master and dashboard package.
- All C++ host suites and 46 dashboard/API tests passed. Existing checks cover
  Close on every axis, all clock poses, animation/example agreement and range
  rejection. The compiled Marlin contract checks all six 50 mm travel limits.
- All 15 Python checks passed, including a targeted homing rerun after updating
  its expected search distance from 78 to 75 mm (1.5 times the new stroke).
- The hardware-example check initially caught the old 52 mm small-wave sample;
  it now uses 50 mm closed and a 2.5 mm opening, and the suite passes.

Logs: `.build-travel50.log`, `.test-travel50-host.log`,
`.test-travel50-web.log`, `.test-travel50-python.log`,
`.test-travel50-homing.log` (successful targeted rerun).
No physical board was flashed or tested. Install the matched package on both
Octopus boards, both ESP bridges and the chosen master with its filesystem.

# Previous package: 2026-09-29.5 - automatic hour dance and smoother junctions

Prepared **TIMIAC2-df18c884b3dc**, protocol 6, for both dashboard modes.
The release clock now runs the same Hour preset as Next hour when the RTC
crosses an hour, targeting the current time rather than advancing another hour.
Ordinary five-minute changes still update the clock pose directly. Startup and
explicit time changes establish a new baseline without an extra hour dance.

Both modes use the patched Marlin planner: junction directions are normalized
across all six axes, preventing A/B/C motion from creating false full-reversal
stops. Junction deviation increases from 0.003 to 0.3 mm for less corner braking.
Acceleration, speed, current, stroke and homing settings remain unchanged:
25 mm/s² requested acceleration, 500 mm/min maximum, 800 mA RMS, 52 mm travel,
and 5 mm initial home release.

Validation:

- `python build.py all` passed for Octopus, both bridges, both masters and both
  dashboard filesystems. The release master was rebuilt after final review.
- `python run.py verify` passed all package hashes, identities and layouts.
- All C++ host suites passed. Release coordinator regressions cover all 24
  hour boundaries, midnight, Start clock at :59, a boundary delayed by another
  animation, both board completions, no repeated dance, normal five-minute
  updates, and Stop cancelling automatic work.
- 15 Python tests passed, including a regression that applies the patch to
  the pinned Marlin source and executes its actual junction normalization code.
  Mixed XYZ/ABC directions no longer cause false reversals; true reversals
  still require braking.
- All 46 dashboard/API tests passed.

Logs: `.build-hourly-smooth.log`, `.build-hourly-smooth-release-final.log`,
`.test-hourly-smooth-host.log`, `.test-hourly-smooth-python.log`,
`.test-hourly-smooth-web.log`.
No physical board was flashed or tested. Mechanical smoothness and exact
inter-board timing remain to be checked on the clock. Flash both Octopus boards,
both ESP bridges and the chosen master with its filesystem for this changed
machine identity. The Octopus SD cards are only needed for firmware installation.

# Previous package: 2026-09-29.4 - 52 mm travel and continuous animations

Prepared **TIMIAC2-8dfd00f59e59**, protocol 6, for both dashboard modes.
Close and every axis limit are 52 mm. Initial concurrent-home release and
single-axis homing bump are 5 mm (previously 2 mm). Current remains 800 mA RMS,
64 commanded microsteps interpolated to 256, with a 500 mm/min motion ceiling.

Clock and animation mapping follows the installed X1=12 through C2=1 order.
Next hour and Struggle prepare one complete path per bridge and finish with one
M400/M114 pair, removing the former completion barrier between every frame.
Struggle's two simulator pauses are omitted. The release dashboard adds Next
5 min, Next hour and Struggle; both local dashboards remove Open. Release
startup still homes all arms together before displaying valid RTC time.

Validation:

- `python build.py all` passed: Octopus, both bridges, both masters and both
  dashboard filesystems. Both masters were also rebuilt after final review.
- `python run.py verify` passed package hashes, matching identities and layouts.
- All C++ host suites passed, including both coordinator modes, all 1,440
  clock poses, queued animations, bridge capability checks, Stop and no replay.
- The actual concurrent Marlin homing routine passed release, interrupted
  release, missing/stuck switch and cancellation tests. The independent motor
  latch passed all 720 switch orders. Compile-time checks verify all six
  52 mm limits and all six 5 mm single-axis bumps in the built Marlin source.
- 14 Python tests and 46 dashboard/API tests passed.
- No connected in-app browser was available for visual layout inspection.

Logs: `.build-smooth52.log`, `.build-smooth52-master-final.log`,
`.build-smooth52-release-final.log`, `.test-smooth52-host.log`,
`.test-smooth52-python.log`, `.test-smooth52-web.log`.
No physical board was flashed or tested. Exact inter-board timing and mechanical
smoothness remain to be checked on the clock. Flash both Octopus boards, both
ESP bridges and the chosen master with its filesystem for this changed machine
identity. The Octopus SD cards are only needed for that firmware installation.

# Previous release master: 2026-09-29.2 - automatic startup

Release now starts Home all as soon as both motor boards are healthy and idle,
then displays the current valid RTC time after both homes complete. Startup
requires no browser or control lease. Stop/failure cancels it without replay;
developer mode still starts manually. Homing can run before the RTC becomes valid.

Validation:

- `python build.py master_release` passed, including the release web filesystem.
- `python run.py verify` passed all package hashes, identities and flash layouts.
- All C++ host suites and 45 web tests passed.
- Production coordinator tests cover delayed/missing boards, driver readiness,
  busy boards, a master-only restart, grouped six-arm preparation on both boards,
  both completion acknowledgements and all homing bits, invalid RTC, current time
  sampled after homing, Stop before connection, homing failure/recovery, manual
  takeover, no automatic rehoming on reconnect, and manual developer startup.
- Dashboard tests cover waiting for boards, automatic homing, invalid RTC and
  running status, without sending browser commands to start the system.
- Only `esp_master_release.bin` and `master_release_littlefs.bin` changed in the
  prepared package. Octopus, both bridges, developer images, machine identity
  **TIMIAC2-f5b8d948e714**, and protocol 6 are unchanged.

Logs: `.build-release-startup.log`, `.test-release-startup-host.log`,
`.test-release-startup-web.log`. No physical board was flashed or tested.
For the already installed matching motor package, upload only
`python run.py master_release --port COM5`. No Octopus SD-card update is required.

# Motor package: 2026-09-29.1 - 800 mA, 24 V, 500 mm/min

Prepared and verified **TIMIAC2-f5b8d948e714** (protocol 6) in both modes.
All twelve TMC2209 axes request **800 mA RMS** for running and homing.
The reported 24 V / 7 A supply uses **CHOPPER_DEFAULT_24V**. The user approved
**64 commanded microsteps with interpolation to 256** to permit a **500 mm/min**
maximum while preserving calibration at **11,520 steps/mm**, 48 mm travel.
Homing stays at **120 mm/min**; holding multiplier stays 0.5.

The maximum feed requires 96,000 step pulses/s. The compiled six-axis
STM32F446 contract checks the 100 kHz configuration budget and 80% of Marlin's
estimated single-step ISR ceiling. This is not a hardware timing measurement.

Home all retains Lumiac's concurrent independent switch stopping, release
validation, and cancellation checks. New diagnostics name affected axes for
initial release, interrupted release, unreleased switches and missing seek
hits. The observed C2 switch-release failure remains physically unverified.

Validation:

- `python build.py all`: Octopus, both bridges, both masters, both web images passed.
- `python run.py verify`: all package hashes, identities and flash layouts passed.
- 14 Python tests, 44 web tests, and all C++ host suites passed.
- The bridge and master parser accept F500 and reject F500.1. Readiness rejects
  old 250/600/1,414 mA settings and old 4/32/256-microstep configurations.
- Marlin compile-time checks cover all six current, microstep, calibration,
  feed, interpolation and 24 V timing settings, plus endstop and thermal protections.
- Homing tests cover independent switch stopping, release/cancellation,
  720 switch orders, missing/stuck switches, and exact axis diagnostics.

Logs: `.build-24v-800-500.log`, `.test-800-500-python.log`,
`.test-800-500-host.log`, `.test-800-500-web.log`.
No physical board was flashed. Reflash both Octopus boards, both bridges and
the selected master mode with its filesystem, then reload the dashboard.
Expected driver display: **800 mA**, **64 microsteps**. The table reports the
commanded resolution; interpolation to 256 is enabled in firmware.

# Previous package: 2026-09-28.3 - 250 mA, 256 microsteps, concurrent homing

Octopus diagnostic follow-up: concurrent homing now reports the exact public
axes for initial release, interrupted release, unreleased switches, and missing
seek hits. Motor settings, homing behavior, machine identity and ESP compatibility
are unchanged. Build log: `.build-endstop-axes.log`. This identifies an observed
Board 2 release fault; it does not establish its physical cause.

Prepared and verified **TIMIAC2-62cb6386688c** (protocol 6) for both dashboard
modes. All twelve TMC2209 axes request **250 mA RMS** for running and homing,
with native **256 microsteps**, **46,080 steps/mm**, 48 mm travel and a
**120 mm/min** movement/homing limit. Holding multiplier remains 0.5.
This supersedes the previous 1,414 mA request.

The cap requires 92,160 steps/s. The compiled six-axis STM32F446 contract
checks that against 75% of Marlin's estimated single-step ISR ceiling.
This is a software budget check, not a measured timing or torque benchmark.

Home all preserves one G28 group per board and uses the Lumiac homing port.
Each physical switch independently latches its motor off. Initially pressed
switches release 2 mm; missing or stuck switches fail without marking home.
The master prepares both boards before committing their groups; network
starts are coordinated but not guaranteed simultaneous to the microsecond.

The driver table receives numerical M122 Set current and msteps values
through bridge and master, with missing/mismatched data shown explicitly.
Static master web assets use no-store caching. Sensorless homing, EEPROM
overrides, and runtime SD programs remain disabled.

Validation:

- All Octopus, ESP bridge, dev/release master and filesystem builds succeeded.
- `python run.py verify` passed all hashes, identities and flash layouts.
- 14 Python tests and 44 web tests passed.
- All C++ host suites passed, including grouped acknowledgements, cancellation,
  old-current rejection, numerical telemetry forwarding, partial TCP writes,
  Stop/recovery and no replay.
- The actual patched Marlin homing routine passed simulated initial-switch
  release, stuck/disconnected switches, every incomplete hit mask, interrupted
  release and cancellation during release/seek. The per-motor latch passed all
  720 switch orders and repeated/bouncing hits.

Logs: `.build-250-256.log`, `.test-250-256-python.log`,
`.test-250-256-host.log`, `.test-250-256-web.log`.
No physical board was flashed. PSU droop and available torque at 250 mA
remain unmeasured. Reflash both Octopus boards, both bridges and the chosen
master with its filesystem, then reload the dashboard.

# Previous package: 2026-09-28.2 — 80% driver current

Prepared and verified **TIMIAC2-fb3efdad1c8e** (protocol 6). All twelve
TMC2209 axes request **1,414 mA RMS** during running and microswitch homing.
This is 80% of the calculated 1,767.8 mA full-scale output for the configured
0.11 Ω sense resistors. TMCStepper 0.7.3 selects IRUN=24, giving approximately
1,381 mA RMS nominal output after register rounding. The holding multiplier
remains 0.5. Motor ratings, cooling and delivered current have not been
physically measured or validated.

- `python build.py all`: Octopus, both bridges, both masters and both web images passed.
- `python run.py verify`: all hashes, identities and master flash layouts passed.
- Python: 13 tests passed; web: 43 tests passed; all C++ host suites passed.
- Marlin compile-time checks verify all six running/homing currents, sense
  resistors, thermal monitoring, 50 mA thermal step-down, driver fault stops,
  microswitch homing and disabled runtime SD support.
- Bridge tests reject both the old 600 mA configuration and current reduced
  by thermal monitoring before accepting another motion job.

Reflash both Octopus boards, both bridges and the chosen master mode together.
Build log: `.build-current80.log`; host logs: `.test-current80-firmware.log`
and `.test-current80-web.log`. No hardware was flashed.

# Previous package: 2026-09-28.1

Microswitch-only dev/release builds replace all sensorless profiles.
Validation for this revision covers all eight CLI routes, firmware-only SD
copying, rejection of sensorless identities/M914 and SD execution commands,
master clock scheduling in both modes, and dashboard controls.
The previous entries below are historical, including the retired DIAG tests.
No physical flashing or homing is claimed by software checks.

Verified package identity: **TIMIAC2-17eea50a6245** (protocol 6).
`python build.py all` passed for Octopus, both bridges, both master modes,
and both LittleFS images. `python run.py verify` passed all package hashes,
machine identities and ESP32 flash layouts. The release filesystem contains
only its dashboard and shared clock/network clients.

- Python: 12 tests passed, including all eight upload routes and firmware-only SD copying.
- Web: 43 tests passed, including release controls and preserved dev controls.
- C++: parser, Octopus session, dev/release coordinator, and bridge tests passed.
  The actual master clock loop was exercised without a browser, including
  invalid RTC, unhomed/offline boards, busy programs, five-minute boundaries,
  gesture completion, midnight/time corrections, and Stop. Dev does not
  schedule automatic clock movements.

Build output: `.build-modes.log`; test output: `.test-python.log`,
`.test-web.log`, `.test-firmware.log` (local ignored logs).

# Validation: two motors / three ESPs

## Sensorless homing at half speed — 2026-09-24.7

Prepared and verified **TIMIAC2-ac03df417d27** (protocol 6), with sensorless
Octopus identity **TIMIAC2-ac03df417d27-SG**. The user confirmed that fitting
DIAG jumpers restored sensorless detection, then requested a 50% speed reduction.
The sensorless profile now uses **337.5 mm/min** on all six local axes;
the microswitch profile retains **675 mm/min**. The generated browser settings
display the sensorless speed and the panel explains per-arm sensitivity tuning.
The default 128 threshold remains uncalibrated.

- Both Octopus profiles, all four ESP applications and both LittleFS images
  built successfully. Compiled contracts check every axis's homing feed, along
  with the existing socket, current, microstep and mode requirements.
- All four host firmware suites, ten Python tests and 44 web tests passed.
  Profile checks cover the exact 50% reduction and the separate switch speed;
  the dashboard test checks the displayed 337.5 value and default sensitivity.
- `python run.py verify` passed for the completed package. All connected
  ESPs and Octopus boards need matching images because the machine identity
  changed. Build details: `.build/sensorless-half-speed-build.log`.
- PowerShell marked compiler warning output as `NativeCommandError`; every
  PlatformIO target reported SUCCESS and the resulting package passed verification.

The 180-second homing budgets remain in place. Six complete 72 mm search
moves at 337.5 mm/min total 76.8 seconds before acceleration and command
overhead. No hardware was flashed or moved by this update; contact force,
sound and repeatable homing at the reduced speed need a physical retest.

## Quiet movement correction — 2026-09-24.6

Prepared and verified **TIMIAC2-763964f457fd** (protocol 6). The normal
microswitch profile now enables Marlin hybrid mode on all six axes with
a 1 mm/s threshold: quiet holding/slow motion, SpreadCycle at ordinary
homing and movement speeds. The sensorless image remains full-time
StealthChop for TMC2209 StallGuard4. Compile-time contracts enforce this
difference. Motor current, travel, speed limits and acceleration are unchanged.

The user confirmed the X2/Y2 incident occurred with normal firmware,
microswitches connected, no DIAG jumpers, and ordinary Home selected.
Y2 using its switch is expected. X2 buzzing without motion has not been
physically diagnosed; the revised movement profile is prepared for a
controlled retest, not claimed as a verified hardware fix.

Read-only master status showed revision .5 and both bridges offline at the
time of inspection. No control was claimed and no motion or flash was sent.

Both Octopus profiles, all four ESP roles and both LittleFS images built.
All four host firmware test suites, ten Python tests, and 44 web tests passed.
The four driver-panel tests were rerun successfully after adding the
sensorless/hybrid display distinction. The packaged images passed
`python run.py verify`. The dashboard now explains the selected board's
specific reason for disabling sensorless tests.
No physical noise, holding, movement or sensorless calibration test was run.

## Quiet motors and sensorless testing — 2026-09-24.5

Prepared machine identity **TIMIAC2-5f16d965fe04**, peer protocol **6**.
The quiet switch image identifies with that base name; the optional
sensorless image identifies as **TIMIAC2-5f16d965fe04-SG**.

- Both Octopus profiles compiled successfully for STM32F446ZE_btt, including
  compile-time checks for all six driver modes, pins, current, microsteps
  and the correct endstop behavior for each profile.
- Both ESP bridges, development master, release master and both LittleFS
  images built successfully with revision 2026-09-24.5.
- Host firmware suites passed: parser/native gate, Octopus session,
  coordinator and bridge networking. Added coverage for all twelve
  sensorless addresses, integer threshold bounds, exact profile identity,
  rejection on switch firmware, M914-before-G28 sequencing and M112 on
  interrupted sensorless homing.
- Ten Python tests passed, including profile generation, release integrity
  and selection/renaming of the optional SD image without touching a card.
- All 44 web tests passed. The ten development-dashboard tests also passed
  after adding an assertion that sensorless DIAG readings cannot illuminate
  a microswitch lamp. They cover profile-based button locks, per-arm values,
  invalid thresholds, selected/all commands and busy-state locks.
- `python run.py verify` passed for the packaged files, including the
  sensorless image and both web filesystems.

The in-app browser was unavailable (no browser surfaces listed), so visual
layout QA could not be completed. No physical board was flashed, no motors
were moved, and sound, holding strength, actual stall sensitivity and repeatable
home position have not been measured. The default threshold of 128 is explicitly
uncalibrated. See [the installation and test guide](quiet-and-sensorless.md).

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
