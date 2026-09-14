// Mars Rover — ESP32 rover firmware (plan/storyboard.md 5.2)
//
// Transport and glue only. Everything with a decision in it lives in a library that
// compiles on the host and is unit-tested there, with no ESP32 attached
// (IMPLEMENTATION_PLAN.md S.3/S.4):
//
//   lib/Protocol — JSON frame <-> typed Command, and telemetry serialisation
//   lib/Safety   — the failsafe state machine
//   lib/Drive    — H-bridge throttle and steering
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

Drive g_drive(
    {PIN_DRIVE_IN1, PIN_DRIVE_IN2, LEDC_CHANNEL_DRIVE_IN1, LEDC_CHANNEL_DRIVE_IN2},
    {PIN_STEER_IN1, PIN_STEER_IN2},
    PIN_MOTOR_STANDBY);

WebSocketsServer g_server(CONTROL_WS_PORT);
safety::Failsafe g_failsafe(COMMAND_TIMEOUT_MS);

safety::State g_state = safety::State::Safe;
uint32_t g_last_telemetry_ms = 0;

uint32_t g_connected_at_ms = 0;
bool g_hello_received = false;
bool g_handshake_expired = false;

// Subsystems this build actually has (docs/protocol.md 4.1). "rfid" joins the list when
// Phase 2 fits the reader and "mast" when Phase 3 fits the servos; until then the console
// greys those out rather than sending commands into a void.
//
// "steer3" declares three-position steering, so the console knows a `steer` of 0.3 will be
// rounded away rather than honoured. Exactly one steering token must accompany "drive" —
// a later proportional-steering build swaps it for "steerprop" and needs no version bump,
// which is the whole reason `steer` stayed a float on the wire (docs/protocol.md 3.2.1).
const char *const kCapabilities[] = {"drive", "steer3"};

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
    g_hello_received = true;
    g_failsafe.setPeerCompatible(compatible);
    g_failsafe.setHandshakeOk(compatible);
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
        g_drive.setDrive(cmd.throttle, cmd.steer);
      }
      break;

    case protocol::CommandType::Mast:
      // Accepted shape, not yet actuated — the pan/tilt servos land in Phase 3.
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
      g_connected_at_ms = now;
      g_hello_received = false;
      g_handshake_expired = false;
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

  // No reader hardware exists yet. Reporting `absent` rather than `fault` is the honest
  // answer and keeps the two distinguishable once one is fitted at step 2.5; the real
  // state comes from lib/Rfid at step 2.6.
  telemetry.reader = protocol::ReaderState::Absent;

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

  // Wi-Fi modem sleep off — measured at step 1.2, and the single largest latency win
  // available to this project. Associated STA mode defaults to WIFI_PS_MIN_MODEM, which
  // parks the radio between AP beacons; a control frame arriving mid-doze waits for the
  // next DTIM. Measured on real hardware, 80 probes each:
  //
  //     power save on   min  7.1   median 43.9   p95  99.3   max 125.1 ms
  //     power save off  min  8.2   median 12.1   p95  19.8   max  36.4 ms
  //
  // p95 sits *at* the 100 ms R1 target with it on and at a fifth of it with it off
  // (docs/protocol.md 4.5). The minimum barely moves, which is what identifies the
  // cause: the link was always fast, the radio was asleep.
  //
  // The cost is current — the radio no longer dozes. docs/power-budget.md already
  // budgets the ESP32 at 120 mA on that basis, so this is priced in rather than a
  // surprise for step 1.7. Do not re-enable power save to save battery without
  // re-measuring latency: R1 is the risk this project is most exposed to, and the
  // headroom bought here is what step 3.3 spends on video contention.
  WiFi.setSleep(false);

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

  // A console that never identifies itself is treated as an unknown protocol version
  // (docs/protocol.md 5). Nothing could have armed in the meantime — Safety refuses to
  // arm without a handshake — so this only changes what the telemetry strip reports,
  // from "safe" to "incompatible", which is the difference between the operator seeing
  // "it is waiting" and "it will never move".
  if (g_failsafe.inputs().connected && !g_hello_received && !g_handshake_expired &&
      safety::timedOut(g_connected_at_ms, now, HANDSHAKE_DEADLINE_MS)) {
    g_handshake_expired = true;
    log_e("No hello within %u ms — refusing to arm", HANDSHAKE_DEADLINE_MS);
    g_failsafe.setPeerCompatible(false);
  }

  // Re-evaluated every pass, so the command timeout trips on its own without needing
  // an inbound frame to notice it.
  applyState(g_failsafe.state(now));

  if (now - g_last_telemetry_ms >= TELEMETRY_INTERVAL_MS) {
    g_last_telemetry_ms = now;
    publishTelemetry();
  }
}
