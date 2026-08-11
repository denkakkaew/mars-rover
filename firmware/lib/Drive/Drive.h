#pragma once
#include <Arduino.h>

/// Differential (skid) steering over a dual H-bridge.
///
/// The four drive motors are ganged two per side, so the whole drivetrain is two
/// signed throttles. Turning comes from the left/right speed difference — there is
/// no steering mechanism to command.
///
/// Self-contained by design: pins come in through the constructor rather than from
/// config.h, so this module can be lifted into another build unchanged.
class Drive {
 public:
  struct SidePins {
    uint8_t in1;      ///< H-bridge direction A
    uint8_t in2;      ///< H-bridge direction B
    uint8_t pwm;      ///< H-bridge enable / speed
    uint8_t channel;  ///< LEDC hardware-PWM channel to drive `pwm` with
  };

  Drive(const SidePins &left, const SidePins &right, uint8_t standby_pin);

  /// Configures pins and PWM channels. Leaves the rover stopped.
  void begin();

  /// Sets both side throttles, each clamped to -1.0 (full reverse) .. 1.0 (full forward).
  void setThrottle(float left, float right);

  /// Cuts drive current and puts the H-bridge into standby.
  void stop();

 private:
  void applySide(const SidePins &side, float throttle);

  SidePins left_;
  SidePins right_;
  uint8_t standby_pin_;
};
