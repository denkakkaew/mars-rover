#include "Drive.h"

namespace {
constexpr uint16_t kRawMinUs = 800;
constexpr uint16_t kRawMaxUs = 2200;
}  // namespace

Drive::Drive(const Channel &esc, const rcmap::ThrottleMap &throttle, const Channel &steer,
             const rcmap::SteerMap &steering, uint32_t frequency_hz,
             uint8_t resolution_bits)
    : esc_(esc),
      throttle_(throttle),
      steer_(steer),
      steering_(steering),
      frequency_hz_(frequency_hz),
      resolution_bits_(resolution_bits) {}

void Drive::begin() {
  // arduino-esp32 2.x LEDC API, as lib/PanTilt uses (a 3.x core needs ledcAttach). The
  // ESC and the steering servo share timer 0, so both calls set the same 50 Hz.
  ledcSetup(esc_.ledc_channel, frequency_hz_, resolution_bits_);
  ledcSetup(steer_.ledc_channel, frequency_hz_, resolution_bits_);
  ledcAttachPin(esc_.pin, esc_.ledc_channel);
  ledcAttachPin(steer_.pin, steer_.ledc_channel);
  stop();
}

void Drive::setDrive(float throttle, float steer) {
  write(rcmap::throttlePulseUs(throttle_, throttle), rcmap::steerPulseUs(steering_, steer));
}

void Drive::stop() { write(throttle_.neutral_us, steering_.centre_us); }

void Drive::writeRawUs(uint16_t esc_us, uint16_t steer_us) {
  write(constrain(esc_us, kRawMinUs, kRawMaxUs), constrain(steer_us, kRawMinUs, kRawMaxUs));
}

void Drive::write(uint16_t esc_us, uint16_t steer_us) {
  esc_us_ = esc_us;
  steer_us_ = steer_us;
  ledcWrite(esc_.ledc_channel, rcmap::dutyCounts(esc_us, frequency_hz_, resolution_bits_));
  ledcWrite(steer_.ledc_channel, rcmap::dutyCounts(steer_us, frequency_hz_, resolution_bits_));
}
