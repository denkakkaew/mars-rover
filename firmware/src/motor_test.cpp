// ESC + steering servo bench — docs/esc-wiring.md section 4, steps 5-7.
//
//   python -m platformio run -e motortest -t upload -t monitor
//
// Serial-driven, with **no Wi-Fi, no console and no protocol**, so a car that will not
// move is not diagnosed through a radio link. Same reasoning as bringup.cpp.
//
// Two kinds of command:
//
//   * **Through lib/Drive** — `f`, `b`, `l`, `r`, `c`. These go through lib/RcMap, so the
//     deadband, the speed cap and the steering end stops under test are exactly the ones
//     the rover will run.
//   * **Raw pulses** — `e <us>`, `t <us>`. These bypass the maps *and the speed cap*, and
//     exist to measure what config.h is missing: where the ESC starts to turn the motor,
//     and where the steering linkage hits its end stops. **Wheels off the ground.**
//
// Safety, because this sketch can spin a 75 km/h car's motor:
//
//   * Boots at neutral / centre, which is what the ESC arms on.
//   * **Throttle returns to neutral after AUTO_STOP_MS** unless a fresh command arrives.
//     Steering is left where it is — a held steering angle cannot move the car.
//   * `s` (or an empty line) stops immediately and is always accepted.

#include <Arduino.h>

#include "Drive.h"
#include "config.h"

namespace {

constexpr uint32_t AUTO_STOP_MS = 3000;

const rcmap::SteerMap kSteerMap{STEER_CENTRE_US, STEER_LEFT_US, STEER_RIGHT_US};

Drive g_drive({PIN_ESC, LEDC_CHANNEL_ESC},
              {ESC_NEUTRAL_US, ESC_FORWARD_US, ESC_REVERSE_US, THROTTLE_DEADBAND,
               THROTTLE_LIMIT_FORWARD, THROTTLE_LIMIT_REVERSE, ESC_FORWARD_START_US,
               ESC_REVERSE_START_US},
              {PIN_SERVO_STEER, LEDC_CHANNEL_STEER}, kSteerMap, DRIVE_PWM_FREQUENCY_HZ,
              DRIVE_PWM_RESOLUTION_BITS);

float g_steer = 0.0f;  // last mapped steering demand, so a throttle command keeps it
uint32_t g_last_throttle_ms = 0;
bool g_throttle_live = false;

String g_line;

/// Why the board last reset. A steering servo stalled against its end stop can pull the
/// ESP32's supply down far enough to trip the brownout detector, and this is what tells
/// "the power rail sagged" apart from "the firmware crashed".
const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_EXT:      return "external pin";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic / exception";
    case ESP_RST_INT_WDT:  return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT:      return "other watchdog";
    case ESP_RST_BROWNOUT: return "BROWNOUT - the supply sagged";
    default:               return "other";
  }
}

void report() {
  Serial.printf("  -> ESC %4u us   steer %4u us\n", g_drive.escPulseUs(),
                g_drive.steerPulseUs());
}

void stopThrottle(const char *why) {
  g_drive.writeRawUs(ESC_NEUTRAL_US, g_drive.steerPulseUs());
  g_throttle_live = false;
  Serial.printf("  -> throttle NEUTRAL (%s)\n", why);
  report();
}

void help() {
  Serial.println();
  Serial.println("  ESC + steering bench   (wheels OFF the ground)");
  Serial.println("  f <0..1>   forward through Drive (speed cap applied)");
  Serial.println("  b <0..1>   reverse through Drive (speed cap applied)");
  Serial.println("  l <0..1>   steer left  through Drive");
  Serial.println("  r <0..1>   steer right through Drive");
  Serial.println("  c          centre steering");
  Serial.println("  e <us>     RAW ESC pulse   800..2200 — NO speed cap");
  Serial.println("  t <us>     RAW steer pulse 800..2200 — find the end stops");
  Serial.println("  s / Enter  stop: neutral + centre");
  Serial.println("  ?          this help and the config in force");
  Serial.printf("  config: ESC %u/%u/%u us (rev/neutral/fwd), cap fwd %.2f rev %.2f\n",
                ESC_REVERSE_US, ESC_NEUTRAL_US, ESC_FORWARD_US, THROTTLE_LIMIT_FORWARD,
                THROTTLE_LIMIT_REVERSE);
  Serial.printf("          motor start fwd %u us, rev %u us (the slowest speed)\n",
                ESC_FORWARD_START_US, ESC_REVERSE_START_US);
  Serial.printf("          steer %u/%u/%u us (left/centre/right)\n", STEER_LEFT_US,
                STEER_CENTRE_US, STEER_RIGHT_US);
  Serial.printf("  throttle returns to neutral %u ms after the last command\n",
                static_cast<unsigned>(AUTO_STOP_MS));
  report();
}

float fraction(const String &arg) {
  const float value = arg.length() ? arg.toFloat() : 1.0f;
  return constrain(value, 0.0f, 1.0f);
}

void handle(String line) {
  line.trim();
  if (line.length() == 0 || line == "s") {
    g_steer = 0.0f;
    g_drive.stop();
    g_throttle_live = false;
    Serial.println("  -> STOPPED");
    report();
    return;
  }
  if (line == "?" || line == "help") {
    help();
    return;
  }

  const char verb = line.charAt(0);
  const String arg = line.substring(1);
  const uint32_t now = millis();

  switch (verb) {
    case 'f':
    case 'b': {
      const float throttle = (verb == 'f' ? 1.0f : -1.0f) * fraction(arg);
      g_drive.setDrive(throttle, g_steer);
      g_throttle_live = true;
      g_last_throttle_ms = now;
      Serial.printf("  -> throttle %+.2f\n", throttle);
      break;
    }
    case 'l':
    case 'r':
    case 'c': {
      g_steer = verb == 'c' ? 0.0f : (verb == 'l' ? -1.0f : 1.0f) * fraction(arg);
      // Steering only: the ESC keeps exactly what it had, so a running motor does not see
      // even one frame of neutral. Same mapping lib/Drive uses.
      g_drive.writeRawUs(g_drive.escPulseUs(), rcmap::steerPulseUs(kSteerMap, g_steer));
      Serial.printf("  -> steer %+.2f\n", g_steer);
      break;
    }
    case 'e': {
      const long us = arg.toInt();
      g_drive.writeRawUs(static_cast<uint16_t>(constrain(us, 800L, 2200L)),
                         g_drive.steerPulseUs());
      g_throttle_live = g_drive.escPulseUs() != ESC_NEUTRAL_US;
      g_last_throttle_ms = now;
      Serial.println("  -> RAW ESC (no speed cap)");
      break;
    }
    case 't': {
      const long us = arg.toInt();
      g_drive.writeRawUs(g_drive.escPulseUs(),
                         static_cast<uint16_t>(constrain(us, 800L, 2200L)));
      Serial.println("  -> RAW steer — listen for the servo buzzing at an end stop");
      break;
    }
    default:
      Serial.printf("  ? unknown '%s' — type ? for help\n", line.c_str());
      return;
  }
  report();
}

}  // namespace

void setup() {
  // Neutral / centre before anything else, so the ESC arms on a clean signal.
  g_drive.begin();
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nESC + steering bench (config of fw %s)\n", FIRMWARE_VERSION);
  // With USB attached the monitor survives a reset, so a stall that browns the board
  // out shows up right here as the next boot's reason.
  Serial.printf("*** last reset: %s ***\n", resetReasonName(esp_reset_reason()));
  help();
}

void loop() {
  while (Serial.available()) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      handle(g_line);
      g_line = "";
    } else {
      g_line += ch;
    }
  }

  if (g_throttle_live && millis() - g_last_throttle_ms >= AUTO_STOP_MS) {
    stopThrottle("auto-stop — send a fresh command to keep going");
  }
}
