#include "Drive.h"

namespace {
// 20 kHz — above the audible band, where this started before step 0.2 chose an L293D.
//
// S.14 dropped it to 1.5 kHz because an L293D is a bipolar Darlington part that cannot
// switch cleanly any faster, and accepted an audible whine as the price. The DRV8833 is a
// MOSFET bridge rated well beyond this, so both halves of that trade are refunded: the
// whine goes, and low-speed torque improves rather than suffers.
constexpr uint32_t kPwmFrequencyHz = 20000;
constexpr uint8_t kPwmResolutionBits = 8;
constexpr uint32_t kPwmMaxDuty = (1u << kPwmResolutionBits) - 1;

// Below this the geared motor buzzes instead of turning, so treat it as stopped.
// Retuned against the real motor at step 1.3.
constexpr float kThrottleDeadband = 0.05f;

// Where a continuous `steer` demand rounds onto this chassis's three positions
// (docs/protocol.md 3.2.1). Stated in the contract so console behaviour is predictable.
constexpr float kSteerThreshold = 0.5f;
}  // namespace

Drive::Drive(const PwmMotor &drive_motor, const SwitchedMotor &steer_motor,
             uint8_t standby_pin)
    : drive_(drive_motor), steer_(steer_motor), standby_pin_(standby_pin) {}

void Drive::begin() {
  ledcSetup(drive_.channel1, kPwmFrequencyHz, kPwmResolutionBits);
  ledcAttachPin(drive_.in1, drive_.channel1);
  ledcSetup(drive_.channel2, kPwmFrequencyHz, kPwmResolutionBits);
  ledcAttachPin(drive_.in2, drive_.channel2);

  // Plain outputs, no LEDC. The steering has no speed to modulate.
  pinMode(steer_.in1, OUTPUT);
  pinMode(steer_.in2, OUTPUT);

  pinMode(standby_pin_, OUTPUT);
  stop();
}

void Drive::setDrive(float throttle, float steer) {
  digitalWrite(standby_pin_, HIGH);
  applyThrottle(throttle);
  applySteer(steer);
}

void Drive::stop() {
  // Outputs to coast first, then disable the driver — not the other way round, so there
  // is no instant where the bridges are still energised with the chip on its way down.
  applyThrottle(0.0f);
  applySteer(0.0f);
  digitalWrite(standby_pin_, LOW);
}

void Drive::applyThrottle(float throttle) {
  throttle = constrain(throttle, -1.0f, 1.0f);
  const float magnitude = fabsf(throttle);
  const uint32_t level =
      (magnitude < kThrottleDeadband) ? 0u
                                      : static_cast<uint32_t>(magnitude * kPwmMaxDuty);

  if (level == 0) {
    // Both inputs low = coast. Deliberately not brake (both high): the rover is meant to
    // roll to a stop, and step 1.6 measures that coast distance as a safety figure.
    ledcWrite(drive_.channel1, 0);
    ledcWrite(drive_.channel2, 0);
    return;
  }

  // PWM on the input matching the direction, the other held low. The idle input must be
  // rewritten every time, not just once — it is the *previous* direction's PWM pin, and
  // leaving it running would brake or reverse instead of driving.
  if (throttle > 0.0f) {
    ledcWrite(drive_.channel1, level);
    ledcWrite(drive_.channel2, 0);
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
  // A held turn is therefore a continuous stall. That is tolerable here in a way it was
  // not on the L293D — the DRV8833 carries 1.2 A per channel against 600 mA and has
  // overcurrent and thermal protection of its own — but it is still the current figure to
  // watch at step 1.7. If it proves too high, the fix is a series resistor or a
  // deliberately measured duty cycle, not the guess that used to live here.
  if (fabsf(steer) < kSteerThreshold) {
    // Coast, so the return spring centres the axle rather than the motor fighting it.
    digitalWrite(steer_.in1, LOW);
    digitalWrite(steer_.in2, LOW);
    return;
  }

  const bool right = steer > 0.0f;
  digitalWrite(steer_.in1, right ? HIGH : LOW);
  digitalWrite(steer_.in2, right ? LOW : HIGH);
}
