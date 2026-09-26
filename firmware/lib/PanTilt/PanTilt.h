#pragma once
#include <Arduino.h>

#include "Aim.h"

/// The camera pan/tilt head: two servos on LEDC PWM (docs/servo-wiring.md).
///
/// Hardware only. Every decision — travel limits, direction, angle-to-pulse, what a
/// missing axis means — is lib/Aim's and is unit-tested on the host; this class holds the
/// current pose and writes duty cycles.
///
/// Self-contained like lib/Drive: pins, channels and limits come in through the
/// constructor rather than from config.h, so the module lifts into another build unchanged.
///
/// **No safe-mode logic here.** Whether a command may move the head is the failsafe's
/// call, made in main.cpp before `point()` is reached (docs/protocol.md 6.1). And the
/// head is never moved *by* the failsafe: in safe mode it holds its last angle, because
/// it is the operator's only way of seeing what just happened.
class PanTilt {
 public:
  struct Servo {
    uint8_t pin;
    uint8_t channel;  ///< LEDC channel; pan and tilt should share a timer pair
    aim::Axis axis;
  };

  PanTilt(const Servo &pan, const Servo &tilt, uint32_t frequency_hz,
          uint8_t resolution_bits);

  /// Configures both LEDC channels and drives the head to 0/0. **The head moves at
  /// boot**: until this runs the signal lines sit at 0 V on their pull-downs and the
  /// servos receive no pulses at all; from here on they hold the centre.
  void begin();

  /// Moves to a new pose. Absolute; a `false` has_* flag holds that axis.
  void point(bool has_pan, float pan_deg, bool has_tilt, float tilt_deg);

  /// The pose last commanded, after clamping — what telemetry reports.
  aim::Pose pose() const { return pose_; }

 private:
  void write();

  Servo pan_;
  Servo tilt_;
  uint32_t frequency_hz_;
  uint8_t resolution_bits_;
  aim::Pose pose_;
};
