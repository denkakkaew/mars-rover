// Mars Rover — ESP32 rover firmware (plan/storyboard.md 5.2)
//
// Transport and glue only. Everything with a decision in it lives in a library that
// compiles on the host and is unit-tested there, with no ESP32 attached
// (IMPLEMENTATION_PLAN.md S.3/S.4):
//
//   lib/Protocol — JSON frame <-> typed Command, and telemetry serialisation
//   lib/Safety   — the failsafe state machine
//   lib/Drive    — H-bridge throttle
//
// What is left here is the part that genuinely needs the board: Wi-Fi, the WebSocket
// server, the battery ADC, and the wiring between those three. Camera video is
// deliberately not routed through here — each IP camera streams straight to its own
// monitor, so video load can never slow the drive controls (plan/storyboard.md 5.4,
// risk R1).

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

#include "Drive.h"
#include "Protocol.h"
#include "Safety.h"
#include "config.h"
#include "secrets.h"

namespace {

Drive g_drive({PIN_LEFT_IN1, PIN_LEFT_IN2, PIN_LEFT_PWM, LEDC_CHANNEL_LEFT},
              {PIN_RIGHT_IN1, PIN_RIGHT_IN2, PIN_RIGHT_PWM, LEDC_CHANNEL_RIGHT},
              PIN_MOTOR_STANDBY);

WebSocketsServer g_server(CONTROL_WS_PORT);
safety::Failsafe g_failsafe(COMMAND_TIMEOUT_MS);

safety::State g_state = safety::State::Safe;
uint32_t g_last_telemetry_ms = 0;

// Subsystems this build actually actuates (docs/protocol.md 4.1). "mast" and "arm"
// join the list when Phases 3 and 2 wire the servos up; until then the console greys
// those controls out rather than sending commands into a void.
const char *const kCapabilities[] = {"drive"};

// One shared outbound buffer. Every frame is built and sent within a single call, and
// the WebSocket library copies before returning, so there is nothing to overlap.
char g_out[protocol::kMaxFrameBytes];

/// Sends whatever is in `g_out`. A zero length means the serialiser refused to write a
/// frame that would not fit; dropping it is correct, since a truncated JSON object would
/// reach the console as a parse error rather than as an obviously missing frame.
void sendFrame(uint8_t client, size_t length) {
  if (length == 0) {
    log_e("Outbound frame did not fit in %u bytes — dropped",
          static_cast<unsigned>(sizeof(g_out)));
    return;
  }
  g_server.sendTXT(client, g_out, length);
}

float readBatteryVolts() {
  const int counts = analogRead(PIN_BATTERY_SENSE);
  return (counts * ADC_REFERENCE_V / ADC_MAX_COUNTS) * BATTERY_DIVIDER_RATIO;
}

protocol::Mode reportedMode(safety::State state) {
  switch (state) {
    case safety::State::Armed: return protocol::Mode::Drive;
    case safety::State::Incompatible: return protocol::Mode::Incompatible;
    case safety::State::Safe: break;
  }
  return protocol::Mode::Safe;
}

/// Adopts a new failsafe state, cutting drive current on any exit from Armed.
void applyState(safety::State next) {
  if (next == g_state) return;
  if (next != safety::State::Armed) {
    log_w("Failsafe: entering %s — stopping",
          protocol::modeName(reportedMode(next)));
    g_drive.stop();
  }
  g_state = next;
}

void handleCommand(uint8_t client, const protocol::Command &cmd, uint32_t now) {
  // Neither a broken frame nor an unrecognised verb is evidence of a live console, so
  // neither refreshes the failsafe timer (docs/protocol.md 6.2).
  if (cmd.type == protocol::CommandType::Malformed) {
    log_e("Dropped frame: parse error %u", static_cast<unsigned>(cmd.error));
    return;
  }
  if (cmd.type == protocol::CommandType::Unknown) {
    log_w("Unknown command — ignored");
    return;
  }

  if (cmd.type == protocol::CommandType::Hello) {
    const bool compatible = cmd.version == protocol::kVersion;
    if (!compatible) {
      log_e("Console speaks protocol v%d, rover speaks v%d — refusing to arm",
            cmd.version, protocol::kVersion);
    }
    g_failsafe.setPeerCompatible(compatible);
  }

  if (protocol::refreshesFailsafe(cmd.type)) {
    g_failsafe.onCommand(now);
  }
  applyState(g_failsafe.state(now));

  switch (cmd.type) {
    case protocol::CommandType::Hello: {
      const size_t length =
          protocol::serializeHello(protocol::kVersion, FIRMWARE_VERSION, kCapabilities,
                                   sizeof(kCapabilities) / sizeof(kCapabilities[0]),
                                   g_out, sizeof(g_out));
      sendFrame(client, length);
      break;
    }

    case protocol::CommandType::Ping: {
      // Answered ahead of any queued telemetry, so the figure measures the link rather
      // than our own scheduling (docs/protocol.md 4.3).
      const size_t length = protocol::serializePong(cmd.ts, g_out, sizeof(g_out));
      sendFrame(client, length);
      break;
    }

    case protocol::CommandType::Stop:
      // Honoured in every state, including safe mode and on a version mismatch.
      g_drive.stop();
      break;

    case protocol::CommandType::Drive:
      if (g_state == safety::State::Armed) {
        g_drive.setThrottle(cmd.left, cmd.right);
      }
      break;

    case protocol::CommandType::Mast:
    case protocol::CommandType::Arm:
      // Accepted shapes, not yet actuated: mast lands in Phase 3, arm in Phase 2.
      log_w("%s command accepted but not actuated yet", protocol::name(cmd.type));
      break;

    default:
      break;
  }
}

void onWebSocketEvent(uint8_t client, WStype_t type, uint8_t *payload, size_t length) {
  const uint32_t now = millis();

  switch (type) {
    case WStype_CONNECTED:
      log_i("Console %u connected", client);
      g_failsafe.onConnect();
      applyState(g_failsafe.state(now));
      break;

    case WStype_DISCONNECTED:
      log_w("Console %u disconnected — stopping", client);
      g_drive.stop();  // belt and braces: the state change below stops us too
      g_failsafe.onDisconnect();
      applyState(g_failsafe.state(now));
      break;

    case WStype_TEXT:
      handleCommand(client, protocol::parse(reinterpret_cast<const char *>(payload), length),
                    now);
      break;

    default:
      break;
  }
}

void publishTelemetry() {
  protocol::Telemetry telemetry;
  telemetry.battery_v = readBatteryVolts();
  telemetry.mode = reportedMode(g_state);
  telemetry.rssi = WiFi.RSSI();  // Phase 1 checkpoint: signal through the glass (risk R6)

  const size_t length = protocol::serializeTelemetry(telemetry, g_out, sizeof(g_out));
  if (length == 0) {
    log_e("Telemetry frame did not fit — dropped");
    return;
  }
  g_server.broadcastTXT(g_out, length);
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
  Serial.printf("\nRover online at ws://%s:%u/ (protocol v%d, fw %s)\n",
                WiFi.localIP().toString().c_str(), CONTROL_WS_PORT, protocol::kVersion,
                FIRMWARE_VERSION);

  g_server.begin();
  g_server.onEvent(onWebSocketEvent);
}

void loop() {
  g_server.loop();

  const uint32_t now = millis();

  // Re-evaluated every pass, so the command timeout trips on its own without needing
  // an inbound frame to notice it.
  applyState(g_failsafe.state(now));

  if (now - g_last_telemetry_ms >= TELEMETRY_INTERVAL_MS) {
    g_last_telemetry_ms = now;
    publishTelemetry();
  }
}
