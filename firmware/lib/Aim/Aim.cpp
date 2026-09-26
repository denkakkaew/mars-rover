#include "Aim.h"

#include <math.h>

namespace aim {

float clampAngle(const Axis &axis, float deg, float hold) {
  if (!isfinite(deg)) return hold;
  if (deg < axis.min_deg) return axis.min_deg;
  if (deg > axis.max_deg) return axis.max_deg;
  return deg;
}

uint16_t pulseUs(const Axis &axis, float deg) {
  const float angle = clampAngle(axis, deg, 0.0f);
  float us = static_cast<float>(axis.centre_us) + angle * axis.us_per_deg;
  if (us < axis.min_us) us = axis.min_us;
  if (us > axis.max_us) us = axis.max_us;
  return static_cast<uint16_t>(lroundf(us));
}

uint32_t dutyCounts(uint16_t pulse_us, uint32_t frequency_hz, uint8_t resolution_bits) {
  // counts = pulse / period * full scale, in integers wide enough not to overflow:
  // 2400 us * 1000 Hz * 2^20 is well inside 64 bits.
  const uint64_t full_scale = 1ull << resolution_bits;
  const uint64_t counts =
      (static_cast<uint64_t>(pulse_us) * frequency_hz * full_scale + 500000ull) / 1000000ull;
  const uint64_t max_count = full_scale - 1;
  return static_cast<uint32_t>(counts > max_count ? max_count : counts);
}

Pose apply(const Pose &current, bool has_pan, float pan_deg, bool has_tilt, float tilt_deg,
           const Axis &pan, const Axis &tilt) {
  Pose next = current;
  if (has_pan) next.pan_deg = clampAngle(pan, pan_deg, current.pan_deg);
  if (has_tilt) next.tilt_deg = clampAngle(tilt, tilt_deg, current.tilt_deg);
  return next;
}

}  // namespace aim
