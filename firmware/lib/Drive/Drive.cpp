#include "Drive.h"

namespace {
// 20 kHz keeps the motor whine above the audible band; 8-bit resolution is plenty
// for open-loop throttle control.
constexpr uint32_t kPwmFrequencyHz = 20000;
constexpr uint8_t kPwmResolutionBits = 8;
constexpr uint32_t kPwmMaxDuty = (1u << kPwmResolutionBits) - 1;

// Below this the geared motors buzz instead of turning, so treat it as stopped.
constexpr float kDeadband = 0.05f;
}  // namespace

Drive::Drive(const SidePins &left, const SidePins &right, uint8_t standby_pin)
    : left_(left), right_(right), standby_pin_(standby_pin) {}

void Drive::begin() {
  for (const SidePins *side : {&left_, &right_}) {
    pinMode(side->in1, OUTPUT);
    pinMode(side->in2, OUTPUT);
    ledcSetup(side->channel, kPwmFrequencyHz, kPwmResolutionBits);
    ledcAttachPin(side->pwm, side->channel);
  }
  pinMode(standby_pin_, OUTPUT);
  stop();
}

void Drive::setThrottle(float left, float right) {
  digitalWrite(standby_pin_, HIGH);
  applySide(left_, left);
  applySide(right_, right);
}

void Drive::stop() {
  applySide(left_, 0.0f);
  applySide(right_, 0.0f);
  digitalWrite(standby_pin_, LOW);
}

void Drive::applySide(const SidePins &side, float throttle) {
  throttle = constrain(throttle, -1.0f, 1.0f);

  const bool forward = throttle > 0.0f;
  const float magnitude = fabsf(throttle);

  if (magnitude < kDeadband) {
    // Both direction pins low = coast.
    digitalWrite(side.in1, LOW);
    digitalWrite(side.in2, LOW);
    ledcWrite(side.channel, 0);
    return;
  }

  digitalWrite(side.in1, forward ? HIGH : LOW);
  digitalWrite(side.in2, forward ? LOW : HIGH);
  ledcWrite(side.channel, static_cast<uint32_t>(magnitude * kPwmMaxDuty));
}
