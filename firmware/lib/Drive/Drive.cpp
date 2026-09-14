#include "Drive.h"

namespace {
// 1.5 kHz, because the PWM lands on the L293D.
//
// An L293D is a bipolar Darlington part. Its outputs take microseconds to cross, so well
// below the audible ceiling the waveform stops resembling a square wave, switching losses
// climb, and the motor sees less than the duty cycle suggests. The DRV8833 is a MOSFET
// bridge and would switch cleanly at the 20 kHz step 1.3 ran it at — but since the
// 2026-08-30 rewire the DRV8833 carries only the steering, which takes no PWM at all, so
// nothing here benefits from raising this. The audible whine is the price of the L293D
// rather than an oversight.
//
// Do not raise this without changing the drive-motor driver.
constexpr uint32_t kPwmFrequencyHz = 1500;
constexpr uint8_t kPwmResolutionBits = 8;
// ESP32 LEDC compares `duty > counter`, and the counter tops out at 2^bits - 1. So a duty
// of 2^bits — one past the top — never loses the comparison and the output is held HIGH
// with no switching at all.
//
// That distinction is worth more on this driver than it looks. At 255/256 the input was
// still being switched 1500 times a second, and an L293D's Darlington outputs take
// microseconds to cross; every one of those transitions is time the motor spends braking
// rather than driving. Commanding a true 100% holds the input at DC HIGH and hands the
// motor every volt the bridge can pass. Full throttle is exactly when the rover needs
// that most — breaking away from rest, which is the case that was failing.
constexpr uint32_t kPwmFullDuty = 1u << kPwmResolutionBits;

// ---- BENCH OVERRIDE, 2026-08-23: the drive channel runs at 100% or not at all ----
//
// Set this false to restore proportional speed control. Nothing else has to change.
//
// Why it is here: the rover cannot be on USB and on the battery at the same time, so a
// free run has no serial monitor and no way to sweep the throttle looking for one that
// moves the chassis. Nailing the duty to full removes the variable entirely — if it does
// not move at 100% with the input held DC, the answer is not in this file.
//
// Three consequences to hold onto while it is set:
//
//   * The console's speed modes become cosmetic. TRANSIT and PRECISION both produce full
//     throttle, and so does a nudge — which is a 0.35 s hop at full power, not the fine
//     move the button promises. Treat every press as full speed.
//   * `ramp` in src/motor_test.cpp stops measuring anything. It jumps to full at the
//     first step past the deadband, so the breakaway figure step 1.5 wants cannot be
//     taken until this goes back to false.
//   * The L293D has no duty cycle to hide behind any more. Full rail into each channel —
//     one motor per channel since the 2026-08-30 rewire, so 600 mA of headroom each
//     rather than one channel carrying both. Still touch the package after a short run,
//     and do not hold forward against a wall.
constexpr bool kFullThrottleOnly = true;

// Below this the geared motor buzzes instead of turning, so treat it as stopped.
//
// This figure was tuned at step 1.3 against a DRV8833 at 20 kHz, and nothing about that
// still holds: the L293D drops ~1.9 V the DRV8833 did not, 1.5 kHz delivers torque
// differently at the same duty, and PWM on the inputs means the off-phase brakes where
// the old enable-pin PWM coasted. Re-measure it with `ramp` in src/motor_test.cpp before
// trusting it.
constexpr float kThrottleDeadband = 0.05f;

// Where a continuous `steer` demand rounds onto this chassis's three positions
// (docs/protocol.md 3.2.1). Stated in the contract so console behaviour is predictable.
constexpr float kSteerThreshold = 0.5f;
}  // namespace

Drive::Drive(const PwmMotor &drive_motor, const SwitchedMotor &steer_motor)
    : drive_(drive_motor), steer_(steer_motor) {}

void Drive::begin() {
  // Both drive inputs are PWM-driven: with the L293D enables strapped to VCC there is no
  // enable to modulate, so the duty moves to whichever input matches the direction and
  // each input needs its own LEDC channel.
  ledcSetup(drive_.channel1, kPwmFrequencyHz, kPwmResolutionBits);
  ledcSetup(drive_.channel2, kPwmFrequencyHz, kPwmResolutionBits);
  ledcAttachPin(drive_.in1, drive_.channel1);
  ledcAttachPin(drive_.in2, drive_.channel2);

  // Plain outputs, no LEDC. The steering has no speed to modulate.
  pinMode(steer_.in1, OUTPUT);
  pinMode(steer_.in2, OUTPUT);

  stop();
}

void Drive::setDrive(float throttle, float steer) {
  applyThrottle(throttle);
  applySteer(steer);
}

void Drive::stop() {
  // Every direction input to LOW. There is no enable and no standby pin left to drop —
  // both are strapped to VCC in the loom — so this is the only stop the firmware has. On
  // the L293D it is a brake (both outputs pulled low); on the DRV8833 it is a coast,
  // which is what lets the steering spring back to centre.
  applyThrottle(0.0f);
  applySteer(0.0f);
}

void Drive::applyThrottle(float throttle) {
  throttle = constrain(throttle, -1.0f, 1.0f);
  const float magnitude = fabsf(throttle);
  uint32_t level = 0u;
  if (magnitude >= kThrottleDeadband) {
    level = kFullThrottleOnly ? kPwmFullDuty
                              : static_cast<uint32_t>(magnitude * kPwmFullDuty + 0.5f);
  }

  if (level == 0) {
    // Both inputs to zero duty. With the L293D enables strapped high that is a brake, not
    // a coast — the outputs are pulled low together and the motor is shorted through the
    // bridge. Step 1.6 measures stopping distance and has to measure it against this.
    ledcWrite(drive_.channel1, 0);
    ledcWrite(drive_.channel2, 0);
    return;
  }

  // The idle input drops to zero *before* the driven one takes its duty, so the bridge is
  // never asked for both directions at once — not even for the width of one instruction.
  const bool forward = throttle > 0.0f;
  if (forward) {
    ledcWrite(drive_.channel2, 0);
    ledcWrite(drive_.channel1, level);
  } else {
    ledcWrite(drive_.channel1, 0);
    ledcWrite(drive_.channel2, level);
  }
}

void Drive::applySteer(float steer) {
  steer = constrain(steer, -1.0f, 1.0f);

  // Three positions, and full voltage for the two that are not centre. There is no duty
  // cycle to pick: the motor drives the axle into a mechanical end stop and stays there
  // while the operator holds the turn.
  //
  // A held turn is therefore a continuous stall, at FULL rail voltage — that is
  // docs/power-budget.md finding F6. Since the 2026-08-30 rewire that stall lands on a
  // DRV8833 channel (1.5 A) with nothing else on the part, which is the most headroom it
  // has had; it is still a stall. Measure it at step 1.4 before holding a turn for any
  // length of time.
  if (fabsf(steer) < kSteerThreshold) {
    // Both inputs low. On a DRV8833 with STBY strapped high that is a coast — the outputs
    // go high-impedance — so the return spring centres the axle unopposed.
    digitalWrite(steer_.in1, LOW);
    digitalWrite(steer_.in2, LOW);
    return;
  }

  // Drop the idle input before raising the other, so the bridge never sees both.
  const bool right = steer > 0.0f;
  digitalWrite(right ? steer_.in2 : steer_.in1, LOW);
  digitalWrite(right ? steer_.in1 : steer_.in2, HIGH);
}
