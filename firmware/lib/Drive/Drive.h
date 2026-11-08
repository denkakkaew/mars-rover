#pragma once
#include <Arduino.h>

#include "RcMap.h"

/// Throttle and steering for the RC-car chassis (docs/esc-wiring.md): **one ESC and one
/// steering servo**, both driven by standard 50 Hz hobby-servo pulses on LEDC.
///
/// Replaces the L293D + DRV8833 drive of 2026-08-30. Two behaviours changed with the
/// hardware, and both matter to the failsafe:
///
///   * **Stopping is a pulse, not the absence of one.** The ESC arms on, and stops at, a
///     neutral pulse. Cutting the pulses instead would leave the outcome to the ESC's own
///     signal-loss behaviour, which is not published for this part — so `stop()` holds
///     neutral, continuously.
///   * **Steering no longer centres itself.** The old axle had a return spring; a servo
///     holds whatever angle it was last given. So `stop()` actively commands centre,
///     which is what keeps a rover that cuts out mid-turn running straight
///     (docs/protocol.md 6.1).
///
/// Hardware only. Every decision — deadband, speed cap, direction, centre trim — is
/// lib/RcMap's and is unit-tested on the host.
///
/// Self-contained by design: pins, channels and maps come in through the constructor
/// rather than from config.h, so this module can be lifted into another build unchanged.
class Drive {
 public:
  struct Channel {
    uint8_t pin;
    uint8_t ledc_channel;  ///< ESC and steering should share a timer pair
  };

  Drive(const Channel &esc, const rcmap::ThrottleMap &throttle, const Channel &steer,
        const rcmap::SteerMap &steering, uint32_t frequency_hz, uint8_t resolution_bits);

  /// Configures both LEDC channels and starts the pulses at neutral / centre. Until this
  /// runs the signal lines sit at 0 V on their pull-downs and the ESC sees no signal, so
  /// it does not arm; from here on it sees neutral, and arms.
  void begin();

  /// `throttle`: -1.0 (full reverse) .. 1.0 (full forward), scaled by the speed cap.
  /// `steer`: -1.0 (full left) .. 1.0 (full right), proportional.
  void setDrive(float throttle, float steer);

  /// Neutral throttle and centred steering, held. Never stops the pulses.
  void stop();

  /// Writes raw pulse widths, bypassing the maps — and therefore the speed cap. **Bench
  /// use only** (src/motor_test.cpp), for finding the ESC's start point and the steering
  /// end stops. Clamped to 800..2200 us, the widest any hobby servo or ESC accepts.
  void writeRawUs(uint16_t esc_us, uint16_t steer_us);

  uint16_t escPulseUs() const { return esc_us_; }
  uint16_t steerPulseUs() const { return steer_us_; }

 private:
  void write(uint16_t esc_us, uint16_t steer_us);

  Channel esc_;
  rcmap::ThrottleMap throttle_;
  Channel steer_;
  rcmap::SteerMap steering_;
  uint32_t frequency_hz_;
  uint8_t resolution_bits_;
  uint16_t esc_us_ = 0;
  uint16_t steer_us_ = 0;
};
