#pragma once
#include <stdint.h>

// Hardware and link configuration for the rover.
// Pin numbers are placeholders until the Phase 1 wiring is fixed (plan/storyboard.md 7).

// ---- Drive: dual H-bridge (TB6612FNG class), motors ganged two per side ----
// Differential/skid steering: a left/right wheel-speed difference produces the turn,
// so there is no separate steering mechanism to drive.
constexpr uint8_t PIN_LEFT_IN1 = 26;
constexpr uint8_t PIN_LEFT_IN2 = 27;
constexpr uint8_t PIN_LEFT_PWM = 14;
constexpr uint8_t PIN_RIGHT_IN1 = 25;
constexpr uint8_t PIN_RIGHT_IN2 = 33;
constexpr uint8_t PIN_RIGHT_PWM = 32;
constexpr uint8_t PIN_MOTOR_STANDBY = 13;

// LEDC hardware-PWM channels reserved for the drive motors.
// Servo channels (arm + mast) get their own channels in Phase 2/3 — do not reuse these.
constexpr uint8_t LEDC_CHANNEL_LEFT = 0;
constexpr uint8_t LEDC_CHANNEL_RIGHT = 1;

// ---- Power monitoring ----
// LiPo pack through a resistor divider into an ADC pin. The divider ratio must be
// re-measured against a real pack in Phase 1 (risk R4).
constexpr uint8_t PIN_BATTERY_SENSE = 34;
constexpr float BATTERY_DIVIDER_RATIO = 2.0f;
constexpr float ADC_REFERENCE_V = 3.3f;
constexpr int ADC_MAX_COUNTS = 4095;

// ---- Control link ----
constexpr uint16_t CONTROL_WS_PORT = 81;

// Failsafe: if no drive command arrives within this window, the motors are cut.
// A dropped Wi-Fi link must never leave the rover driving into the glass wall (risk R1).
constexpr uint32_t COMMAND_TIMEOUT_MS = 500;

constexpr uint32_t TELEMETRY_INTERVAL_MS = 500;
