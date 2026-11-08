// GPIO health bench — is a pin broken, or is something on it holding it?
//
//   python -m platformio run -e gpiotest -t upload -t monitor
//
// Written 2026-11-07, after GPIO26 (0.9 V driving high, 2026-08-30) and GPIO27 failed on
// this board and GPIO32 came under suspicion. No Wi-Fi, no protocol, nothing from config.h
// except the pin names, so a result here is about the silicon and the wiring only.
//
// **Before running: switch the ESC off (or unplug its battery).** The auto test drives
// every candidate pin high for a few milliseconds, and the ESC is on one of them.
//
// Commands (type, then Enter):
//
//   a            auto test every output-capable pin (below)
//   h <pin>      hold <pin> HIGH  -> a multimeter to GND should read 3.2-3.3 V
//   l <pin>      hold <pin> LOW   -> should read ~0 V
//   z <pin>      release <pin> (input, no pull) — what is the wiring doing to it?
//   p <pin> [us] 50 Hz servo pulse of [us] (default 1500) on <pin>, measured back on the
//                same pin. A multimeter on DC reads ~0.25 V for 1500 us (3.3 V x 1.5/20).
//   s            stop any pulse, release every pin
//
// What the auto test does, per pin, reading the pad back through its own input buffer:
//
//   1. internal pull-up   -> must read 1     } fails if something outside holds the pin:
//   2. internal pull-down -> must read 0     } a resistor, a wire, a short — or a dead input
//   3. drive HIGH         -> must read 1     } fails if the output driver cannot hold the
//   4. drive LOW          -> must read 0     } level: a damaged driver, or a short
//
// 3 and 4 are the ones that find a broken pin. GPIO26's fault (0.9 V when driving high)
// reads 0 at step 3, because the input only calls 1 above about 2.5 V. Steps 1-2 mostly
// find what is wired on: the 10 k pull-downs on GPIO32/33/18/19 (docs/esc-wiring.md)
// beat the ~45 k internal pull-up, so on those pins step 1 is EXPECTED to read 0.
//
// It cannot see a driver that holds the level with no load but sags under one. For that,
// `h <pin>` with the real load connected and a multimeter is the test.

#include <Arduino.h>

#include "config.h"
#include "soc/gpio_periph.h"
#include "soc/io_mux_reg.h"

namespace {

// Every pin on a 38-pin ESP32 devkit that can drive an output and is free to toggle at
// runtime. Left out: 6-11 (the SPI flash — touching them crashes the board), 1/3 (the
// USB serial port), 0 (boot strap, on the BOOT button), 34-39 (input only, no pulls).
const uint8_t kPins[] = {2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27,
                         32, 33};

constexpr uint8_t PULSE_LEDC_CHANNEL = 0;
int g_pulse_pin = -1;

String g_line;

bool isCandidate(int pin) {
  for (uint8_t p : kPins) {
    if (p == pin) return true;
  }
  return false;
}

const char *role(uint8_t pin) {
  if (pin == PIN_ESC) return "ESC signal";
  if (pin == PIN_SERVO_STEER) return "steering servo";
  if (pin == PIN_SERVO_CAM_PAN) return "camera pan";
  if (pin == PIN_SERVO_CAM_TILT) return "camera tilt";
  if (pin == PIN_STATUS_LED) return "status LED";
  if (pin == 26 || pin == 27) return "known bad";
  return "";
}

int readAfter(uint8_t pin, uint8_t mode, int level = -1) {
  pinMode(pin, mode);
  if (level >= 0) digitalWrite(pin, level);
  delayMicroseconds(500);  // let the pad settle through whatever is wired on
  return digitalRead(pin);
}

void stopPulse() {
  if (g_pulse_pin < 0) return;
  ledcDetachPin(g_pulse_pin);
  pinMode(g_pulse_pin, INPUT);
  g_pulse_pin = -1;
}

void autoTest() {
  stopPulse();
  Serial.println();
  Serial.println("  pin  pull-up pull-dn drive-H drive-L  verdict");
  int broken = 0;
  for (uint8_t pin : kPins) {
    const int up = readAfter(pin, INPUT_PULLUP);
    const int down = readAfter(pin, INPUT_PULLDOWN);
    const int high = readAfter(pin, OUTPUT, HIGH);
    const int low = readAfter(pin, OUTPUT, LOW);
    pinMode(pin, INPUT);  // back to high impedance — leave nothing driven

    const char *verdict;
    if (high != 1 && low != 0) {
      verdict = "BROKEN - cannot drive either way";
    } else if (high != 1) {
      verdict = "BROKEN - cannot drive HIGH (or shorted to GND)";
    } else if (low != 0) {
      verdict = "BROKEN - cannot drive LOW (or shorted to 3.3/5 V)";
    } else if (up == 1 && down == 0) {
      verdict = "ok";
    } else if (up == 0 && down == 0) {
      verdict = "ok - held LOW from outside (a pull-down, or a load)";
    } else if (up == 1 && down == 1) {
      verdict = "ok - held HIGH from outside (a pull-up, or a 5 V source?)";
    } else {
      verdict = "ok to drive - input reads inverted, recheck";
    }
    if (high != 1 || low != 0) ++broken;

    Serial.printf("  %3u     %d       %d       %d       %d     %s", pin, up, down, high, low,
                  verdict);
    const char *r = role(pin);
    if (r[0]) Serial.printf("   [%s]", r);
    Serial.println();
  }
  digitalWrite(PIN_STATUS_LED, LOW);
  Serial.printf("\n  %d pin(s) failed to drive. Every pin is released (input, no pull).\n",
                broken);
}

void pulse(int pin, int us) {
  stopPulse();
  us = constrain(us, 500, 2500);
  ledcSetup(PULSE_LEDC_CHANNEL, 50, 16);
  ledcAttachPin(pin, PULSE_LEDC_CHANNEL);
  // LEDC configures the pad output-only. Turn its input buffer on as well — without
  // re-routing it, which gpio_set_direction() would do — so pulseIn() can measure the
  // pulse the pad is actually producing, not the one the timer was asked for.
  // (FUN_IE directly: the PIN_INPUT_ENABLE macro's DPORT compile-time assert warns when
  // expanded inside C++.)
  *reinterpret_cast<volatile uint32_t *>(GPIO_PIN_MUX_REG[pin]) |= FUN_IE;
  ledcWrite(PULSE_LEDC_CHANNEL, (static_cast<uint32_t>(us) * 50u * 65536u) / 1000000u);
  g_pulse_pin = pin;
  delay(100);

  unsigned long total = 0;
  int got = 0;
  for (int i = 0; i < 5; ++i) {
    const unsigned long width = pulseIn(pin, HIGH, 60000);
    if (width > 0) {
      total += width;
      ++got;
    }
  }
  if (got == 0) {
    Serial.printf("  -> GPIO%d asked for %d us at 50 Hz, measured NOTHING: the pad never went "
                  "high. Broken output, or the pin is shorted to GND.\n", pin, us);
  } else {
    Serial.printf("  -> GPIO%d asked for %d us, measured %lu us (%d/5 frames). Within ~10 us "
                  "is healthy.\n", pin, us, total / got, got);
  }
  Serial.println("     The pulse keeps running — `s` to stop.");
}

void help() {
  Serial.println();
  Serial.println("  GPIO health bench   (ESC switched OFF first)");
  Serial.println("  a            auto test every output-capable pin");
  Serial.println("  h <pin>      hold HIGH  (multimeter: 3.2-3.3 V)");
  Serial.println("  l <pin>      hold LOW   (multimeter: ~0 V)");
  Serial.println("  z <pin>      release: input, no pull");
  Serial.println("  p <pin> [us] 50 Hz servo pulse, measured back (default 1500 us)");
  Serial.println("  s            stop the pulse, release every pin");
}

void handle(String line) {
  line.trim();
  if (line.length() == 0) return;
  const char verb = line.charAt(0);
  String rest = line.substring(1);
  rest.trim();
  const int space = rest.indexOf(' ');
  const int pin = (space < 0 ? rest : rest.substring(0, space)).toInt();
  const int extra = space < 0 ? 0 : rest.substring(space + 1).toInt();

  switch (verb) {
    case 'a':
      autoTest();
      return;
    case 's':
      stopPulse();
      for (uint8_t p : kPins) pinMode(p, INPUT);
      Serial.println("  -> everything released");
      return;
    case '?':
      help();
      return;
    case 'h':
    case 'l':
    case 'z':
    case 'p':
      break;
    default:
      Serial.printf("  ? unknown '%s' — type ? for help\n", line.c_str());
      return;
  }

  if (!isCandidate(pin)) {
    Serial.printf("  ? GPIO%d is not on the test list (6-11 flash, 1/3 serial, 0 boot, "
                  "34-39 input-only)\n", pin);
    return;
  }
  if (pin == g_pulse_pin) stopPulse();

  switch (verb) {
    case 'h':
      pinMode(pin, OUTPUT);
      digitalWrite(pin, HIGH);
      Serial.printf("  -> GPIO%d HIGH, reads back %d. Measure it to GND: 3.2-3.3 V is "
                    "healthy; well below that under its load is a weak driver.\n",
                    pin, digitalRead(pin));
      break;
    case 'l':
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
      Serial.printf("  -> GPIO%d LOW, reads back %d. Should measure ~0 V.\n", pin,
                    digitalRead(pin));
      break;
    case 'z':
      pinMode(pin, INPUT);
      delay(5);
      Serial.printf("  -> GPIO%d released, reads %d. Measure it: whatever you see is what "
                    "the wiring puts on the pin. Above 3.6 V is damaging it.\n",
                    pin, digitalRead(pin));
      break;
    case 'p':
      pulse(pin, extra > 0 ? extra : 1500);
      break;
  }
}

}  // namespace

void setup() {
  for (uint8_t p : kPins) pinMode(p, INPUT);
  Serial.begin(115200);
  delay(300);
  Serial.println("\nGPIO health bench");
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
}
