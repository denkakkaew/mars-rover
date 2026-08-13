// Board bring-up sketch — IMPLEMENTATION_PLAN.md step 1.1.
//
// The smallest program that answers "does the toolchain reach real silicon": it blinks,
// it talks, it listens, and it says exactly which chip it is running on. Nothing else.
//
//   python -m platformio run -e bringup -t upload -t monitor
//
// Deliberately NOT in this file, because 1.1 comes before all of it:
//
//   * No Wi-Fi, and so no secrets.h. A board that cannot join an AP must still pass 1.1
//     — otherwise a wrong password looks identical to a dead board, which is exactly the
//     confusion this step exists to remove. Wi-Fi is step 1.2.
//   * No motor pins driven. Nothing is wired at 1.1, and energising pins that will later
//     be an H-bridge proves nothing and risks something.
//   * No protocol, no failsafe. Those are host-tested already (S.4) and need no board.
//
// It is a separate environment rather than a #define in main.cpp so that the real
// firmware never carries a debug mode that could be shipped by accident.

#include <Arduino.h>
#include <esp_system.h>

namespace {

// Most ESP32 DevKit-class boards put a user LED on GPIO2. Some have none at all, which
// is why the serial heartbeat below is the primary evidence and the LED is corroboration
// — a board that prints but does not blink has passed, and probably just lacks the LED.
// Override with -DBRINGUP_LED_PIN=<n> if this board differs.
#ifndef BRINGUP_LED_PIN
#define BRINGUP_LED_PIN 2
#endif

constexpr uint32_t kBlinkIntervalMs = 500;
constexpr uint32_t kHeartbeatMs = 2000;

uint32_t g_last_blink = 0;
uint32_t g_last_beat = 0;
uint32_t g_beats = 0;
bool g_led = false;

/// Human-readable reset cause. Worth printing on every boot: at step 1.7 a brownout from
/// a motor stall shows up here as ESP_RST_BROWNOUT, which is the difference between
/// "the firmware crashed" and "the power rail sagged" (docs/power-budget.md F6).
const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_EXT:      return "external pin";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic / exception";
    case ESP_RST_INT_WDT:  return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT:      return "other watchdog";
    case ESP_RST_DEEPSLEEP:return "deep sleep wake";
    case ESP_RST_BROWNOUT: return "BROWNOUT — check the supply";
    case ESP_RST_SDIO:     return "SDIO";
    default:               return "unknown";
  }
}

/// Everything step 1.1 asks us to "note the exact board variant" from. These are read
/// off the chip itself rather than assumed from platformio.ini, which is the whole
/// point: if `board = esp32dev` is wrong for this hardware, this is what says so.
void reportIdentity() {
  Serial.println();
  Serial.println("=== Mars rover — board bring-up (step 1.1) ===");
  Serial.printf("chip model     %s rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("cores          %d\n", ESP.getChipCores());
  Serial.printf("cpu freq       %u MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("flash size     %u bytes\n", ESP.getFlashChipSize());
  Serial.printf("flash speed    %u Hz\n", ESP.getFlashChipSpeed());
  Serial.printf("psram          %s\n",
                psramFound() ? "present" : "none");
  Serial.printf("efuse MAC      %012llX\n", ESP.getEfuseMac());
  Serial.printf("sdk            %s\n", ESP.getSdkVersion());
  Serial.printf("free heap      %u bytes\n", ESP.getFreeHeap());
  Serial.printf("reset reason   %s\n", resetReason());
  Serial.printf("LED pin        GPIO%d\n", BRINGUP_LED_PIN);
  Serial.println();
  Serial.println("Blinking at 2 Hz. Type anything — it will be echoed back, which is how");
  Serial.println("the host-to-board direction gets proven as well as board-to-host.");
  Serial.println();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  // The USB-serial bridge needs a moment after a reset before the host is listening;
  // without this the identity block is printed into the void and the operator sees a
  // board that "only prints heartbeats".
  delay(500);

  pinMode(BRINGUP_LED_PIN, OUTPUT);
  digitalWrite(BRINGUP_LED_PIN, LOW);

  reportIdentity();
}

void loop() {
  const uint32_t now = millis();

  if (now - g_last_blink >= kBlinkIntervalMs) {
    g_last_blink = now;
    g_led = !g_led;
    digitalWrite(BRINGUP_LED_PIN, g_led ? HIGH : LOW);
  }

  if (now - g_last_beat >= kHeartbeatMs) {
    g_last_beat = now;
    // Uptime as well as a counter: a board that silently resets shows a heartbeat number
    // that goes back to 1, which is easy to miss, and an uptime that does too, which is
    // not.
    Serial.printf("[%6u ms] heartbeat %u   heap %u\n", now, ++g_beats, ESP.getFreeHeap());
  }

  // Echo, so the host -> board path is proven too. Upload depends on that direction, so
  // a board that prints happily but never receives is a board that will fail to flash
  // next time — better to find that here than mid-way through step 1.3.
  while (Serial.available() > 0) {
    const int c = Serial.read();
    Serial.printf("echo: 0x%02X", c);
    if (isPrintable(c)) {
      Serial.printf(" '%c'", static_cast<char>(c));
    }
    Serial.println();
  }
}
