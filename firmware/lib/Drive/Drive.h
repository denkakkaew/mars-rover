#pragma once
#include <Arduino.h>

/// Throttle and steering over a DRV8833 dual H-bridge, for the chassis chosen at step
/// 0.2: **one drive motor and one steering motor**, not two driven sides.
///
/// **The two motors are driven differently, and deliberately so.** The drive motor has a
/// speed, so its bridge gets PWM. The steering motor has no speed — the axle is at full
/// left lock, straight, or full right lock (docs/protocol.md 3.2.1) — so its bridge is
/// switched flat on or off with no PWM at all.
///
/// That asymmetry is the shape of the problem rather than an optimisation, so the two
/// motors take different pin structs. Measured on the bench at step 1.3: a PWM-limited
/// steering channel could not move the axle against its return spring, and the fix is
/// full voltage, not a better duty cycle.
///
/// **The DRV8833 has no enable pin**, so on the drive bridge the PWM goes on whichever
/// input matches the commanded direction, with the other held low:
///
///     IN1   IN2   result
///     0     0     coast — outputs float
///     PWM   0     forward at duty
///     0     PWM   reverse at duty
///     1     1     brake — both outputs low
///
/// It does have a **standby** pin, which the L293D it replaced lacked. `stop()` drops it,
/// so a stop is a hardware disable of both bridges rather than merely a zero duty cycle —
/// see docs/protocol.md 3.3.
///
/// Self-contained by design: pins come in through the constructor rather than from
/// config.h, so this module can be lifted into another build unchanged.
class Drive {
 public:
  /// The drive bridge: speed matters, so both inputs are PWM-capable.
  struct PwmMotor {
    uint8_t in1;       ///< PWM lives here when driving forward
    uint8_t in2;       ///< PWM lives here when driving in reverse
    uint8_t channel1;  ///< LEDC channel bound to `in1`
    uint8_t channel2;  ///< LEDC channel bound to `in2`
  };

  /// The steering bridge: three positions, no speed, so plain digital outputs. Uses no
  /// LEDC channels at all, which leaves them for the Phase 3 mast servos.
  struct SwitchedMotor {
    uint8_t in1;  ///< high for right lock
    uint8_t in2;  ///< high for left lock
  };

  Drive(const PwmMotor &drive_motor, const SwitchedMotor &steer_motor,
        uint8_t standby_pin);

  /// Configures pins and PWM channels. Leaves the rover stopped, the steering centred,
  /// and the driver in standby.
  void begin();

  /// `throttle`: -1.0 (full reverse) .. 1.0 (full forward), PWM-controlled.
  /// `steer`: -1.0 (full left) .. 1.0 (full right). Thresholded — anything inside the
  /// centre band leaves the steering bridge coasting so the spring centres the axle.
  ///
  /// Brings the driver out of standby. A call with both values zero is still "armed and
  /// commanded to zero" — it coasts, it does not disable (docs/protocol.md 3.3).
  void setDrive(float throttle, float steer);

  /// Cuts both motors and drops the driver into standby. The steering spring-centres as a
  /// result (docs/protocol.md 6.1).
  void stop();

 private:
  void applyThrottle(float throttle);
  void applySteer(float steer);

  PwmMotor drive_;
  SwitchedMotor steer_;
  uint8_t standby_pin_;
};
