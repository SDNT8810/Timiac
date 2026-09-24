// Compiled against the staged Marlin configuration, not run on the host.
// It prevents the Octopus board header from silently overriding the profile.
#include "inc/MarlinConfig.h"

#if ANY(STEALTHCHOP_XY, STEALTHCHOP_Z, STEALTHCHOP_I, STEALTHCHOP_J, STEALTHCHOP_K, HYBRID_THRESHOLD)
  #error "All six Timiac axes must use spreadCycle"
#endif
#if DISABLED(VALIDATE_HOMING_ENDSTOPS) || DISABLED(DETECT_BROKEN_ENDSTOP) || DISABLED(EMERGENCY_PARSER)
  #error "Timiac requires homing validation and emergency UART parsing"
#endif
#if ENABLED(EEPROM_SETTINGS)
  #error "Old EEPROM values must not override the machine contract"
#endif
#if ENABLED(TMC_USE_SW_SPI) || HAS_TMC_SPI || !HAS_TMC_UART
  #error "Timiac requires TMC2209 UART, not Lumiac's SPI drivers"
#endif
#if DISABLED(S_CURVE_ACCELERATION) || DISABLED(ENDSTOP_INTERRUPTS_FEATURE) || DISABLED(ENDSTOPS_ALWAYS_ON_DEFAULT)
  #error "Lumiac motion and endstop behavior must be preserved"
#endif
#if ENABLED(CASE_LIGHT_ENABLE) || ENABLED(SDSUPPORT)
  #error "Lumiac peripherals must not reuse Timiac's socket pins or run SD programs"
#endif

static_assert(NUM_AXES == 6, "Timiac requires six axes per Octopus");
static_assert(NUM_Z_STEPPERS == 1, "MOTOR3 is independent axis A, not a second Z axis");
static_assert(AXIS4_NAME == 'A' && AXIS5_NAME == 'B' && AXIS6_NAME == 'C', "A/B/C names changed");
static_assert(AXIS_DRIVER_TYPE(X, TMC2209) && AXIS_DRIVER_TYPE(Y, TMC2209) && AXIS_DRIVER_TYPE(Z, TMC2209) && AXIS_DRIVER_TYPE(I, TMC2209) && AXIS_DRIVER_TYPE(J, TMC2209) && AXIS_DRIVER_TYPE(K, TMC2209), "All six drivers must be UART TMC2209");
constexpr float steps[] = DEFAULT_AXIS_STEPS_PER_UNIT;
static_assert(steps[0] == 5760 && steps[1] == 5760 && steps[2] == 5760 && steps[3] == 5760 && steps[4] == 5760 && steps[5] == 5760, "Preserve Timiac's calibration at 32 microsteps");
static_assert(DEFAULT_ACCELERATION == 25 && DEFAULT_TRAVEL_ACCELERATION == 25 && JUNCTION_DEVIATION_MM == 0.003, "Lumiac acceleration profile");

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
static_assert(X_CURRENT == 600 && Y_CURRENT == X_CURRENT && Z_CURRENT == X_CURRENT && I_CURRENT == X_CURRENT && J_CURRENT == X_CURRENT && K_CURRENT == X_CURRENT, "Uniform current");
static_assert(X_MICROSTEPS == 32 && Y_MICROSTEPS == 32 && Z_MICROSTEPS == 32 && I_MICROSTEPS == 32 && J_MICROSTEPS == 32 && K_MICROSTEPS == 32, "Uniform microsteps");
static_assert(X_SLAVE_ADDRESS == 0 && Y_SLAVE_ADDRESS == 0 && Z_SLAVE_ADDRESS == 0 && I_SLAVE_ADDRESS == 0 && J_SLAVE_ADDRESS == 0 && K_SLAVE_ADDRESS == 0, "UART addresses");
