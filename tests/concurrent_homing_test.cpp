// Run the actual patched Marlin homing routine against simulated mechanics.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "../shared/spider_homing.h"
#define LOOP_NUM_AXES(a) for (unsigned a = 0; a < 6; ++a)
#define TEST(bits, a) ((bits) & (1U << (a)))
#define SBI(bits, a) ((bits) |= (1U << (a)))
#define _MIN(a, b) ((a) < (b) ? (a) : (b))
#define _MAX(a, b) ((a) > (b) ? (a) : (b))
#define ABS(a) std::fabs(a)
#define SPIDER_HOME_RELEASE_MM 5.0f
#define GET_TEXT_F(a) "Homing failed"
std::vector<std::string> messages;
std::string partialMessage;
#define SERIAL_ECHOLNPGM(a) do { messages.push_back(partialMessage + (a)); partialMessage.clear(); } while (0)
#define SERIAL_ERROR_MSG(a) messages.push_back(a)
#define SERIAL_ERROR_START() (partialMessage += "Error:")
#define SERIAL_ECHO(a) (partialMessage += (a))
#define SERIAL_ECHOPGM(a) (partialMessage += (a))
#define SERIAL_CHAR(a, b) do { partialMessage += (a); partialMessage += (b); } while (0)
#define AXIS_CHAR(a) ("XYZABC"[a])
enum AxisEnum {X_AXIS, Y_AXIS, Z_AXIS, I_AXIS, J_AXIS, K_AXIS};
enum EndstopEnum {X_MIN, Y_MIN, Z_MIN, I_MIN, J_MIN, K_MIN};
using feedRate_t = float;
struct Position {
  float values[6] = {};
  Position(float = 0) {}
  float& operator[](unsigned a) { return values[a]; }
  float operator[](unsigned a) const { return values[a]; }
  void reset() { for (auto& v : values) v = 0; }
} current_position, destination;
using abce_pos_t = Position;
struct PlannerHints { float millimeters = 0; };
unsigned active_extruder = 0, homed = 63;
bool killed = false, stuck = false, releaseInterrupted = false;
unsigned cancelAt = 0;
uint8_t seekHits = 63;
void kill(const char*) { killed = true; }
void set_axis_never_homed(AxisEnum a) { homed &= ~(1U << a); }
void set_axis_is_at_home(AxisEnum a) { homed |= 1U << a; current_position[a] = 0; }
void sync_plan_position() {}
#define FORCE_INLINE inline
#define TERN0(condition, value) value
#define DISABLED(option) 1
#define NUM_AXIS_CODE(a,b,c,d,e,f,g,h,i) a; b; c; d; e; f
#define HOMING_FEEDRATE_MM_M {120,120,120,120,120,120}
#define MMM_TO_MMS(v) ((v) / 60.0f)
#define constrain(v, lo, hi) _MIN(_MAX(v, lo), hi)
struct xyz_feedrate_t { float x, y, z, i, j, k; };
uint8_t timiac_homing_percent = 100;
int16_t feedrate_percentage = 100;
#include "homing_feedrate.inc"
#include "restorer.inc"
float max_length(AxisEnum) { return 50; }
struct Endstops {
  uint8_t pressed = 0;
  uint8_t state() { return pressed; }
  void hit_on_purpose() {}
  void enable(bool) {}
} endstops;
struct Stepper {
  bool active = false;
  void begin_spider_homing() { active = true; }
  uint8_t end_spider_homing() { active = false; return seekHits; }
} stepper;
struct Move { Position target; float feed, length; bool concurrent; };
struct Planner {
  uint32_t quick_stop_count = 0;
  Position position;
  float mm_per_step[6] = {1.f/11520, 1.f/11520, 1.f/11520, 1.f/11520, 1.f/11520, 1.f/11520};
  std::vector<Move> moves;
  void synchronize() {}
  void set_machine_position_mm(Position p) { position = p; }
  float get_axis_position_mm(AxisEnum a) { return position[a]; }
  void buffer_segment(Position p, float f, unsigned, PlannerHints h) {
    moves.push_back({p, f, h.millimeters, stepper.active});
    if (cancelAt == moves.size()) { ++quick_stop_count; return; }
    position = p;
    if (!stepper.active) {
      if (releaseInterrupted) position.reset();
      if (!stuck) endstops.pressed = 0;
    }
  }
} planner;
#include "concurrent_homing_routine.inc"
void homeAtSpeed(unsigned percent) {
  feedrate_percentage = percent;
#include "homing_speed_capture.inc"
  feedrate_percentage = 100; // G28's remember_feedrate_scaling_off.
  home_spider_group();
}
void reset() {
  planner = Planner(); stepper = Stepper(); endstops = Endstops();
  killed = stuck = releaseInterrupted = false; cancelAt = 0; seekHits = 63;
  homed = 63; messages.clear(); partialMessage.clear();
}
int main() {
  for (unsigned percent : {20U, 50U, 100U}) {
    for (bool cancelled : {false, true}) {
      reset(); endstops.pressed = 0x25; cancelAt = cancelled ? 2 : 0;
      homeAtSpeed(percent);
      assert(planner.moves.size() == 2);
      for (const auto& move : planner.moves) assert(std::fabs(move.feed - 2 * percent / 100.0f) < 0.00001f);
      assert(timiac_homing_percent == 100); // No scaling leaks out of G28.
      assert(homed == (cancelled ? 0U : 63U));
    }
  }
  reset(); home_spider_group();
  assert(!killed && homed == 63 && planner.moves.size() == 1 && !stepper.active);
  const auto move = planner.moves[0];
  assert(move.concurrent && move.feed == 2 && move.length == 75);
  for (unsigned a = 0; a < 6; ++a) assert(move.target[a] == -75);
  // Initially pressed switches alone move away before the shared seek.
  reset(); endstops.pressed = 0x25; home_spider_group();
  assert(!killed && homed == 63 && planner.moves.size() == 2);
  for (unsigned a = 0; a < 6; ++a) assert(planner.moves[0].target[a] == (TEST(0x25, a) ? 5 : 0));
  assert(!planner.moves[0].concurrent && planner.moves[1].concurrent);
  assert(messages[1] == "Six-arm homing: releasing 5 mm [axes: X Z C]");
  for (uint8_t mask = 0; mask < 63; ++mask) {
    reset(); seekHits = mask; home_spider_group();
    assert(killed && homed == 0 && !stepper.active);
    std::string expected = "Error:Six-arm homing: not all endstops reached [axes:";
    for (unsigned a = 0; a < 6; ++a) if (!TEST(mask, a)) { expected += ' '; expected += AXIS_CHAR(a); }
    assert(messages.back() == expected + "]");
  }
  reset(); endstops.pressed = 1; stuck = true; home_spider_group();
  assert(killed && homed == 0 && planner.moves.size() == 1);
  assert(messages.back() == "Error:Six-arm homing: endstop did not release [axes: X]");
  reset(); endstops.pressed = 0x38; stuck = true; home_spider_group();
  assert(killed && homed == 0 && planner.moves.size() == 1);
  assert(messages.back() == "Error:Six-arm homing: endstop did not release [axes: A B C]");
  reset(); endstops.pressed = 1; releaseInterrupted = true; home_spider_group();
  assert(killed && homed == 0 && planner.moves.size() == 1);
  assert(messages.back() == "Error:Six-arm homing: release move interrupted [axes: X]");
  // Cancellation during release or seek must never authorize movement.
  for (unsigned at = 1; at <= 2; ++at) {
    reset(); endstops.pressed = 1; cancelAt = at; home_spider_group();
    assert(!killed && homed == 0 && !stepper.active && planner.moves.size() == at);
    assert(messages.back() == "Six-arm homing cancelled.");
  }
}
