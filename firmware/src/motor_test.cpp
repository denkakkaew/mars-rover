// Motor bench test — IMPLEMENTATION_PLAN.md step 1.3.
//
//   python -m platformio run -e motortest -t upload -t monitor
//
// Drives the motors from serial commands, with **no Wi-Fi, no console and no protocol**.
// Same reasoning as bringup.cpp at step 1.1: prove the thing in front of you before
// adding a radio link to the list of things that might be wrong. A motor that will not
// turn is a very different investigation from a console that will not connect, and
// running both at once makes each one look like the other.
//
// It drives through **lib/Drive**, not through raw digitalWrite, so what gets tested is
// the code the rover will actually run — including the throttle deadband and the
// three-position steering threshold. The one exception is `pins`, which bypasses Drive
// deliberately to check wiring with a meter.
//
// Safety, because this is the first code in the project that can move something:
//
//   * Everything starts stopped and stays stopped until told otherwise.
//   * A running motor **stops itself after AUTO_STOP_MS** unless a fresh command
//     arrives. Same instinct as the rover's failsafe: nothing on this bench should keep
//     running because someone walked away from the keyboard.
//   * `s` stops immediately and is always accepted.

#include <Arduino.h>

#include "Drive.h"
#include "config.h"

namespace {

/// A motor left running while nobody is typing is how a bench test drags a rover off a
/// desk. Every command restarts this.
constexpr uint32_t AUTO_STOP_MS = 5000;

Drive g_drive({PIN_DRIVE_IN1, PIN_DRIVE_IN2, LEDC_CHANNEL_DRIVE_IN1, LEDC_CHANNEL_DRIVE_IN2},
              {PIN_STEER_IN1, PIN_STEER_IN2});

float g_throttle = 0.0f;
float g_steer = 0.0f;
uint32_t g_last_command_ms = 0;
bool g_moving = false;

// Ramp state — the deadband hunt (step 1.3's actual measurement).
bool g_ramping = false;
float g_ramp = 0.0f;
uint32_t g_last_ramp_ms = 0;
constexpr uint32_t RAMP_STEP_MS = 400;
constexpr float RAMP_INCREMENT = 0.02f;

void apply(float throttle, float steer) {
  g_throttle = throttle;
  g_steer = steer;
  g_drive.setDrive(throttle, steer);
  g_last_command_ms = millis();
  g_moving = (throttle != 0.0f) || (steer != 0.0f);
  Serial.printf("  -> fwd %+.2f   steer %+.2f\n", throttle, steer);
}

void stopAll(const char *why) {
  g_ramping = false;
  g_ramp = 0.0f;
  g_throttle = g_steer = 0.0f;
  g_moving = false;
  g_drive.stop();
  Serial.printf("  -> STOPPED (%s)\n", why);
}

/// Wiring check. Toggles one output at a time with the motors ideally DISCONNECTED, so
/// each pin can be confirmed with a multimeter before anything can move. Bypasses Drive
/// on purpose — this asks "is the wire where I think it is", not "does the module work".
void pinWalk() {
  struct Entry { const char *name; uint8_t pin; };
  const Entry entries[] = {
      {"L293D   1A+3A  drive fwd", PIN_DRIVE_IN1},
      {"L293D   2A+4A  drive rev", PIN_DRIVE_IN2},
      {"DRV8833 AIN1   steer R  ", PIN_STEER_IN1},
      {"DRV8833 AIN2   steer L  ", PIN_STEER_IN2},
  };

  Serial.println("\n  pin walk — each output goes HIGH for 2 s, in order.");
  Serial.println("  Meter each one against GND. Disconnect the motors first.\n");

  // Both drive inputs are on LEDC since the enables went to VCC; detach them so
  // digitalWrite works. The steering pins are already plain outputs.
  //
  // This walk is the fastest way to catch a repeat of the GPIO26 failure that forced the
  // 2026-08-30 pin move: a healthy output meters ~3.3 V here, and anything markedly below
  // that with the motors disconnected is a damaged pin, not a firmware problem.
  ledcDetachPin(PIN_DRIVE_IN1);
  ledcDetachPin(PIN_DRIVE_IN2);

  for (const Entry &e : entries) {
    pinMode(e.pin, OUTPUT);
    digitalWrite(e.pin, LOW);
  }

  for (const Entry &e : entries) {
    Serial.printf("    %s  GPIO%-2d  HIGH ... ", e.name, e.pin);
    Serial.flush();
    digitalWrite(e.pin, HIGH);
    delay(2000);
    digitalWrite(e.pin, LOW);
    Serial.println("low again");
  }

  // Hand the pins back to Drive.
  g_drive.begin();
  Serial.println("\n  pin walk done — Drive reinitialised, everything stopped.\n");
}

void help() {
  Serial.println();
  Serial.println("  commands (one per line):");
  Serial.println("    pins      walk each output HIGH for 2 s — wiring check, motors OFF");
  Serial.println("    f <pct>   drive motor forward, 0-100   e.g.  f 60");
  Serial.println("    b <pct>   drive motor backward");
  Serial.println("    l         steer full left      (steer = -1)");
  Serial.println("    r         steer full right     (steer = +1)");
  Serial.println("    c         steer centre         (steer =  0, spring recentres)");
  Serial.println("    ramp      creep the throttle up 2% every 400 ms — say when it moves");
  Serial.println("    s         stop everything");
  Serial.println("    ?         this help");
  Serial.println();
  Serial.printf("  a moving motor auto-stops after %lu ms without a command\n",
                static_cast<unsigned long>(AUTO_STOP_MS));
  Serial.println();
}

void handleLine(String line) {
  line.trim();
  if (line.isEmpty()) return;
  line.toLowerCase();

  if (line == "s" || line == "stop") { stopAll("commanded"); return; }
  if (line == "?" || line == "help") { help(); return; }
  if (line == "pins") { stopAll("pin walk"); pinWalk(); return; }

  if (line == "l") { apply(g_throttle, -1.0f); return; }
  if (line == "r") { apply(g_throttle, 1.0f); return; }
  if (line == "c") { apply(g_throttle, 0.0f); return; }

  if (line == "ramp") {
    g_ramping = true;
    g_ramp = 0.0f;
    g_last_ramp_ms = millis();
    Serial.println("  ramping from 0 — watch the shaft, note the first movement.");
    Serial.println("  That figure is the breakaway throttle step 1.5 needs. `s` to stop.");
    return;
  }

  if ((line.startsWith("f ") || line.startsWith("b ")) && line.length() > 2) {
    const float pct = constrain(line.substring(2).toFloat(), 0.0f, 100.0f);
    const float sign = line.startsWith("f") ? 1.0f : -1.0f;
    g_ramping = false;
    apply(sign * pct / 100.0f, g_steer);
    return;
  }

  Serial.printf("  unrecognised: '%s' — try ?\n", line.c_str());
}

void serviceRamp() {
  if (!g_ramping) return;
  const uint32_t now = millis();
  if (now - g_last_ramp_ms < RAMP_STEP_MS) return;
  g_last_ramp_ms = now;

  g_ramp += RAMP_INCREMENT;
  if (g_ramp > 1.0f) {
    Serial.println("  ramp reached 100% — stopping.");
    stopAll("ramp complete");
    return;
  }
  // Straight to Drive: the ramp has to sweep *through* the deadband to find it, so it
  // must not be gated by the auto-stop that apply() arms.
  g_drive.setDrive(g_ramp, g_steer);
  g_last_command_ms = now;
  g_moving = true;
  Serial.printf("  ramp %5.0f %%\n", g_ramp * 100.0f);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  g_drive.begin();  // leaves everything stopped

  Serial.println();
  Serial.println("=== Mars rover — motor bench test (step 1.3) ===");
  Serial.println("drive  L293D,   both enables to VCC — PWM on the inputs, 1.5 kHz");
  Serial.println("steer  DRV8833, STBY to VCC        — no PWM, three positions");
  Serial.printf("drive  1A+3A GPIO%-2d (LEDC %d)   2A+4A GPIO%-2d (LEDC %d)\n",
                PIN_DRIVE_IN1, LEDC_CHANNEL_DRIVE_IN1,
                PIN_DRIVE_IN2, LEDC_CHANNEL_DRIVE_IN2);
  Serial.printf("steer  AIN1  GPIO%-2d            AIN2  GPIO%-2d\n",
                PIN_STEER_IN1, PIN_STEER_IN2);
  Serial.println("600 mA per L293D channel, one drive motor each — still watch for heat.");
  Serial.println();
  Serial.println("Motors should be FREE-SPINNING, wheels off, chassis nowhere near this.");
  help();
}

void loop() {
  static String line;

  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (line.length()) { handleLine(line); line = ""; }
    } else if (line.length() < 32) {
      line += c;
    }
  }

  serviceRamp();

  if (g_moving && millis() - g_last_command_ms >= AUTO_STOP_MS) {
    stopAll("auto-stop, no command");
  }
}
