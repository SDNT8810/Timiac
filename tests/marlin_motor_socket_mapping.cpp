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

static_assert(NUM_AXES == 6, "Timiac requires six axes per Octopus");
static_assert(NUM_Z_STEPPERS == 1, "MOTOR3 must not be a second Z axis");
static_assert(AXIS4_NAME == 'A' && AXIS5_NAME == 'B' && AXIS6_NAME == 'C', "A/B/C names changed");

// Physical MOTOR3 / Z2 socket on BTT Octopus V1.x.
static_assert(Z_STEP_PIN == PG4 && Z_DIR_PIN == PC1 && Z_ENABLE_PIN == PA0, "Z must use MOTOR3 motion pins");
static_assert(Z_SERIAL_TX_PIN == PC7 && Z_SERIAL_RX_PIN == PC7, "Z must use MOTOR3 TMC UART");

// Keep the requested six physical sockets: 0, 1, 3, 4, 5, 6.
static_assert(X_STEP_PIN == PF13 && Y_STEP_PIN == PG0, "X/Y socket mapping changed");
static_assert(I_STEP_PIN == PF9 && J_STEP_PIN == PC13 && K_STEP_PIN == PE2, "A/B/C socket mapping changed");
static_assert(I_MIN_PIN == PG11 && J_MIN_PIN == PG12 && K_MIN_PIN == PG13, "A/B/C endstop mapping changed");
static_assert(I_DIR_PIN == PF10 && I_ENABLE_PIN == PG2 && I_SERIAL_TX_PIN == PF2 && I_SERIAL_RX_PIN == PF2, "A signals");
static_assert(J_DIR_PIN == PF0 && J_ENABLE_PIN == PF1 && J_SERIAL_TX_PIN == PE4 && J_SERIAL_RX_PIN == PE4, "B signals");
static_assert(K_DIR_PIN == PE3 && K_ENABLE_PIN == PD4 && K_SERIAL_TX_PIN == PE1 && K_SERIAL_RX_PIN == PE1, "C signals");
static_assert(X_CURRENT == 600 && Y_CURRENT == X_CURRENT && Z_CURRENT == X_CURRENT && I_CURRENT == X_CURRENT && J_CURRENT == X_CURRENT && K_CURRENT == X_CURRENT, "Uniform current");
static_assert(X_MICROSTEPS == 4 && Y_MICROSTEPS == 4 && Z_MICROSTEPS == 4 && I_MICROSTEPS == 4 && J_MICROSTEPS == 4 && K_MICROSTEPS == 4, "Uniform microsteps");
static_assert(X_SLAVE_ADDRESS == 0 && Y_SLAVE_ADDRESS == 0 && Z_SLAVE_ADDRESS == 0 && I_SLAVE_ADDRESS == 0 && J_SLAVE_ADDRESS == 0 && K_SLAVE_ADDRESS == 0, "UART addresses");
