#pragma once
#include <stdint.h>

// Hardware and link configuration for the rover.
// Pin numbers are still placeholders until the Phase 1 wiring is fixed (step 1.3) — S.14
// renamed them to match the chassis chosen at 0.2, it did not measure them.

// ---- Drive: DRV8833 dual H-bridge, one drive motor and one steering motor ----
// The chassis chosen at step 0.2 is steered, not skid-steer: bridge A drives the rear
// axle, bridge B swings the front axle to a mechanical end stop (docs/protocol.md 3.2.1).
//
// **Driver changed from L293D to DRV8833 at step 1.3**, after the L293D left only ~2.9 V
// at the motor from a 4.8 V pack. Wired and jumper-tested on the bench 2026-08-13.
//
// The DRV8833 has NO enable pin: each bridge takes two inputs and the PWM goes on
// whichever one matches the commanded direction (see lib/Drive/Drive.h). So every input
// needs its own LEDC channel — four here, where the L293D needed two.
//
// Module silkscreen, confirmed on the actual board:
//   VM · NC · GND · AO1 · AO2 · BO2 · BO1 · BIN1 · BIN2 · AIN1 · AIN2 · STBY
// Note BO2 is printed before BO1 — reversed relative to the A side, so if the steering
// runs backwards, that is a sign flip to fix at step 1.4, not a wiring error.
constexpr uint8_t PIN_DRIVE_IN1 = 26;  // -> AIN1
constexpr uint8_t PIN_DRIVE_IN2 = 27;  // -> AIN2
constexpr uint8_t PIN_STEER_IN1 = 25;  // -> BIN1
constexpr uint8_t PIN_STEER_IN2 = 33;  // -> BIN2

// STBY must be HIGH for the driver to do anything; this module breaks it out with no
// pull-up. Driving it from a GPIO rather than tying it to 3V3 is what makes `stop()` a
// hardware disable of both bridges instead of just a zero duty cycle.
constexpr uint8_t PIN_MOTOR_STANDBY = 14;

// LEDC hardware-PWM channels — only the drive bridge needs them.
//
// The steering bridge is switched flat on/off with plain digital outputs and claims no
// LEDC channel at all: it is three-position, so there is no speed to modulate. Measured
// at step 1.3 — a PWM-limited steering channel could not shift the axle against its
// return spring, and full voltage is the fix rather than a higher duty cycle.
//
// So channels 2 upward are free for the Phase 3 mast servos.
constexpr uint8_t LEDC_CHANNEL_DRIVE_IN1 = 0;
constexpr uint8_t LEDC_CHANNEL_DRIVE_IN2 = 1;

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
