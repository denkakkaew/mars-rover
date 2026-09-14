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

Drive g_drive({PIN_DRIVE_IN1, PIN_DRIVE_IN2, LEDC_CHANNEL_DRIVE_IN1, LEDC_CHANNEL_DRIVE_IN2},
              {PIN_STEER_IN1, PIN_STEER_IN2});

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


// ---- Wi-Fi, without ever blocking ----------------------------------------------------
//
// This used to be `while (WiFi.status() != WL_CONNECTED) delay(250);` in setup(), which
// meant a wrong password, a downed AP or a rover parked in a Wi-Fi hole left the board
// stuck in setup() forever: no WebSocket server, no telemetry, and — from the console's
// side — indistinguishable from dead silicon. That is IMPLEMENTATION_PLAN.md finding F7,
// and it is precisely the confusion step 1.1's Wi-Fi-free bring-up sketch was designed to
// avoid, reintroduced one step later.
//
// It matters most exactly when the rover is least reachable: running on battery with no
// serial cable attached, which is the only way it will ever run in the arena.
//
// So: try, carry on regardless, and keep retrying in the background. The rover comes up
// whether or not the network does, the WebSocket server is listening the moment an
// address arrives, and an AP that returns recovers the rover without a power cycle. None
// of this weakens the failsafe — an unassociated rover receives no commands, so
// Safety times it out and the motors stay cut (docs/protocol.md 6.2).

uint32_t g_wifi_attempt_ms = 0;
bool g_wifi_up = false;

void beginWifi(uint32_t now) {
  g_wifi_attempt_ms = now;

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
  // (docs/protocol.md 4.5). The minimum barely moves, which is what identifies the cause:
  // the link was always fast, the radio was asleep.
  //
  // Reapplied on every join attempt rather than once at boot, so a reconnect cannot
  // silently come back with power save on. Do not re-enable it to save battery without
  // re-measuring latency: R1 is the risk this project is most exposed to.
  WiFi.setSleep(false);

  // A fixed address, so the console can be pointed at the rover without first reading a
  // DHCP lease off the serial monitor — which does not exist once the rover is
  // free-running on battery (config.h, ROVER_STATIC_IP). An empty ROVER_STATIC_IP, or a
  // router that refuses the address, falls back to DHCP; the log line on association
  // reports the address actually in force either way.
  if (ROVER_STATIC_IP[0] != 0) {
    IPAddress ip, gateway, subnet;
    if (ip.fromString(ROVER_STATIC_IP) && gateway.fromString(ROVER_GATEWAY_IP) &&
        subnet.fromString(ROVER_SUBNET_MASK)) {
      if (!WiFi.config(ip, gateway, subnet, gateway)) {
        log_e("Static IP %s refused — falling back to DHCP", ROVER_STATIC_IP);
      }
    } else {
      log_e("Static IP config is not parseable — falling back to DHCP");
    }
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  log_i("Joining %s", WIFI_SSID);
}

void serviceWifi(uint32_t now) {
  const bool up = (WiFi.status() == WL_CONNECTED);

  if (up != g_wifi_up) {
    g_wifi_up = up;
    if (up) {
      log_i("Rover online at ws://%s:%u/ (protocol v%d, fw %s)",
            WiFi.localIP().toString().c_str(), CONTROL_WS_PORT, protocol::kVersion,
            FIRMWARE_VERSION);
    } else {
      // Nothing to do about the motors here: no commands are arriving, so the failsafe
      // has already cut them. This line exists so the reason is on the record.
      log_w("Wi-Fi lost — retrying; the failsafe has already stopped the motors");
    }
    return;
  }

  if (!up && (now - g_wifi_attempt_ms) >= WIFI_RETRY_INTERVAL_MS) {
    beginWifi(now);
  }
}

// ---- Status LED ----------------------------------------------------------------------
//
// On battery there is no serial monitor, so this is the whole of the rover's ability to
// say what it is doing. Four states, chosen so the two that matter most in the arena are
// the two that cannot be mistaken for each other: a rover that cannot find the network
// flickers, and a rover whose motors are live is steady.
//
//     fast flicker   120 ms on, 120 ms off   hunting for the AP — check secrets.h, range
//     brief pulse     80 ms on, 1420 ms off  associated, waiting for a console
//     even blink     500 ms on, 500 ms off   console linked, safe — not armed
//     solid on                               ARMED: a command can move the rover now
//
// Anything else — dark, or a repeating boot flicker — is the board resetting, which on
// battery usually means the supply is sagging under motor current rather than a firmware
// fault (docs/power-budget.md 3.2).

struct BlinkPattern {
  uint16_t on_ms;
  uint16_t period_ms;
};

BlinkPattern statusPattern() {
  if (!g_wifi_up) return {120, 240};
  if (g_state == safety::State::Armed) return {1000, 1000};
  if (g_failsafe.inputs().connected) return {500, 1000};
  return {80, 1500};
}

void serviceStatusLed(uint32_t now) {
  const BlinkPattern pattern = statusPattern();
  const bool on = (now % pattern.period_ms) < pattern.on_ms;
  digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
}

}  // namespace

void setup() {
  // Motor pins first, before anything that can take time. GPIO14 is a bootstrap pin and
  // the ROM loader drives it while the board comes up, so the drive enable is briefly
  // outside this firmware's control on every reset; this is the earliest point at which
  // it can be pulled down and the motors made safe.
  g_drive.begin();

  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  Serial.begin(115200);

  // Order matters here, and it is narrower than it looks. The server must come up:
  //
  //   * AFTER the first WiFi call, because that is what initialises lwIP. Calling
  //     g_server.begin() first aborts the board in setup() with
  //     "assert failed: tcpip_send_msg_wait_sem ... (Invalid mbox)" — a socket asked for
  //     before the TCP/IP task exists — and the panic reboots, so the symptom is a boot
  //     loop rather than a message.
  //   * BEFORE association completes, because association is not waited for any more.
  //     A server started only on a successful join would never start at all on a rover
  //     that came up out of range.
  //
  // beginWifi() satisfies the first by calling WiFi.mode()/WiFi.begin(); it does not
  // block, so the second is free. The listening socket binds to INADDR_ANY and simply
  // has no callers until an address arrives.
  beginWifi(millis());

  g_server.begin();
  g_server.onEvent(onWebSocketEvent);
}

void loop() {
  g_server.loop();

  const uint32_t now = millis();

  serviceWifi(now);
  serviceStatusLed(now);

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
