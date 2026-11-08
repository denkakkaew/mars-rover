// Mars Rover — ESP32 rover firmware (plan/storyboard.md 5.2)
//
// Transport and glue only. Everything with a decision in it lives in a library that
// compiles on the host and is unit-tested there, with no ESP32 attached
// (IMPLEMENTATION_PLAN.md S.3/S.4):
//
//   lib/Protocol — JSON frame <-> typed Command, and telemetry serialisation
//   lib/Safety   — the failsafe state machine
//   lib/RcMap    — throttle/steering demand -> ESC and steering-servo pulse
//   lib/Drive    — the ESC and steering servo on LEDC
//   lib/Aim      — camera pan/tilt travel limits and angle -> pulse
//   lib/PanTilt  — the two head servos on LEDC
//
// What is left here is the part that genuinely needs the board: Wi-Fi, the WebSocket
// server, the battery ADC, and the wiring between those three. Camera video is
// deliberately not routed through here — each IP camera streams straight to its own
// monitor, so video load can never slow the drive controls (plan/storyboard.md 5.4,
// risk R1).

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <atomic>
#include <WiFi.h>

#include "Drive.h"
#include "PanTilt.h"
#include "Protocol.h"
#include "Safety.h"
#include "config.h"
#include "secrets.h"

namespace {

// The RC-car drivetrain: ESC + steering servo (docs/esc-wiring.md). Speed cap, deadband
// and direction are config.h's; the mapping is lib/RcMap's and is host-tested.
Drive g_drive({PIN_ESC, LEDC_CHANNEL_ESC},
              {ESC_NEUTRAL_US, ESC_FORWARD_US, ESC_REVERSE_US, THROTTLE_DEADBAND,
               THROTTLE_LIMIT_FORWARD, THROTTLE_LIMIT_REVERSE, ESC_FORWARD_START_US,
               ESC_REVERSE_START_US},
              {PIN_SERVO_STEER, LEDC_CHANNEL_STEER},
              {STEER_CENTRE_US, STEER_LEFT_US, STEER_RIGHT_US},
              DRIVE_PWM_FREQUENCY_HZ, DRIVE_PWM_RESOLUTION_BITS);

// The camera pan/tilt head: two D56MGs (docs/servo-wiring.md). Limits and direction are
// config.h's; the mapping is lib/Aim's and is host-tested.
PanTilt g_head(
    {PIN_SERVO_CAM_PAN, LEDC_CHANNEL_SERVO_CAM_PAN,
     {CAM_PAN_MIN_DEG, CAM_PAN_MAX_DEG, SERVO_PULSE_CENTRE_US, CAM_PAN_US_PER_DEG,
      SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US}},
    {PIN_SERVO_CAM_TILT, LEDC_CHANNEL_SERVO_CAM_TILT,
     {CAM_TILT_MIN_DEG, CAM_TILT_MAX_DEG, SERVO_PULSE_CENTRE_US, CAM_TILT_US_PER_DEG,
      SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US}},
    SERVO_PWM_FREQUENCY_HZ, SERVO_PWM_RESOLUTION_BITS);

WebSocketsServer g_server(CONTROL_WS_PORT);
safety::Failsafe g_failsafe(COMMAND_TIMEOUT_MS);

safety::State g_state = safety::State::Safe;
uint32_t g_last_telemetry_ms = 0;

// ---- Drive watchdog: a failsafe the main loop cannot block (2026-11-07) ---------------
//
// lib/Safety's command timeout is only enforced when loop() runs. On 2026-11-07 the link
// log showed loop() stuck for up to 4.3 s at a time inside the WebSocket library: an
// ESP32 WiFiClient write waits for the peer's TCP ACK, retrying up to 10 x 1 s, and while
// it waits nothing else on this core runs. A rover armed at that moment kept its last
// throttle and steering for the whole stall — 4 s of driving at the glass with no console.
//
// This task is the backstop. It runs at a higher priority than loopTask, so it preempts
// a loop that is blocked, and every DRIVE_WATCHDOG_PERIOD_MS it applies the same rule
// Safety does: no failsafe-refreshing command for COMMAND_TIMEOUT_MS while the drive is
// off neutral -> neutral + centre. It does not replace Safety (which still owns arming,
// the handshake and disconnects); it only guarantees the motor stops on time.
//
// Every write to g_drive goes through driveSet()/driveStop() under g_drive_mutex, so the
// task and the loop never interleave half a drive update, and the check-and-stop is one
// step: a command that arrives during the check is either seen as fresh or applied after
// the stop, never undone by it.
constexpr uint32_t DRIVE_WATCHDOG_PERIOD_MS = 20;

SemaphoreHandle_t g_drive_mutex = nullptr;
std::atomic<uint32_t> g_drive_refresh_ms{0};  // last command that refreshed the failsafe
std::atomic<bool> g_drive_moving{false};      // drive is off neutral/centre right now

void driveSet(float throttle, float steer) {
  xSemaphoreTake(g_drive_mutex, portMAX_DELAY);
  g_drive.setDrive(throttle, steer);
  g_drive_moving = g_drive.escPulseUs() != ESC_NEUTRAL_US ||
                   g_drive.steerPulseUs() != STEER_CENTRE_US;
  xSemaphoreGive(g_drive_mutex);
}

void driveStop() {
  xSemaphoreTake(g_drive_mutex, portMAX_DELAY);
  g_drive.stop();
  g_drive_moving = false;
  xSemaphoreGive(g_drive_mutex);
}

void driveWatchdogTask(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(DRIVE_WATCHDOG_PERIOD_MS));
    if (!g_drive_moving) continue;

    uint32_t silent_ms = 0;
    bool tripped = false;
    xSemaphoreTake(g_drive_mutex, portMAX_DELAY);
    const uint32_t now = millis();
    if (g_drive_moving && safety::timedOut(g_drive_refresh_ms, now, COMMAND_TIMEOUT_MS)) {
      g_drive.stop();
      g_drive_moving = false;
      tripped = true;
      silent_ms = now - g_drive_refresh_ms;
    }
    xSemaphoreGive(g_drive_mutex);

    if (tripped) {
      log_w("Drive watchdog: no command for %u ms — neutral + centre",
            static_cast<unsigned>(silent_ms));
    }
  }
}

// Set at boot if the last reset was a brownout. The LED then double-blinks for
// BROWNOUT_SIGNAL_MS (serviceStatusLed), because on battery there is no serial monitor
// to read the reason from - and a supply sag is the likeliest reason a free-running
// rover drops its link (a stalled servo, a motor breaking away).
bool g_brownout_boot = false;
constexpr uint32_t BROWNOUT_SIGNAL_MS = 15000;

uint32_t g_connected_at_ms = 0;
bool g_hello_received = false;
bool g_handshake_expired = false;

// Subsystems this build actually has (docs/protocol.md 4.1). "rfid" joins the list when
// Phase 2 fits the reader; until then the console greys it out rather than sending
// commands into a void.
//
// "mast" is the camera pan/tilt head, fitted 2026-09-19. The rover no longer has a moving
// mast, but the token keeps its wire name: renaming it would break every console for the
// sake of a word (docs/protocol.md 3.4).
//
// "steerprop" declares proportional steering: the RC-car chassis (fw 0.3.0) has a servo,
// so a `steer` of 0.3 is honoured as 30% of lock. It replaced "steer3" with no version
// bump, which is the whole reason `steer` stayed a float on the wire (docs/protocol.md
// 3.2.1). Exactly one steering token must accompany "drive".
const char *const kCapabilities[] = {"drive", "steerprop", "mast"};

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

float readBatteryVolts() {
  // Calibrated millivolts (eFuse Vref, attenuation applied), not raw counts.
  return (analogReadMilliVolts(PIN_BATTERY_SENSE) / 1000.0f) * BATTERY_DIVIDER_RATIO;
}

// ---- Link-gap diagnostics (2026-11-07) -----------------------------------------------
//
// Added to chase the console's TELEMETRY STALE with no load and USB power, where the board
// was shown not to reset. A linked console pings every 250 ms (rover_link.gd
// PING_INTERVAL_SEC), so silence from it longer than LINK_GAP_LOG_MS means frames are
// being held up somewhere between the PC and here. These lines put each gap on the serial
// log with its timestamp and the RSSI at the time, to line up against what the console
// showed. Logging only: nothing here touches the failsafe, which keeps its own timer.
//
//   "Link: console silent ..."     a gap began (logged once, at the threshold)
//   "Link: frames resumed ..."     it ended, with its full length
//   "Link: telemetry send ..."     a send failed or blocked — the rover's own side stalling
//   "Link: loop pass took ..."     the firmware itself stopped servicing the socket
//   "Link: ok ..."                 every LINK_SUMMARY_MS while connected: RSSI and the
//                                  longest gap since the last summary
//
// Before the handshake the console only sends a hello once a second, so the threshold is
// widened until one has arrived — otherwise every connect would log a false gap.
constexpr uint32_t LINK_GAP_LOG_MS = 500;
constexpr uint32_t LINK_GAP_PRE_HELLO_MS = 1500;
constexpr uint32_t LINK_SLOW_SEND_MS = 50;
constexpr uint32_t LINK_SLOW_LOOP_MS = 100;
constexpr uint32_t LINK_SUMMARY_MS = 10000;

uint32_t g_last_rx_ms = 0;
bool g_link_in_gap = false;
uint32_t g_longest_gap_ms = 0;
uint32_t g_last_summary_ms = 0;
uint32_t g_frames_rx = 0;

void linkResetCounters(uint32_t now) {
  g_last_rx_ms = now;
  g_link_in_gap = false;
  g_longest_gap_ms = 0;
  g_last_summary_ms = now;
  g_frames_rx = 0;
}

/// Every inbound frame, whether or not it parses — this measures the link, not the console.
void linkNoteFrame(uint32_t now) {
  const uint32_t gap = now - g_last_rx_ms;
  if (gap > g_longest_gap_ms) g_longest_gap_ms = gap;
  if (g_link_in_gap) {
    log_w("Link: frames resumed after %u ms of silence (RSSI %d dBm)",
          static_cast<unsigned>(gap), static_cast<int>(WiFi.RSSI()));
    g_link_in_gap = false;
  }
  g_last_rx_ms = now;
  ++g_frames_rx;
}

void serviceLinkDiagnostics(uint32_t now) {
  if (!g_failsafe.inputs().connected) return;

  const uint32_t gap = now - g_last_rx_ms;
  const uint32_t threshold = g_hello_received ? LINK_GAP_LOG_MS : LINK_GAP_PRE_HELLO_MS;
  if (!g_link_in_gap && gap >= threshold) {
    g_link_in_gap = true;
    log_w("Link: console silent for %u ms%s (RSSI %d dBm)", static_cast<unsigned>(gap),
          g_hello_received ? "" : ", no hello yet", static_cast<int>(WiFi.RSSI()));
  }

  if (now - g_last_summary_ms >= LINK_SUMMARY_MS) {
    const uint32_t longest = g_longest_gap_ms > gap ? g_longest_gap_ms : gap;
    log_i("Link: ok, %u frames in %u s, longest gap %u ms, RSSI %d dBm",
          static_cast<unsigned>(g_frames_rx),
          static_cast<unsigned>((now - g_last_summary_ms) / 1000),
          static_cast<unsigned>(longest), static_cast<int>(WiFi.RSSI()));
    g_last_summary_ms = now;
    g_longest_gap_ms = 0;
    g_frames_rx = 0;
  }
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
    driveStop();
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
    g_drive_refresh_ms = now;  // the drive watchdog's copy of the same rule
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
      driveStop();
      break;

    case protocol::CommandType::Drive:
      if (g_state == safety::State::Armed) {
        driveSet(cmd.throttle, cmd.steer);
      }
      break;

    case protocol::CommandType::Mast:
      // Same gate as drive: servo commands are ignored in safe mode, and the head holds
      // where it is rather than being moved by the failsafe (docs/protocol.md 6.1). A
      // `mast` frame is itself a fresh command, so after the handshake the first one
      // arms the rover and is then acted on — panning needs no drive press first.
      if (g_state == safety::State::Armed) {
        g_head.point(cmd.has_pan, cmd.pan_deg, cmd.has_tilt, cmd.tilt_deg);
      }
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
      linkResetCounters(now);
      g_failsafe.onConnect();
      applyState(g_failsafe.state(now));
      break;

    case WStype_DISCONNECTED:
      log_w("Console %u disconnected — stopping (last frame %u ms ago, RSSI %d dBm)", client,
            static_cast<unsigned>(now - g_last_rx_ms), static_cast<int>(WiFi.RSSI()));
      driveStop();  // belt and braces: the state change below stops us too
      g_failsafe.onDisconnect();
      applyState(g_failsafe.state(now));
      break;

    case WStype_TEXT:
      linkNoteFrame(now);
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

  // The commanded pose after clamping. Hobby servos report nothing back, so this is
  // where the head was told to be — a stalled or unpowered servo still reads as obeying.
  const aim::Pose head = g_head.pose();
  telemetry.has_head = true;
  telemetry.pan_deg = head.pan_deg;
  telemetry.tilt_deg = head.tilt_deg;

  const size_t length = protocol::serializeTelemetry(telemetry, g_out, sizeof(g_out));
  if (length == 0) {
    log_e("Telemetry frame did not fit — dropped");
    return;
  }
  const uint32_t started = millis();
  const bool sent = g_server.broadcastTXT(g_out, length);
  const uint32_t took = millis() - started;
  if (!sent || took >= LINK_SLOW_SEND_MS) {
    log_w("Link: telemetry send %s, took %u ms", sent ? "slow" : "FAILED",
          static_cast<unsigned>(took));
  }
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
//     double blink, 1 s period, first 15 s   the LAST RESET WAS A BROWNOUT: the supply
//                                            sagged - check the 5 V rail and grounds
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
  if (g_brownout_boot && now < BROWNOUT_SIGNAL_MS) {
    const uint32_t t = now % 1000;
    digitalWrite(PIN_STATUS_LED, (t < 100 || (t >= 200 && t < 300)) ? HIGH : LOW);
    return;
  }
  const BlinkPattern pattern = statusPattern();
  const bool on = (now % pattern.period_ms) < pattern.on_ms;
  digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
}

}  // namespace

void setup() {
  // Drive first, before anything that can take time. Until here the ESC and steering
  // lines sit low on their pull-downs and the ESC sees no signal, so it has not armed;
  // begin() starts neutral / centre, which is what the ESC arms on. The sooner that
  // pulse is steady, the sooner the ESC is ready — and it is never anything but neutral
  // until a console has handshaken and commanded otherwise.
  g_drive.begin();

  // The watchdog starts the moment the drive exists, before Wi-Fi or the server. Core 1
  // (loopTask's own core) at priority 3 against loopTask's 1, so a blocked loop is
  // preempted rather than waited for; Wi-Fi runs on core 0 and is not disturbed.
  g_drive_mutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(driveWatchdogTask, "drive_wd", 3072, nullptr, 3, nullptr, 1);

  // The head centres here, on every reset: its signal lines have sat low on their
  // pull-downs until now, so the servos had no pulses and were limp (docs/servo-wiring.md).
  g_head.begin();

  // 6 dB keeps the divider's 0.55-0.76 V in the ADC's linear region (config.h).
  analogSetPinAttenuation(PIN_BATTERY_SENSE, ADC_6db);

  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  Serial.begin(115200);

  const esp_reset_reason_t reason = esp_reset_reason();
  g_brownout_boot = (reason == ESP_RST_BROWNOUT);
  log_w("Last reset: %s", resetReasonName(reason));

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
  const uint32_t pass_started = millis();
  g_server.loop();

  const uint32_t now = millis();

  // If the firmware itself stalls, it stops reading the socket, and that would look like
  // a silent console from here — this line is what tells the two apart.
  if (now - pass_started >= LINK_SLOW_LOOP_MS) {
    log_w("Link: loop pass took %u ms (socket not serviced)",
          static_cast<unsigned>(now - pass_started));
  }

  serviceWifi(now);
  serviceStatusLed(now);
  serviceLinkDiagnostics(now);

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
