#include "RcMap.h"

#include <math.h>

namespace rcmap {
namespace {

float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

/// `from` moved `fraction` (0..1) of the way to `to`, rounded to the nearest microsecond.
/// Signed arithmetic, so it works whichever side of `from` the endpoint sits.
uint16_t towards(uint16_t from, uint16_t to, float fraction) {
  const float us = static_cast<float>(from) +
                   (static_cast<float>(to) - static_cast<float>(from)) * fraction;
  return static_cast<uint16_t>(lroundf(us));
}

}  // namespace

uint16_t throttlePulseUs(const ThrottleMap &map, float throttle) {
  if (!isfinite(throttle)) return map.neutral_us;
  throttle = clampf(throttle, -1.0f, 1.0f);
  if (fabsf(throttle) < map.deadband) return map.neutral_us;

  const bool forward = throttle > 0.0f;
  const float limit = clampf(forward ? map.forward_limit : map.reverse_limit, 0.0f, 1.0f);
  const uint16_t top = towards(map.neutral_us, forward ? map.forward_us : map.reverse_us,
                               limit);
  if (top == map.neutral_us) return map.neutral_us;  // a zero cap never moves

  // The start, as a fraction of the way from neutral to the capped top — signed, so it
  // works whichever side of neutral the endpoints sit. Clamped to 0..1: a start on the
  // wrong side of neutral is no offset, and one beyond the cap is the cap.
  const float start_us = forward ? map.forward_start_us : map.reverse_start_us;
  const float span = static_cast<float>(top) - static_cast<float>(map.neutral_us);
  const float start =
      clampf((start_us - static_cast<float>(map.neutral_us)) / span, 0.0f, 1.0f);

  // start .. top, proportionally. With start = 0 this is exactly neutral .. top.
  return towards(map.neutral_us, top, start + (1.0f - start) * fabsf(throttle));
}

uint16_t steerPulseUs(const SteerMap &map, float steer) {
  if (!isfinite(steer)) return map.centre_us;
  steer = clampf(steer, -1.0f, 1.0f);
  return steer < 0.0f ? towards(map.centre_us, map.left_us, -steer)
                      : towards(map.centre_us, map.right_us, steer);
}

uint32_t dutyCounts(uint16_t pulse_us, uint32_t frequency_hz, uint8_t resolution_bits) {
  const uint64_t full_scale = 1ull << resolution_bits;
  const uint64_t counts =
      (static_cast<uint64_t>(pulse_us) * frequency_hz * full_scale + 500000ull) / 1000000ull;
  const uint64_t max_count = full_scale - 1;
  return static_cast<uint32_t>(counts > max_count ? max_count : counts);
}

}  // namespace rcmap
