// Compiled against the staged Marlin configuration, not run on the host.
// It prevents the Octopus board header from silently overriding the profile.
#include "src/inc/MarlinConfig.h"

static_assert(NUM_AXES == 6, "Timiac requires six axes per Octopus");
static_assert(NUM_Z_STEPPERS == 1, "MOTOR3 must not be a second Z axis");
static_assert(AXIS4_NAME == 'A' && AXIS5_NAME == 'B' && AXIS6_NAME == 'C', "A/B/C names changed");

// Physical MOTOR3 / Z2 socket on BTT Octopus V1.x.
static_assert(Z_STEP_PIN == PG4 && Z_DIR_PIN == PC1 && Z_ENABLE_PIN == PA0, "Z must use MOTOR3 motion pins");
static_assert(Z_SERIAL_TX_PIN == PC7 && Z_SERIAL_RX_PIN == PC7 && Z_CS_PIN == PC7, "Z must use MOTOR3 TMC UART");

// Keep the requested six physical sockets: 0, 1, 3, 4, 5, 6.
static_assert(X_STEP_PIN == PF13 && Y_STEP_PIN == PG0, "X/Y socket mapping changed");
static_assert(I_STEP_PIN == PF9 && J_STEP_PIN == PC13 && K_STEP_PIN == PE2, "A/B/C socket mapping changed");
static_assert(I_MIN_PIN == PG11 && J_MIN_PIN == PG12 && K_MIN_PIN == PG13, "A/B/C endstop mapping changed");
