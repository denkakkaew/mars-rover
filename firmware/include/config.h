#pragma once
#include <stdint.h>

// Hardware and link configuration for the rover.
// Pin numbers are still placeholders until the Phase 1 wiring is fixed (step 1.3) — S.14
// renamed them to match the chassis chosen at 0.2, it did not measure them.

// ---- Drive: L293D dual H-bridge, one drive motor and one steering motor ----
// The chassis chosen at step 0.2 is steered, not skid-steer: channel 1 drives the rear
// axle, channel 2 swings the front axle to a mechanical end stop (docs/protocol.md 3.2.1).
//
// L293D pin naming, since it differs from the TB6612FNG this was first written for:
// each channel pair has IN1/IN2 for direction and its own EN pin, and the EN pin is what
// carries the PWM. **There is no standby pin** — zero duty on both enables is standby, so
// the PIN_MOTOR_STANDBY that used to live here is gone rather than reassigned.
// Confirm against the kit's actual driver board at step 1.3: if it breaks out an extra
// enable of its own, it comes back.
constexpr uint8_t PIN_DRIVE_IN1 = 26;
constexpr uint8_t PIN_DRIVE_IN2 = 27;
constexpr uint8_t PIN_DRIVE_EN = 14;
constexpr uint8_t PIN_STEER_IN1 = 25;
constexpr uint8_t PIN_STEER_IN2 = 33;
constexpr uint8_t PIN_STEER_EN = 32;

// LEDC hardware-PWM channels reserved for the two motors.
// The mast servos get their own channels in Phase 3 — do not reuse these.
constexpr uint8_t LEDC_CHANNEL_DRIVE = 0;
constexpr uint8_t LEDC_CHANNEL_STEER = 1;

// ---- Power monitoring ----
// LiPo pack through a resistor divider into an ADC pin.
//
// ⚠️ DO NOT WIRE A DIVIDER TO MATCH THE 2.0 BELOW. Step 0.3 found it unsafe against the
// 3S pack this repo assumes everywhere else: 12.6 V / 2.0 puts 6.3 V on GPIO34, and the
// ESP32's absolute maximum on any GPIO is 3.6 V. See docs/power-budget.md finding F5,
// which recommends 100k/27k — ratio 4.70, giving 2.68 V at full charge.
//
// Left at 2.0 deliberately until 0.3's review gate picks the resistors: the constant
// follows the divider that gets built, not the other way round. Fix it at step 1.3 when
// the real one is on the board, then calibrate against a meter at 1.7 (risk R4).
constexpr uint8_t PIN_BATTERY_SENSE = 34;
constexpr float BATTERY_DIVIDER_RATIO = 2.0f;
constexpr float ADC_REFERENCE_V = 3.3f;
constexpr int ADC_MAX_COUNTS = 4095;

// ---- Control link ----
// Wire format and failsafe contract: docs/protocol.md.
constexpr uint16_t CONTROL_WS_PORT = 81;

// Reported to the console in the `hello` reply, so a mission log can record which
// build produced a run (docs/protocol.md 4.1).
constexpr char FIRMWARE_VERSION[] = "0.1.0";

// Failsafe: if no drive command arrives within this window, the motors are cut.
// A dropped Wi-Fi link must never leave the rover driving into the glass wall (risk R1).
constexpr uint32_t COMMAND_TIMEOUT_MS = 500;

constexpr uint32_t TELEMETRY_INTERVAL_MS = 500;

// A console that has not identified itself with `hello` this long after connecting is
// treated as an unknown protocol version and refused (docs/protocol.md 5). Mirrored by
// HANDSHAKE_DEADLINE_S in tools/fake_rover.py.
constexpr uint32_t HANDSHAKE_DEADLINE_MS = 2000;
