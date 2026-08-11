// Mars Rover — ESP32 rover firmware (plan/storyboard.md 5.2)
//
// Owns the control channel only: it accepts JSON commands from the Godot console
// over a WebSocket and publishes telemetry back. Camera video is deliberately not
// routed through here — each IP camera streams straight to its own monitor, so
// video load can never slow the drive controls (plan/storyboard.md 5.4, risk R1).
//
// Scaffold state: drive + telemetry. Arm servos (Phase 2) and mast pan/tilt
// (Phase 3) plug into handleCommand() alongside the "drive" case.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

#include "Drive.h"
#include "config.h"
#include "secrets.h"

namespace {

Drive g_drive({PIN_LEFT_IN1, PIN_LEFT_IN2, PIN_LEFT_PWM, LEDC_CHANNEL_LEFT},
              {PIN_RIGHT_IN1, PIN_RIGHT_IN2, PIN_RIGHT_PWM, LEDC_CHANNEL_RIGHT},
              PIN_MOTOR_STANDBY);

WebSocketsServer g_server(CONTROL_WS_PORT);

uint32_t g_last_command_ms = 0;
uint32_t g_last_telemetry_ms = 0;
bool g_failsafe_tripped = true;

float readBatteryVolts() {
  const int counts = analogRead(PIN_BATTERY_SENSE);
  return (counts * ADC_REFERENCE_V / ADC_MAX_COUNTS) * BATTERY_DIVIDER_RATIO;
}

void handleCommand(const JsonDocument &doc) {
  const char *cmd = doc["cmd"] | "";

  if (strcmp(cmd, "drive") == 0) {
    g_drive.setThrottle(doc["l"] | 0.0f, doc["r"] | 0.0f);
    g_last_command_ms = millis();
    g_failsafe_tripped = false;
  } else if (strcmp(cmd, "stop") == 0) {
    g_drive.stop();
    g_last_command_ms = millis();
  } else {
    // "arm" and "mast" land here until Phase 2/3 wires up the servos.
    log_w("Unhandled command: %s", cmd);
  }
}

void onWebSocketEvent(uint8_t client, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      log_i("Console %u connected", client);
      break;

    case WStype_DISCONNECTED:
      log_w("Console %u disconnected — stopping", client);
      g_drive.stop();
      g_failsafe_tripped = true;
      break;

    case WStype_TEXT: {
      JsonDocument doc;
      const DeserializationError err = deserializeJson(doc, payload, length);
      if (err) {
        log_e("Bad command JSON: %s", err.c_str());
        return;
      }
      handleCommand(doc);
      break;
    }

    default:
      break;
  }
}

void publishTelemetry() {
  JsonDocument doc;
  doc["battery_v"] = readBatteryVolts();
  doc["mode"] = g_failsafe_tripped ? "safe" : "drive";
  doc["rssi"] = WiFi.RSSI();  // Phase 1 checkpoint: signal through the glass (risk R6)

  String out;
  serializeJson(doc, out);
  g_server.broadcastTXT(out);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  g_drive.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
  }
  Serial.printf("\nRover online at ws://%s:%u/\n", WiFi.localIP().toString().c_str(),
                CONTROL_WS_PORT);

  g_server.begin();
  g_server.onEvent(onWebSocketEvent);
}

void loop() {
  g_server.loop();

  const uint32_t now = millis();

  // Failsafe: a silent console means a degraded link, not a command to keep driving.
  if (!g_failsafe_tripped && now - g_last_command_ms > COMMAND_TIMEOUT_MS) {
    log_w("Command timeout — stopping");
    g_drive.stop();
    g_failsafe_tripped = true;
  }

  if (now - g_last_telemetry_ms >= TELEMETRY_INTERVAL_MS) {
    g_last_telemetry_ms = now;
    publishTelemetry();
  }
}
