#pragma once
#include <stdint.h>

/// Angle -> servo pulse for the camera pan/tilt head (docs/servo-wiring.md).
///
/// Every decision the head makes lives here: how far an axis may travel, which way is
/// positive, what pulse an angle becomes, and what a `mast` command with a missing axis
/// means. lib/PanTilt only writes the resulting duty to the LEDC hardware.
///
/// **No Arduino headers**, for the same reason as lib/Protocol and lib/Safety: this builds
/// and is tested on the PC (`python -m platformio test -e native`), so a wrong limit or a
/// reversed axis is caught before the camera is driven into its own cable.
namespace aim {

/// One servo axis. Two sets of limits, deliberately: the angle limits are *mechanical*
/// (how far the head can turn before it wraps its cable or hits the chassis), the pulse
/// limits are *electrical* (what the servo is specified to accept). Both are enforced, so
/// a mis-set scale can never push a pulse outside the servo's range either.
struct Axis {
  float min_deg;       ///< Travel limit, most negative angle allowed
  float max_deg;       ///< Travel limit, most positive angle allowed
  uint16_t centre_us;  ///< Pulse at 0 degrees
  /// Microseconds per degree. **Signed**: a servo mounted the other way round gets a
  /// negative scale, so the protocol's "positive = right / up" holds whatever the bracket.
  float us_per_deg;
  uint16_t min_us;  ///< Shortest pulse ever emitted
  uint16_t max_us;  ///< Longest pulse ever emitted
};

/// The head's commanded angles, in protocol terms: pan positive = right, tilt positive
/// = up, 0/0 = straight ahead at the horizon (docs/protocol.md 3.4).
struct Pose {
  float pan_deg = 0.0f;
  float tilt_deg = 0.0f;
};

/// Clamps `deg` into the axis's travel. A non-finite angle returns `hold` unchanged —
/// JSON cannot carry NaN, but a NaN that got here some other way must not reach a servo.
float clampAngle(const Axis &axis, float deg, float hold);

/// The pulse width for `deg`, after clamping to both the travel and the pulse limits.
uint16_t pulseUs(const Axis &axis, float deg);

/// LEDC duty counts for `pulse_us` at the given PWM frequency and resolution. Rounded to
/// the nearest count and never above full scale.
uint32_t dutyCounts(uint16_t pulse_us, uint32_t frequency_hz, uint8_t resolution_bits);

/// Applies one `mast` command (docs/protocol.md 3.4): absolute angles, a missing axis
/// holds its current angle, and each axis is clamped to its own travel.
Pose apply(const Pose &current, bool has_pan, float pan_deg, bool has_tilt, float tilt_deg,
           const Axis &pan, const Axis &tilt);

}  // namespace aim
