#include "Drive.h"

namespace {
// 1.5 kHz, down from the 20 kHz this used under the TB6612FNG assumption.
//
// An L293D is a bipolar Darlington part and cannot switch cleanly at 20 kHz: most of the
// duty cycle would be spent in the transition, wasted as heat, and what it costs is
// low-speed torque — exactly the region the S.13 throttle floor lives in
// (docs/chassis-envelope.md 8.4). A few kHz is the usual guidance for this part.
//
// The deliberate price: this is inside the audible band, so **the motors will whine**.
// That is a trade made knowingly, not a defect to be reported at step 1.3.
constexpr uint32_t kPwmFrequencyHz = 1500;
constexpr uint8_t kPwmResolutionBits = 8;
constexpr uint32_t kPwmMaxDuty = (1u << kPwmResolutionBits) - 1;

// Below this the geared motor buzzes instead of turning, so treat it as stopped.
// Retuned against the real motor at step 1.3.
constexpr float kThrottleDeadband = 0.05f;

// Where a continuous `steer` demand rounds onto this chassis's three positions
// (docs/protocol.md 3.2.1). Stated in the contract so console behaviour is predictable.
constexpr float kSteerThreshold = 0.5f;

// Steering is three-position, so a held turn parks the motor against a mechanical end stop
// for as long as the operator holds it — a continuous stall, not a transient one. Holding
// below full duty limits that stall current, which matters because the L293D is rated
// 600 mA per channel and a stalled motor is the case most likely to exceed it
// (docs/chassis-envelope.md 8.3).
//
// This figure is a guess with no hardware to check it against: too low and the axle will
// not reach full lock, too high and the driver cooks. Step 1.3 finds the lowest value that
// still reaches the stop, and 1.7 puts the resulting current on a meter
// (docs/protocol.md 9, open item 8).
constexpr float kSteerHoldDuty = 0.7f;
}  // namespace

Drive::Drive(const MotorPins &drive_motor, const MotorPins &steer_motor)
    : drive_(drive_motor), steer_(steer_motor) {}

void Drive::begin() {
  for (const MotorPins *motor : {&drive_, &steer_}) {
    pinMode(motor->in1, OUTPUT);
    pinMode(motor->in2, OUTPUT);
    ledcSetup(motor->channel, kPwmFrequencyHz, kPwmResolutionBits);
    ledcAttachPin(motor->enable, motor->channel);
  }
  stop();
}

void Drive::setDrive(float throttle, float steer) {
  applyThrottle(throttle);
  applySteer(steer);
}

void Drive::stop() {
  // Zero duty on both enables is the whole of standby on an L293D. Cutting the steering
  // motor along with the drive is not incidental: it is what lets the spring recentre the
  // axle, so a rover that stops mid-turn coasts straight (docs/protocol.md 6.1).
  applyMotor(drive_, true, 0.0f);
  applyMotor(steer_, true, 0.0f);
}

void Drive::applyThrottle(float throttle) {
  throttle = constrain(throttle, -1.0f, 1.0f);
  const float magnitude = fabsf(throttle);

  if (magnitude < kThrottleDeadband) {
    applyMotor(drive_, true, 0.0f);
    return;
  }
  applyMotor(drive_, throttle > 0.0f, magnitude);
}

void Drive::applySteer(float steer) {
  steer = constrain(steer, -1.0f, 1.0f);

  // Three positions, not a proportional angle: the demand picks a direction, and the motor
  // runs into its end stop. Inside the centre band it is left unpowered rather than driven
  // to centre, because the spring does that job better than the motor can.
  if (fabsf(steer) < kSteerThreshold) {
    applyMotor(steer_, true, 0.0f);
    return;
  }
  applyMotor(steer_, steer > 0.0f, kSteerHoldDuty);
}

void Drive::applyMotor(const MotorPins &motor, bool forward, float duty) {
  if (duty <= 0.0f) {
    // Both direction pins low = coast, and zero duty removes the channel enable too.
    digitalWrite(motor.in1, LOW);
    digitalWrite(motor.in2, LOW);
    ledcWrite(motor.channel, 0);
    return;
  }

  digitalWrite(motor.in1, forward ? HIGH : LOW);
  digitalWrite(motor.in2, forward ? LOW : HIGH);
  ledcWrite(motor.channel, static_cast<uint32_t>(duty * kPwmMaxDuty));
}
