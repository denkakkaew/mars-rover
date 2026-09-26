#include "PanTilt.h"

PanTilt::PanTilt(const Servo &pan, const Servo &tilt, uint32_t frequency_hz,
                 uint8_t resolution_bits)
    : pan_(pan), tilt_(tilt), frequency_hz_(frequency_hz), resolution_bits_(resolution_bits) {}

void PanTilt::begin() {
  // arduino-esp32 2.x LEDC API, as lib/Drive uses (a 3.x core needs ledcAttach instead).
  // Pan and tilt are on one timer pair, so these two calls set the same 50 Hz twice —
  // harmless, and it keeps each channel's setup self-describing.
  ledcSetup(pan_.channel, frequency_hz_, resolution_bits_);
  ledcSetup(tilt_.channel, frequency_hz_, resolution_bits_);
  ledcAttachPin(pan_.pin, pan_.channel);
  ledcAttachPin(tilt_.pin, tilt_.channel);

  pose_ = aim::Pose{};
  write();
}

void PanTilt::point(bool has_pan, float pan_deg, bool has_tilt, float tilt_deg) {
  pose_ = aim::apply(pose_, has_pan, pan_deg, has_tilt, tilt_deg, pan_.axis, tilt_.axis);
  write();
}

void PanTilt::write() {
  const uint16_t pan_us = aim::pulseUs(pan_.axis, pose_.pan_deg);
  const uint16_t tilt_us = aim::pulseUs(tilt_.axis, pose_.tilt_deg);
  ledcWrite(pan_.channel, aim::dutyCounts(pan_us, frequency_hz_, resolution_bits_));
  ledcWrite(tilt_.channel, aim::dutyCounts(tilt_us, frequency_hz_, resolution_bits_));
}
