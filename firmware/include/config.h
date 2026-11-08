#pragma once
#include <stdint.h>

// Hardware and link configuration for the rover.
// The pins below are the wiring actually on the bench, last rewired 2026-11-07 for the
// RC-car chassis (docs/esc-wiring.md). The steering end stops are still placeholders.

// ---- Drive: RC-car chassis — brushed ESC + steering servo (docs/esc-wiring.md) ----
// **Rewired 2026-11-07.** The L293D, the DRV8833 and the spring-return steering motor are
// gone. The drive motor is on a **TFTH 40 A brushed ESC** and the steering is the car's
// own **servo**, so both take a standard 50 Hz hobby-servo pulse, not H-bridge PWM.
//
// Pins. **The ESC moved GPIO32 -> GPIO23 on 2026-11-08**: GPIO32 measured 4.8 V with the
// ESC unplugged — 5 V reaching it from the board wiring, which is a fault no firmware can
// drive through, and the likely cause of this board's dead pins. GPIO23 is on the other
// header row from 26/27/32, is not a strapping pin, and is silent during boot, so the ESC
// sees nothing until begin() starts neutral. GPIO33 (steering) passes the same tests.
// **Out of service: GPIO26 (damaged 2026-08-30), GPIO27 (reported broken 2026-11-07) and
// GPIO32 (5 V on its net, 2026-11-08; recorded as BROKEN the same day, once the ESC was
// confirmed working on GPIO23) — drive nothing on any of them, and keep GPIO32's pin
// disconnected from that wiring.** GPIO25 is spare but on the same row as the faults.
//
// The ESC's own signal input pulls up to ~3.8 V when unplugged (measured 2026-11-08). The
// pull-up is weak, so 330 R series is adequate; 1 k (or 3 x 330 R) gives more margin,
// since 3.8 V is above the ESP32's 3.6 V pin limit. The pull-down is optional on GPIO23
// (silent at boot): 4.7-47 k, or none — the ESC then sees a steady high, not a pulse,
// and waits for neutral.
constexpr uint8_t PIN_ESC = 23;          // -> ESC signal (white/orange), 330 R-1 k series
constexpr uint8_t PIN_SERVO_STEER = 33;  // -> steering servo signal, 330 R series, 10 k down

// LEDC channels 0+1 share timer 0, which drops from the L293D's 1.5 kHz to 50 Hz: an ESC
// and a servo both want 50 Hz frames. The camera head keeps timer 1 (channels 2+3).
constexpr uint8_t LEDC_CHANNEL_ESC = 0;
constexpr uint8_t LEDC_CHANNEL_STEER = 1;
constexpr uint32_t DRIVE_PWM_FREQUENCY_HZ = 50;
constexpr uint8_t DRIVE_PWM_RESOLUTION_BITS = 16;  // 0.31 us per count at 50 Hz

// ESC pulse widths. The usual car-ESC convention; the ESC arms on neutral at power-up and
// the failsafe holds neutral rather than stopping the pulses (lib/Drive). **If FORWARD
// drives the car backwards**, swap ESC_FORWARD_US and ESC_REVERSE_US (or the two motor
// leads) — nothing else changes.
constexpr uint16_t ESC_NEUTRAL_US = 1500;
constexpr uint16_t ESC_FORWARD_US = 2000;
constexpr uint16_t ESC_REVERSE_US = 1000;

// |throttle| below this is neutral, so a stick resting a hair off centre does not creep.
// The ESC has its own neutral band on top of this.
constexpr float THROTTLE_DEADBAND = 0.05f;

// **The speed cap: 30% of the ESC's travel**, set 2026-11-08 (0.75 on 2026-11-07, because
// the 370 motor is too powerful at full; then 0.40 and 0.20 on 2026-11-08).
// Full stick reaches this fraction of the ESC's travel — scaled, not clipped, so half
// stick is half the cap and the console's TRANSIT/PRECISION modes keep their meaning.
// 0.30 forward is 1650 us, reverse 1350 us, so the stick's whole travel spans the start
// points below to these: 1550-1650 forward, 1450-1350 reverse. Change it here;
// nothing else changes. Keep it above the start points, or full stick becomes the start
// point (lib/RcMap clamps the start to the cap).
constexpr float THROTTLE_LIMIT_FORWARD = 0.30f;
constexpr float THROTTLE_LIMIT_REVERSE = 0.30f;

// **The minimum speed: where the motor starts to turn.** The ESC ignores pulses near
// neutral (its own dead zone), and the 370 needs more again to break the car away on the
// floor, so the slowest demand jumps straight here instead of creeping up through pulses
// that do nothing. Full stick is still the cap above. **Raise these to make the slowest
// speed faster** — forward up from 1500, reverse down from 1500.
//
// **Forward measured 2026-11-07**: wheels off the ground, the motor first turned at
// `e 1540` (docs/esc-wiring.md 4 step 7) — a 40 us dead zone. The old console floor only
// reached ~1538 us, just short of it, which is why the slowest stick did nothing. Set
// 20 us above that for the car's weight on the floor. **Reverse measured the same day**:
// first turned at `e 1440`, a 60 us dead zone — wider than forward's — so set 20 us
// beyond, at 1420. On most car ESCs reverse only engages after neutral is seen first
// (forward, neutral, reverse).
//
// **2026-11-08: 1550 / 1450**, set by the user after trying 1560 / 1420, 1500 (off — too
// much dead stick) and 1570 / 1440. Forward sits 10 us past the measured start; reverse
// sits 10 us *inside* the ESC's dead zone (measured start 1440), so the lightest reverse
// stick may not move the car — the motor picks up a little further out. Set either to
// 1500 to switch it off.
constexpr uint16_t ESC_FORWARD_START_US = 1550;
constexpr uint16_t ESC_REVERSE_START_US = 1450;

// Steering servo pulse widths. **Measured 2026-11-07 on the bench** with `t` in the
// motortest env (docs/esc-wiring.md 4 step 6): the linkage reaches full left at 1100 us
// and full right at 1900 us. The values below sit **20 us inside each**, so holding full
// lock never jams the servo against the linkage — a jammed servo draws its stall current
// for as long as the stick is held, and stall current is what browns the board out.
// **If LEFT steers right**, swap STEER_LEFT_US and STEER_RIGHT_US. STEER_CENTRE_US is the
// trim: nudge it until a centred stick tracks straight.
constexpr uint16_t STEER_CENTRE_US = 1500;
constexpr uint16_t STEER_LEFT_US = 1120;
constexpr uint16_t STEER_RIGHT_US = 1880;

// ---- Servos: two JX PDI-D56MG, camera pan and tilt (Phase 3) ----
// Revised 2026-09-19: the mast no longer moves, so its pan/tilt pair is gone and only the
// camera's pan/tilt remains. Wiring in docs/servo-wiring.md. **Wired and actuated
// 2026-09-19** (firmware 0.2.0): main.cpp drives them through lib/PanTilt from `mast`
// commands. Free-run testing since 2026-09-20 — 0.2.1 reversed tilt, 0.2.2 opened it
// downward, 0.2.3 reversed both axes after the servos were re-positioned; step 3.4 still
// owes measured limits.
//
// **The D56MG is a 3.7-5.5 V part.** Its rail must be a regulated 5.0 V — never the 6 V
// motor rail, never the pack, and not a fresh 4xAA pack either (5.6 V NiMH, 6.4 V
// alkaline).
//
// Why these GPIOs: neither is a strapping pin and neither emits anything during boot, so
// a reset cannot twitch the camera head into its own cable (the same test GPIO32 passed
// for the drive). GPIO16/17 are left for UART2 to the detection board and GPIO21/22 for
// I2C, so neither future link has to evict a servo.
constexpr uint8_t PIN_SERVO_CAM_PAN = 18;
constexpr uint8_t PIN_SERVO_CAM_TILT = 19;

// LEDC channels share a timer in pairs on arduino-esp32 2.x (0+1, 2+3, ...), and a timer
// has one frequency. The drive (ESC + steering) holds timer 0, so the camera takes 2+3 —
// timer 1 at 50 Hz — and never touch the drive's timer.
constexpr uint8_t LEDC_CHANNEL_SERVO_CAM_PAN = 2;
constexpr uint8_t LEDC_CHANNEL_SERVO_CAM_TILT = 3;

// D56MG timing. It is a digital servo and accepts up to 333 Hz; 50 Hz is the rate every
// servo accepts, so start there and raise it only if step 3.4 wants faster settling.
// 16-bit resolution at 50 Hz is 0.31 us per count. Centre is the vendor's 1520 us.
//
// **Widened 920-2120 us on 2026-09-20**, from the conservative 1000-2000 it first ran. The
// D56MG's 120 degrees is 1520 +/- 600 us at 10 us/deg, so this is the part's whole travel
// and not a guess past it — 2000 us stopped tilt at -48 deg, short of the downward view
// asked for. What each axis may actually use is the *angle* limits below, which is where a
// head that would wrap its own camera cable gets stopped.
constexpr uint32_t SERVO_PWM_FREQUENCY_HZ = 50;
constexpr uint8_t SERVO_PWM_RESOLUTION_BITS = 16;
constexpr uint16_t SERVO_PULSE_MIN_US = 920;
constexpr uint16_t SERVO_PULSE_CENTRE_US = 1520;
constexpr uint16_t SERVO_PULSE_MAX_US = 2120;

// Travel limits, degrees, in protocol terms: pan positive = right, tilt positive = up,
// 0/0 = straight ahead at the horizon (docs/protocol.md 3.4). **First-free-run values,
// set narrow on purpose** — the real mechanical limits are measured at step 3.4 and
// replace these. The rover clamps to them and so does the console, which mirrors them as
// HEAD_PAN_LIMITS / HEAD_TILT_LIMITS in console/scripts/console.gd: change one, change
// both.
constexpr float CAM_PAN_MIN_DEG = -45.0f;  // still has reserve; tilt does not
constexpr float CAM_PAN_MAX_DEG = 45.0f;
// Tilt down opened from -30 to -60 on 2026-09-20 at your request: -30 did not look far
// enough down for driving. -60 is the servo's mechanical end, so this axis now has nothing
// left in reserve — if the bracket fouls or the servo strains before it gets there, pull
// this back rather than widening the pulse limits again. **These angles survived the
// re-positioning unchanged**: they are stated in protocol terms (down is down), and which
// way the servo turns to get there is the scale's sign below.
constexpr float CAM_TILT_MIN_DEG = -60.0f;
constexpr float CAM_TILT_MAX_DEG = 45.0f;

// Pulse per degree. An **estimate**: the D56MG's 120 degrees of travel is taken to span
// about 1200 us. At 10 us/deg the limits above land at 1970..1070 us (pan -45..+45, which
// runs backwards on this mounting) and 920..1970 us (tilt -60..+45) — so tilt reaches the
// end of the 920..2120 us range at full down, while pan keeps margin at both ends.
//
// **The sign is the direction.** If PAN R turns the camera left, or TILT UP points it
// down, the servo is mounted the other way round: negate that axis's constant. Nothing
// else changes — the limits, the console and the wire all stay in protocol terms.
//
// History, because these have moved twice and the reason is mechanical, not a bug:
//   0.2.1 (2026-09-20) tilt reversed to -10: TILT UP pointed the camera down.
//   0.2.3 (2026-09-20) **both** reversed after you re-positioned the servos — pan to -10
//         and tilt back to +10. Re-mounting a servo is exactly what flips these, so expect
//         to revisit them after any bracket change.
constexpr float CAM_PAN_US_PER_DEG = -10.0f;
constexpr float CAM_TILT_US_PER_DEG = 10.0f;

// ---- Power monitoring ----
// 2S LiPo through a **220 k / 22 k** divider into GPIO34 (docs/esc-wiring.md 1.1 and 3).
// Ratio (220 + 22) / 22 = 11.0, so a full 2S pack (8.4 V) is 0.76 V on the pin and even a
// 3S pack (12.6 V) only 1.15 V — far under the 3.6 V absolute maximum whatever is plugged
// in. That closes power-budget finding F5 for this wiring.
//
// Read with analogReadMilliVolts(), which applies the chip's eFuse calibration and the
// attenuation, at **6 dB** (about 0.15-1.75 V): that keeps 0.55-0.76 V in the ADC's
// linear region and roughly halves the step size against the default 11 dB. Calibrate
// against a meter at step 1.7 (risk R4) — 5% resistors can put the ratio off by ~9%,
// and BATTERY_DIVIDER_RATIO is where the correction goes.
constexpr uint8_t PIN_BATTERY_SENSE = 34;
constexpr float BATTERY_DIVIDER_RATIO = 11.0f;

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
// 0.2.0: camera pan/tilt head actuated. 0.2.1: tilt direction reversed. 0.2.2: tilt down
// to -60 deg. 0.2.3: both axes reversed after the servos were re-positioned.
// 0.3.0: RC-car chassis — ESC + proportional steering servo, `steerprop`, 220k/22k divider.
constexpr char FIRMWARE_VERSION[] = "0.3.0";

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
