// Compiled against the staged Marlin configuration, not run on the host.
// It prevents the Octopus board header from silently overriding the profile.
#include "inc/MarlinConfig.h"
#include "module/stepper.h"

#if !ALL(STEALTHCHOP_XY, STEALTHCHOP_Z, STEALTHCHOP_I, STEALTHCHOP_J, STEALTHCHOP_K)
  #error "All six Timiac axes must support quiet holding"
#endif
#if DISABLED(VALIDATE_HOMING_ENDSTOPS) || DISABLED(DETECT_BROKEN_ENDSTOP) || DISABLED(EMERGENCY_PARSER)
  #error "Timiac requires homing validation and emergency UART parsing"
#endif
#if DISABLED(MONITOR_DRIVER_STATUS) || DISABLED(STOP_ON_ERROR) || DISABLED(REPORT_CURRENT_CHANGE)
  #error "Timiac requires driver thermal monitoring, current reduction reporting, and error stops"
#endif
static_assert(CURRENT_STEP_DOWN == 50, "Retain thermal current reduction");
#if ENABLED(EEPROM_SETTINGS)
  #error "Old EEPROM values must not override the machine contract"
#endif
#if ENABLED(TMC_USE_SW_SPI) || HAS_TMC_SPI || !HAS_TMC_UART
  #error "Timiac requires TMC2209 UART, not Lumiac's SPI drivers"
#endif
#if DISABLED(S_CURVE_ACCELERATION) || DISABLED(ENDSTOP_INTERRUPTS_FEATURE)
  #error "Lumiac motion and endstop behavior must be preserved"
#endif
#if ENABLED(CASE_LIGHT_ENABLE)
  #error "Case light must not reuse Timiac's socket pins"
#endif
#if DISABLED(SDSUPPORT) || SDCARD_CONNECTION != ONBOARD || DISABLED(NO_SD_AUTOSTART)
  #error "Remote firmware updates require onboard SD storage with autostart disabled"
#endif

static_assert(NUM_AXES == 6, "Timiac requires six axes per Octopus");
constexpr float homeFeeds[] = HOMING_FEEDRATE_MM_M;
constexpr float expectedHomeFeed = 120;
static_assert(homeFeeds[0] == expectedHomeFeed && homeFeeds[1] == expectedHomeFeed && homeFeeds[2] == expectedHomeFeed &&
  homeFeeds[3] == expectedHomeFeed && homeFeeds[4] == expectedHomeFeed && homeFeeds[5] == expectedHomeFeed,
  "Microswitch homing feed must match on all six axes");
#if ENABLED(SENSORLESS_HOMING) || ANY(X_SENSORLESS, Y_SENSORLESS, Z_SENSORLESS, I_SENSORLESS, J_SENSORLESS, K_SENSORLESS)
  #error "Sensorless homing is forbidden in every Timiac mode"
#endif
#if DISABLED(ENDSTOPS_ALWAYS_ON_DEFAULT) || DISABLED(HYBRID_THRESHOLD)
  #error "Microswitch profile requires endstop protection and SpreadCycle at normal speed"
#endif
static_assert(X_HYBRID_THRESHOLD == 1 && Y_HYBRID_THRESHOLD == 1 && Z_HYBRID_THRESHOLD == 1 &&
  I_HYBRID_THRESHOLD == 1 && J_HYBRID_THRESHOLD == 1 && K_HYBRID_THRESHOLD == 1, "Switch every axis above 1 mm/s");
static_assert(HOLD_MULTIPLIER == 0.5, "Preserve holding torque at half running current");
static_assert(NUM_Z_STEPPERS == 1, "MOTOR3 is independent axis A, not a second Z axis");
static_assert(AXIS4_NAME == 'A' && AXIS5_NAME == 'B' && AXIS6_NAME == 'C', "A/B/C names changed");
static_assert(AXIS_DRIVER_TYPE(X, TMC2209) && AXIS_DRIVER_TYPE(Y, TMC2209) && AXIS_DRIVER_TYPE(Z, TMC2209) && AXIS_DRIVER_TYPE(I, TMC2209) && AXIS_DRIVER_TYPE(J, TMC2209) && AXIS_DRIVER_TYPE(K, TMC2209), "All six drivers must be UART TMC2209");
constexpr float steps[] = DEFAULT_AXIS_STEPS_PER_UNIT;
static_assert(steps[0] == 11520 && steps[1] == 11520 && steps[2] == 11520 && steps[3] == 11520 && steps[4] == 11520 && steps[5] == 11520, "Preserve Timiac's calibration at 64 microsteps");
static_assert(DEFAULT_ACCELERATION == 25 && DEFAULT_TRAVEL_ACCELERATION == 25 && JUNCTION_DEVIATION_MM == 0.3, "Continuous corner blending with unchanged acceleration");

// Lumiac layout: MOTOR2_1 / MOTOR2_2 are parallel outputs from one Z driver.
static_assert(Z_STEP_PIN == PF11 && Z_DIR_PIN == PG3 && Z_ENABLE_PIN == PG5, "Z must use MOTOR2 motion pins");
static_assert(Z_SERIAL_TX_PIN == PC6 && Z_SERIAL_RX_PIN == PC6, "Z must use MOTOR2 TMC UART");

// Match Lumiac's six physical sockets: 0, 1, 2, 3, 4, 5.
static_assert(X_STEP_PIN == PF13 && Y_STEP_PIN == PG0, "X/Y socket mapping changed");
static_assert(X_DIR_PIN == PF12 && X_ENABLE_PIN == PF14 && X_SERIAL_TX_PIN == PC4 && X_SERIAL_RX_PIN == PC4, "X signals");
static_assert(Y_DIR_PIN == PG1 && Y_ENABLE_PIN == PF15 && Y_SERIAL_TX_PIN == PD11 && Y_SERIAL_RX_PIN == PD11, "Y signals");
static_assert(X_MIN_PIN == PG6 && Y_MIN_PIN == PG9 && Z_MIN_PIN == PG10, "X/Y/Z endstop mapping changed");
static_assert(I_STEP_PIN == PG4 && J_STEP_PIN == PF9 && K_STEP_PIN == PC13, "A/B/C socket mapping changed");
static_assert(I_MIN_PIN == PG11 && J_MIN_PIN == PG12 && K_MIN_PIN == PG13, "A/B/C endstop mapping changed");
static_assert(I_DIR_PIN == PC1 && I_ENABLE_PIN == PA0 && I_SERIAL_TX_PIN == PC7 && I_SERIAL_RX_PIN == PC7, "A signals");
static_assert(J_DIR_PIN == PF10 && J_ENABLE_PIN == PG2 && J_SERIAL_TX_PIN == PF2 && J_SERIAL_RX_PIN == PF2, "B signals");
static_assert(K_DIR_PIN == PF0 && K_ENABLE_PIN == PF1 && K_SERIAL_TX_PIN == PE4 && K_SERIAL_RX_PIN == PE4, "C signals");
static_assert(X_CURRENT == 1000 && Y_CURRENT == X_CURRENT && Z_CURRENT == X_CURRENT && I_CURRENT == X_CURRENT && J_CURRENT == X_CURRENT && K_CURRENT == X_CURRENT, "1000 mA current on every driver");
constexpr int chopperTiming[] = CHOPPER_TIMING;
static_assert(chopperTiming[0] == 4 && chopperTiming[1] == 2 && chopperTiming[2] == 1, "Use the 24 V chopper timing");
static_assert(X_CURRENT_HOME == X_CURRENT && Y_CURRENT_HOME == X_CURRENT && Z_CURRENT_HOME == X_CURRENT && I_CURRENT_HOME == X_CURRENT && J_CURRENT_HOME == X_CURRENT && K_CURRENT_HOME == X_CURRENT, "Use the same current for microswitch homing");
static_assert(X_RSENSE == 0.11 && Y_RSENSE == 0.11 && Z_RSENSE == 0.11 && I_RSENSE == 0.11 && J_RSENSE == 0.11 && K_RSENSE == 0.11, "Current calculation requires the configured 0.11 ohm sense resistors");
static_assert(X_MICROSTEPS == 64 && Y_MICROSTEPS == 64 && Z_MICROSTEPS == 64 && I_MICROSTEPS == 64 && J_MICROSTEPS == 64 && K_MICROSTEPS == 64, "Uniform microsteps");
static_assert(X_SLAVE_ADDRESS == 0 && Y_SLAVE_ADDRESS == 0 && Z_SLAVE_ADDRESS == 0 && I_SLAVE_ADDRESS == 0 && J_SLAVE_ADDRESS == 0 && K_SLAVE_ADDRESS == 0, "UART addresses");

constexpr float maxFeeds[] = DEFAULT_MAX_FEEDRATE;
static_assert(maxFeeds[0] == 8.333333f && maxFeeds[1] == maxFeeds[0] && maxFeeds[2] == maxFeeds[0] && maxFeeds[3] == maxFeeds[0] && maxFeeds[4] == maxFeeds[0] && maxFeeds[5] == maxFeeds[0], "All axes must support 500 mm/min at 64 microsteps");
static_assert(steps[0] * maxFeeds[0] <= 100000 && steps[0] * maxFeeds[0] <= MAX_STEP_ISR_FREQUENCY_1X * 0.80, "Reserve interrupt headroom for six axes, serial and endstops");

#if DISABLED(SPIDER_CONCURRENT_HOMING)
  #error "Home all must use independent concurrent homing"
#endif
static_assert(SPIDER_HOME_RELEASE_MM == 5, "Release initially pressed switches before homing");
constexpr float homeBump[] = HOMING_BUMP_MM;
static_assert(homeBump[0] == 5 && homeBump[1] == 5 && homeBump[2] == 5 && homeBump[3] == 5 && homeBump[4] == 5 && homeBump[5] == 5, "5 mm single-axis homing backoff");
static_assert(X_MAX_POS == 50 && Y_MAX_POS == 50 && Z_MAX_POS == 50 && I_MAX_POS == 50 && J_MAX_POS == 50 && K_MAX_POS == 50, "50 mm travel on every arm");

static_assert(INTERPOLATE, "Interpolate commanded 64 microsteps internally to 256");
