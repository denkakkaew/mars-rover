#pragma once
#include <Arduino.h>

/// Throttle and steering over a dual H-bridge (L293D class), for the chassis chosen at
/// step 0.2: **one drive motor and one steering motor**, not two driven sides.
///
/// Turning is a mechanism, not a speed difference. The steering motor drives the front
/// axle to a mechanical end stop and a spring recentres it when unpowered, so steering is
/// three-position — full left, straight, full right — and this module owns the rounding
/// from the protocol's continuous `steer` demand onto those three positions
/// (docs/protocol.md 3.2.1).
///
/// **There is no standby pin.** An L293D's only enable is the per-channel enable that the
/// PWM already drives, unlike the TB6612FNG this was originally written for. Stopping is
/// therefore zero duty plus both direction pins low, which is also what lets the steering
/// spring back to centre (docs/protocol.md 6.1).
///
/// Self-contained by design: pins come in through the constructor rather than from
/// config.h, so this module can be lifted into another build unchanged.
class Drive {
 public:
  struct MotorPins {
    uint8_t in1;      ///< H-bridge direction A
    uint8_t in2;      ///< H-bridge direction B
    uint8_t enable;   ///< H-bridge channel enable; PWM is applied here
    uint8_t channel;  ///< LEDC hardware-PWM channel to drive `enable` with
  };

  Drive(const MotorPins &drive_motor, const MotorPins &steer_motor);

  /// Configures pins and PWM channels. Leaves the rover stopped and the steering centred.
  void begin();

  /// `throttle`: -1.0 (full reverse) .. 1.0 (full forward).
  /// `steer`: -1.0 (full left) .. 1.0 (full right); anything inside the centre band leaves
  /// the steering motor unpowered so the spring centres it.
  void setDrive(float throttle, float steer);

  /// Cuts current to both motors. The steering spring-centres as a result.
  void stop();

 private:
  void applyThrottle(float throttle);
  void applySteer(float steer);
  void applyMotor(const MotorPins &motor, bool forward, float duty);

  MotorPins drive_;
  MotorPins steer_;
};
