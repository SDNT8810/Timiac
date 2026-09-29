#include <cassert>
#include <cmath>
#include <iostream>
#define FORCE_INLINE inline
#define NUM_AXES 6
#define ANY(...) false
#define LOOP_LOGICAL_AXES(index) for (unsigned index = 0; index < 6; ++index)
#define sq(value) ((value) * (value))
#define RSQRT(value) (1.0f / std::sqrt(value))
#define LOGICAL_AXIS_ARRAY(E, X, Y, Z, I, J, K, U, V, W) {{X, Y, Z, I, J, K}}
struct xyze_float_t {
  float values[6];
  float& operator[](unsigned index) { return values[index]; }
  float operator[](unsigned index) const { return values[index]; }
  void operator*=(float scale) { for (auto& value : values) value *= scale; }
};
struct Displacement { float x, y, z, i, j, k; };
#include "junction_normalize.inc"
xyze_float_t direction(const Displacement& steps_dist_mm) {
  const unsigned esteps = 0;
  const float primary = sq(steps_dist_mm.x) + sq(steps_dist_mm.y) + sq(steps_dist_mm.z);
  const float secondary = sq(steps_dist_mm.i) + sq(steps_dist_mm.j) + sq(steps_dist_mm.k);
  const float inverse_millimeters = 1 / std::sqrt(primary > 0 ? primary : secondary);
  // This is the actual patched planner branch, with Marlin's own normalizer.
  #include "junction_direction.inc"
  return unit_vec;
}
float dot(const xyze_float_t& a, const xyze_float_t& b) {
  float result = 0;
  for (unsigned i = 0; i < 6; ++i) result += a[i] * b[i];
  return result;
}
int main() {
  const Displacement samples[] = {
    {1, 0, 0, 2, 1, 0}, {1, 0, 0, -2, 1, 0},
    {0, 0, 0, 2, 3, 4}, {1, 2, 3, 0, 0, 0},
    {.001f, .002f, 0, 40, -20, 15}, {0, 3, 4, -7, 8, 9},
    {-1, 0, 0, -2, -1, 0}, {1, 1, 1, 1, 1, 1}
  };
  for (const auto& a : samples) {
    const auto u = direction(a);
    assert(std::fabs(dot(u, u) - 1) < .00001f);
    for (const auto& b : samples) assert(std::fabs(dot(u, direction(b))) <= 1.00001f);
  }
  // Only A reverses; X and B continue. The old XYZ-only scale made the
  // junction cosine 2, incorrectly selecting MINIMUM_PLANNER_SPEED.
  const float corner = -dot(direction(samples[0]), direction(samples[1]));
  assert(std::fabs(corner - 1.f / 3) < .00001f && corner < .999999f);
  assert(-dot(direction(samples[0]), direction(samples[6])) > .99999f); // A true reversal still brakes.
  std::cout << "Marlin junction directions: six-axis normalization and true/false reversals passed\n";
}
