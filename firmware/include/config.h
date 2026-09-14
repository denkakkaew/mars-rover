#pragma once
#include <stdint.h>

// Hardware and link configuration for the rover.
// The drive and steering pins below are the wiring actually on the bench, last rewired
// 2026-08-30; they are not placeholders.

// ---- Drive: L293D for the two drive motors, DRV8833 for the steering motor ----
// The chassis chosen at step 0.2 is steered, not skid-steer: the L293D drives the rear
// axle, the DRV8833 swings the front axle to a mechanical end stop (docs/protocol.md
// 3.2.1).
//
// **Rewired on 2026-08-30, and this is a two-part change.**
//
// 1. The two drive motors are no longer paralleled onto one L293D channel. The front
//    motor takes output A (1Y/2Y) and the rear motor takes output B (3Y/4Y), so each
//    gets its own 600 mA channel instead of the pair sharing one — which is what
//    power-budget finding F8 was about. They still have no independent control, and a
//    steered chassis wants none: the inputs are *tied together in the wiring loom*,
//    IN1+IN3 to one GPIO and IN2+IN4 to the other, so the two channels always see the
//    same direction and the same duty. One command, two bridges, twice the current
//    ceiling.
//
//    **Both L293D enables (1,2EN and 3,4EN) are strapped to VCC on the board**, so the
//    firmware no longer owns an enable pin and the PWM has moved onto the direction
//    inputs — the same sign-magnitude scheme the DRV8833 used at step 1.3, back again
//    for a different reason. Two consequences:
//      * that is TWO LEDC channels for the drive, not one;
//      * with the enable held high there is no high-impedance state, so IN1=IN2=LOW is
//        a **brake**, not a coast. Step 1.6's coast-distance figure has to be measured
//        against that, and a stop now stops the rover shorter than the old wiring did.
//
// 2. The steering motor moved to a **DRV8833**, driven on IN1/IN2 with **STBY strapped
//    to VCC** — so, again, no enable pin in firmware. Both inputs LOW is a genuine
//    coast on a DRV8833, so the return spring still centres the axle unopposed, which
//    is what docs/protocol.md 6.1 relies on. The steering keeps its three positions and
//    full rail voltage; step 1.3 measured that a PWM-limited steering channel cannot
//    shift the axle against its spring.
//
// **PIN_DRIVE_IN1 moved GPIO26 -> GPIO32 on 2026-08-30.** GPIO26 measured 0.9 V driving
// forward where GPIO27 measured a clean 3.3 V driving back, after the pin had been
// working — i.e. the output driver is damaged, not mis-driven, and no firmware change
// recovers it. GPIO32 is the replacement because it is free (the DRV8833's STBY strap
// gave it back), it is not a strapping pin, and unlike GPIO14/GPIO12 it emits nothing
// during boot — a pin that pulses at reset is a pin that twitches the drive motors
// before setup() runs. **Do not put the drive back on GPIO26.**
constexpr uint8_t PIN_DRIVE_IN1 = 32;  // -> L293D 1A + 3A, DIP pins 2 and 10 (tied)
constexpr uint8_t PIN_DRIVE_IN2 = 27;  // -> L293D 2A + 4A, DIP pins 7 and 15 (tied)
constexpr uint8_t PIN_STEER_IN1 = 25;  // -> DRV8833 AIN1
constexpr uint8_t PIN_STEER_IN2 = 33;  // -> DRV8833 AIN2

// LEDC hardware-PWM channels — the drive takes two of them.
//
// With the L293D enables strapped high, there is no enable pin to modulate: the PWM goes
// on whichever direction input matches the commanded direction, and the other input is
// held LOW. Each input therefore needs its own LEDC channel.
//
// The steering claims no LEDC channel at all: it is three-position, so there is no speed
// to modulate (step 1.3).
//
// So channels 2 upward are free for the Phase 3 servos, which need four.
constexpr uint8_t LEDC_CHANNEL_DRIVE_IN1 = 0;
constexpr uint8_t LEDC_CHANNEL_DRIVE_IN2 = 1;

// ---- Power monitoring ----
// LiPo pack through a resistor divider into an ADC pin.
//
// ⚠️ DO NOT WIRE A DIVIDER TO MATCH THE 2.0 BELOW. Step 0.3 found it unsafe against any
// pack this repo has considered: 12.6 V / 2.0 puts 6.3 V on GPIO34 against a 3S pack, and
// 8.4 V / 2.0 still puts 4.2 V there against the 2S pack now recommended. The ESP32's
// absolute maximum on any GPIO is 3.6 V. See docs/power-budget.md finding F5.
//
// Its recommendation moved with the pack: **100k/47k — ratio 3.13, giving 2.69 V at a 2S
// full charge** (it was 100k/27k / 4.70 while a 3S pack was assumed; that pair is still
// *safe* on 2S at 1.79 V, it merely wastes ADC range). The 2S recommendation itself is
// awaiting the 0.3 review gate, which is why nothing below has been changed yet.
//
// Left at 2.0 deliberately until 0.3's review gate picks the resistors: the constant
// follows the divider that gets built, not the other way round. Fix it at step 1.3 when
// the real one is on the board, then calibrate against a meter at 1.7 (risk R4).
constexpr uint8_t PIN_BATTERY_SENSE = 34;
constexpr float BATTERY_DIVIDER_RATIO = 2.0f;
constexpr float ADC_REFERENCE_V = 3.3f;
constexpr int ADC_MAX_COUNTS = 4095;

// ---- Status LED ----
// The only diagnostic the rover has when it is running on battery with no serial cable.
// Blink patterns are in src/main.cpp; what matters here is that GPIO2 is the user LED on
// most ESP32 DevKit-class boards, the same one src/bringup.cpp used at step 1.1. A board
// without one loses the indication and nothing else.
constexpr uint8_t PIN_STATUS_LED = 2;

// ---- Control link ----
// Wire format and failsafe contract: docs/protocol.md.
constexpr uint16_t CONTROL_WS_PORT = 81;

// The rover's address on the arena network. The firmware joins as a station, so without
// this it takes whatever the router's DHCP hands out — and the console has no way to
// discover it. That is tolerable on the bench with a USB serial monitor open (setup()
// prints the address it got) and useless the moment the rover free-runs on battery, when
// there is no serial at all.
//
// A fixed address makes the link deterministic: the console's committed default points
// here, so battery bring-up needs no lookup step. Leave ROVER_STATIC_IP as "" to fall
// back to DHCP.
//
// Chosen for the "Dean_WiFi" bench network (192.168.1.0/24, gateway .1) on 2026-08-23.
// .50 sits below the usual .100-.200 DHCP pool; if the router hands that pool out from
// lower down, move this rather than fighting it, and change rover_link.gd to match.
constexpr char ROVER_STATIC_IP[] = "192.168.1.50";
constexpr char ROVER_GATEWAY_IP[] = "192.168.1.1";
constexpr char ROVER_SUBNET_MASK[] = "255.255.255.0";

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

// How often a rover that is not associated retries the join. Short enough that an AP
// coming back recovers the rover within a few seconds, long enough that a rover parked
// out of range is not thrashing its radio for the whole session.
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
