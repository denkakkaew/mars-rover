#pragma once
#include <stdint.h>

/// Throttle and steering demand -> hobby-servo pulse width, for the RC-car chassis
/// (docs/esc-wiring.md): a brushed ESC on the drive motor and a standard steering servo.
///
/// Every decision the drivetrain makes lives here: the throttle deadband, the speed cap,
/// which pulse is neutral, which way is left. lib/Drive only writes the resulting duty to
/// the LEDC hardware.
///
/// **No Arduino headers**, for the same reason as lib/Protocol, lib/Safety and lib/Aim:
/// this builds and is tested on the PC (`python -m platformio test -e native`). A wrong
/// sign here drives a 75 km/h car the wrong way into the glass, so it is cheaper to catch
/// it there.
namespace rcmap {

/// The ESC. Pulses are absolute; `forward_us` may be below `neutral_us` if the ESC or the
/// motor leads turn out to be the other way round — the mapping does not assume an order.
struct ThrottleMap {
  uint16_t neutral_us;  ///< Stopped. The ESC arms on this and the failsafe holds it.
  uint16_t forward_us;  ///< Full forward, before the cap
  uint16_t reverse_us;  ///< Full reverse, before the cap
  /// |throttle| below this is neutral. Keeps a stick resting a hair off centre from
  /// creeping the car.
  float deadband;
  /// Fraction of the forward travel that a full-forward demand reaches, 0..1. The demand
  /// is **scaled**, not clipped, so the console's speed modes keep their meaning: full
  /// stick is the cap, half stick is half the cap.
  float forward_limit;
  float reverse_limit;  ///< Same, for reverse
  /// Where the motor actually starts to turn, forward. Every brushed ESC has its own
  /// neutral band, and a geared 370 needs more again to break away under load, so a
  /// demand just past the deadband jumps here rather than creeping up from neutral
  /// through pulses that do nothing. Set it to `neutral_us` for no offset. A start past
  /// the capped endpoint is treated as the capped endpoint — the cap always wins.
  uint16_t forward_start_us;
  uint16_t reverse_start_us;  ///< Same, for reverse
};

/// The steering servo. Like ThrottleMap, the endpoints carry the direction: if LEFT
/// steers right, swap `left_us` and `right_us` and nothing else changes.
struct SteerMap {
  uint16_t centre_us;  ///< Straight ahead — the trim
  uint16_t left_us;    ///< Full left lock (steer = -1)
  uint16_t right_us;   ///< Full right lock (steer = +1)
};

/// The ESC pulse for a throttle demand of -1.0 (full reverse) .. 1.0 (full forward).
/// Out-of-range demands clamp; a non-finite demand is neutral.
uint16_t throttlePulseUs(const ThrottleMap &map, float throttle);

/// The steering pulse for a demand of -1.0 (full left) .. 1.0 (full right),
/// proportional (docs/protocol.md 3.2.1, `steerprop`). Out-of-range demands clamp; a
/// non-finite demand is centre.
uint16_t steerPulseUs(const SteerMap &map, float steer);

/// LEDC duty counts for `pulse_us` at the given PWM frequency and resolution. Rounded to
/// the nearest count and never above full scale. The same arithmetic as aim::dutyCounts,
/// restated so lib/Drive does not depend on the camera head's library.
uint32_t dutyCounts(uint16_t pulse_us, uint32_t frequency_hz, uint8_t resolution_bits);

}  // namespace rcmap
