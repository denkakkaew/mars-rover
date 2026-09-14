// Wi-Fi survey tool — IMPLEMENTATION_PLAN.md steps 1.2 and 0.5.
//
//   python -m platformio run -e wifiscan -t upload -t monitor
//
// Two jobs, neither of which needs credentials:
//
//   1. **Say what is actually on the air**, so `secrets.h` gets the exact SSID rather
//      than an approximation of it. A `NO_AP_FOUND` at step 1.2 is almost always a typo
//      or a 5 GHz-only network, and both are visible here.
//
//   2. **Be the instrument for the RSSI survey.** It rescans continuously, so the board
//      can be carried to the near end, the mid point, the far end and each corner with
//      the readings updating in place. Beacon RSSI from a scan is a valid survey
//      measurement and — unlike the rover firmware's telemetry RSSI — it needs no
//      association, so the survey can be run before the arena network exists.
//
// The ESP32 is 2.4 GHz only. A network absent here but visible on a phone is very
// likely 5 GHz, which is the single most common cause of an ESP32 that "cannot see the
// house Wi-Fi" — hence the explicit note in the output rather than a silent empty list.

#include <Arduino.h>
#include <WiFi.h>

namespace {

constexpr uint32_t kRescanIntervalMs = 5000;

/// 2.4 GHz has only three non-overlapping channels. Step 0.5 has to pick one, and the
/// useful question is not "which channel is free" but "which of 1, 6, 11 is least busy".
void reportChannelLoad(int count) {
  int occupancy[14] = {0};
  for (int i = 0; i < count; ++i) {
    const int channel = WiFi.channel(i);
    if (channel >= 1 && channel <= 13) occupancy[channel]++;
  }

  Serial.println();
  Serial.println("  non-overlapping channel load (step 0.5 picks one of these):");
  for (const int channel : {1, 6, 11}) {
    // A network on an adjacent channel still interferes, so count the neighbourhood.
    int neighbourhood = 0;
    for (int c = max(1, channel - 2); c <= min(13, channel + 2); ++c) {
      neighbourhood += occupancy[c];
    }
    Serial.printf("    ch %-2d  %d on channel, %d within +/-2  %s\n", channel,
                  occupancy[channel], neighbourhood,
                  neighbourhood == 0 ? "<-- clear" : "");
  }
}

const char *encryptionName(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN:            return "open";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ent";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
    default:                        return "?";
  }
}

/// Rough guidance for the survey. The number that matters at step 1.2 is the far corner
/// with the lid closed, and it is easier to judge against words than against dBm.
const char *quality(int32_t rssi) {
  if (rssi >= -55) return "strong";
  if (rssi >= -67) return "good";
  if (rssi >= -75) return "workable";
  if (rssi >= -82) return "MARGINAL";
  return "UNUSABLE";
}

void scanOnce() {
  Serial.println();
  Serial.println("scanning 2.4 GHz...");
  const int count = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);

  if (count <= 0) {
    Serial.println("  no networks found at all.");
    Serial.println("  If a phone sees Wi-Fi here, the network is very likely 5 GHz —");
    Serial.println("  the ESP32 is 2.4 GHz only and cannot join it. Step 0.5 decides the");
    Serial.println("  arena topology; a 2.4 GHz SSID has to exist for the rover to use.");
    return;
  }

  Serial.printf("  %d network(s), strongest first:\n\n", count);
  Serial.println("    rssi  ch  enc       quality    ssid");
  Serial.println("    ----  --  --------  ---------  --------------------------------");
  for (int i = 0; i < count; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) ssid = "<hidden>";
    Serial.printf("    %4d  %2d  %-8s  %-9s  %s\n", WiFi.RSSI(i), WiFi.channel(i),
                  encryptionName(WiFi.encryptionType(i)), quality(WiFi.RSSI(i)),
                  ssid.c_str());
  }

  reportChannelLoad(count);
  WiFi.scanDelete();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=== Mars rover — Wi-Fi survey (steps 1.2, 0.5) ===");
  Serial.println("Rescans every 5 s. Carry the board to each arena position and read off");
  Serial.println("the target SSID's rssi. The far corner with the lid CLOSED is the");
  Serial.println("number risk R6 is judged on.");
  Serial.printf("MAC %s\n", WiFi.macAddress().c_str());

  // Station mode, unassociated: scanning needs no credentials.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
}

void loop() {
  scanOnce();
  delay(kRescanIntervalMs);
}
