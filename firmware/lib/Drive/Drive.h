#pragma once
#include <Arduino.h>

/// Throttle and steering for the chassis chosen at step 0.2: **one throttle and one
/// steering angle**, not two driven sides.
///
/// Two different driver chips, rewired 2026-08-30 (see config.h for the wiring and the
/// reasoning):
///
///   * the two **drive** motors on an **L293D**, one per channel — front on output A,
///     rear on output B — with their direction inputs tied together in the loom, so both
///     channels always take the same direction and the same duty;
///   * the **steering** motor on a **DRV8833**.
///
/// **Neither driver gives the firmware an enable pin any more.** The L293D's two enables
/// are strapped to VCC and the DRV8833's STBY is strapped to VCC, so both motors are
/// sign-magnitude on their direction inputs: PWM on whichever input matches the commanded
/// direction, the other held LOW.
///
///     in1   in2   L293D (EN high)          DRV8833 (STBY high)
///     PWM   0     forward at duty          forward at duty
///     0     PWM   reverse at duty          reverse at duty
///     0     0     brake — outputs low      coast — outputs high-Z
///
/// That last row is the one difference that matters, and it lands on the right motor by
/// luck rather than design: the drive channel **brakes** when commanded to zero (the
/// L293D has no high-impedance state with its enable strapped high), while the steering
/// channel **coasts**, which is what lets the return spring centre the axle unopposed
/// (docs/protocol.md 6.1). Step 1.6 measures coast distance and must be measured against
/// a braking drive channel, not the coasting one the old enable-pin wiring had.
///
/// **The two motors are driven differently, and deliberately so.** The drive channel has
/// a speed, so its inputs are PWM-driven and each claims an LEDC channel. The steering
/// channel has no speed — the axle is at full left lock, straight, or full right lock
/// (docs/protocol.md 3.2.1) — so its inputs are switched flat on or off with no PWM and
/// no LEDC channel at all. Measured on the bench at step 1.3: a PWM-limited steering
/// channel could not move the axle against its return spring, and the fix is full
/// voltage, not a better duty cycle.
///
/// That asymmetry is the shape of the problem rather than an optimisation, so the two
/// motors take different pin structs.
///
/// That there are *two* drive motors is invisible here and should stay that way — a
/// steered chassis wants both drive wheels at the same speed, so `PwmMotor` describes a
/// commanded direction with two L293D channels behind it, not a motor.
///
/// Self-contained by design: pins come in through the constructor rather than from
/// config.h, so this module can be lifted into another build unchanged.
class Drive {
 public:
  /// The drive channel: speed matters, so both direction inputs are PWM-driven and each
  /// needs its own LEDC channel. Only one is ever non-zero at a time.
  struct PwmMotor {
    uint8_t in1;       ///< PWM here for forward, LOW for reverse
    uint8_t in2;       ///< PWM here for reverse, LOW for forward
    uint8_t channel1;  ///< LEDC channel bound to `in1`
    uint8_t channel2;  ///< LEDC channel bound to `in2`
  };

  /// The steering channel: three positions, no speed, so its inputs are plain digital
  /// outputs. Claims no LEDC channel at all, which leaves them for the Phase 3 servos.
  struct SwitchedMotor {
    uint8_t in1;  ///< high for right lock
    uint8_t in2;  ///< high for left lock
  };

  Drive(const PwmMotor &drive_motor, const SwitchedMotor &steer_motor);

  /// Configures pins and the two drive PWM channels. Leaves the rover stopped, steering
  /// centred.
  void begin();

  /// `throttle`: -1.0 (full reverse) .. 1.0 (full forward), PWM-controlled.
  /// `steer`: -1.0 (full left) .. 1.0 (full right). Thresholded — anything inside the
  /// centre band drops both steering inputs so the spring centres the axle.
  ///
  /// A call with both values zero is still "armed and commanded to zero" — the drive
  /// channel brakes and the steering coasts, which at the motor terminals is
  /// indistinguishable from a stop (docs/protocol.md 3.3).
  void setDrive(float throttle, float steer);

  /// Cuts both motors by dropping every direction input to LOW. The drive channel brakes
  /// and the steering spring-centres as a result (docs/protocol.md 6.1).
  void stop();

 private:
  void applyThrottle(float throttle);
  void applySteer(float steer);

  PwmMotor drive_;
  SwitchedMotor steer_;
};
