// Camera board bring-up sketch — IMPLEMENTATION_PLAN.md step 2.0b.1.
//
// The ESP32-CAM equivalent of bringup.cpp, and it exists for the same reason: prove the
// silicon and the sensor before anything downstream is allowed to be suspected. Step
// 2.0b's whole value is that a bad discrimination result means "colour does not separate"
// — that inference is only safe once the camera itself is known good.
//
//   python -m platformio run -e camcheck -t upload -t monitor
//
// Deliberately NOT in this file:
//
//   * No Wi-Fi, and so no secrets.h — same argument as step 1.1. A camera board that
//     cannot join an AP must still be provable, otherwise a wrong password looks
//     identical to a dead sensor. Streaming arrives at 2.0b.2, where bulk capture needs
//     it; here a frame comes out over the serial line instead, base64-encoded, which
//     proves the entire imaging path with nothing but the programmer already attached.
//   * No detection, no thresholds, no classification. That is 2.0b.3 and it happens on
//     the host, against a corpus, where iterating costs nothing.
//
// This is a different board from the rover's controller — an ESP32-CAM, not the
// ESP32-D0WD-V3 devkit — which is why it is its own env with its own `board =` rather
// than a flag. The two can never link into one binary by accident.
//
// The module is silkscreened "ESP32-S", which is a MODULE name and not a chip variant:
// the die is a plain ESP32-D0WD, the same family as the rover controller. It is not an
// ESP32-S2 or -S3. The giveaway is the absence of a USB socket — every S3 camera board
// has native USB, which is why this one needs an external adapter to flash.

#include <Arduino.h>
#include <esp_camera.h>
#include <esp_system.h>

namespace {

// AI Thinker-compatible ESP32-CAM pin map, which is what the common clones use too.
// These are the board's, not a choice — the OV2640 is hard-wired to them. Listed in full
// rather than pulled from a board header so that a wrong-module diagnosis is a diff
// against this block.
constexpr int kPwdn = 32;
constexpr int kReset = -1;  // not broken out on this module
constexpr int kXclk = 0;
constexpr int kSiod = 26;
constexpr int kSioc = 27;
constexpr int kY9 = 35;
constexpr int kY8 = 34;
constexpr int kY7 = 39;
constexpr int kY6 = 36;
constexpr int kY5 = 21;
constexpr int kY4 = 19;
constexpr int kY3 = 18;
constexpr int kY2 = 5;
constexpr int kVsync = 25;
constexpr int kHref = 23;
constexpr int kPclk = 22;

// GPIO33 is the small red status LED and is active LOW. GPIO4 is the bright white flash
// LED — left alone here on purpose: it draws ~320 mA (power-budget §6.1) and firing it on
// a supply that is already marginal turns a bring-up into a brownout hunt.
constexpr int kStatusLed = 33;

constexpr uint32_t kBlinkIntervalMs = 500;
constexpr uint32_t kHeartbeatMs = 5000;

uint32_t g_last_blink = 0;
uint32_t g_last_beat = 0;
uint32_t g_beats = 0;
bool g_led = false;
bool g_camera_ok = false;

/// Same reset-cause decode as bringup.cpp. It matters more on this board, not less:
/// ESP_RST_BROWNOUT is the single most likely failure here, because the OV2640's inrush
/// is what a marginal 5 V feed gives way under. A board that reports a brownout has a
/// supply problem, not a firmware problem.
const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external pin";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic / exception";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT:  return "BROWNOUT — the supply sagged, not the code";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "unknown";
  }
}

/// Sensor part number → name. Only the OV2640 is expected; anything else means the module
/// is not the one this plan was written against and 2.0b's optics assumptions need
/// rechecking before the corpus is shot.
const char *sensorName(uint16_t pid) {
  switch (pid) {
    case 0x26: return "OV2640";
    case 0x36: return "OV3660";
    case 0x56: return "OV5640";
    case 0x77: return "OV7725";
    case 0x76: return "OV7670";
    default:   return "UNKNOWN — not the module 2.0b assumes";
  }
}

void printIdentity() {
  esp_chip_info_t info;
  esp_chip_info(&info);

  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  Serial.println();
  Serial.println(F("=== ESP32-CAM bring-up — step 2.0b.1 ==="));
  Serial.printf("chip           : %s rev %d, %d core(s)\n",
                info.model == CHIP_ESP32 ? "ESP32" : "not-ESP32", info.revision, info.cores);
  Serial.printf("flash          : %u MB\n", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.printf("MAC (STA)      : %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("reset reason   : %s\n", resetReason());

  // PSRAM is not a nicety on this board. Without it the frame buffer is capped small
  // enough that QVGA double-buffering fails, which shows up later as a camera that
  // initialises and then starves. Report it up front so that failure is never a mystery.
  if (psramFound()) {
    Serial.printf("PSRAM          : yes, %u KB free of %u KB\n",
                  (unsigned)(ESP.getFreePsram() / 1024), (unsigned)(ESP.getPsramSize() / 1024));
  } else {
    Serial.println(F("PSRAM          : NONE — 4 MB expected. Some ESP32-CAM clones ship"));
    Serial.println(F("                 without it; check -DBOARD_HAS_PSRAM is set, and if"));
    Serial.println(F("                 it is, this module genuinely has none and QVGA"));
    Serial.println(F("                 double buffering will not work."));
  }
  Serial.printf("heap free      : %u KB\n", (unsigned)(ESP.getFreeHeap() / 1024));
}

/// Bring the OV2640 up at the resolution 2.0b actually works in.
///
/// QVGA (320x240) is chosen here rather than something larger because it is the size the
/// detector will run at: 2.0b.3 downscales before thresholding anyway, and shooting the
/// corpus at a resolution the detector never sees would measure the wrong optics. JPEG is
/// used only so a frame fits down the serial line — 2.0b.3 works in RGB565, with no
/// decode step in the pipeline at all.
bool startCamera() {
  camera_config_t cfg = {};
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer = LEDC_TIMER_0;
  cfg.pin_d0 = kY2;
  cfg.pin_d1 = kY3;
  cfg.pin_d2 = kY4;
  cfg.pin_d3 = kY5;
  cfg.pin_d4 = kY6;
  cfg.pin_d5 = kY7;
  cfg.pin_d6 = kY8;
  cfg.pin_d7 = kY9;
  cfg.pin_xclk = kXclk;
  cfg.pin_pclk = kPclk;
  cfg.pin_vsync = kVsync;
  cfg.pin_href = kHref;
  cfg.pin_sccb_sda = kSiod;
  cfg.pin_sccb_scl = kSioc;
  cfg.pin_pwdn = kPwdn;
  cfg.pin_reset = kReset;
  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.frame_size = FRAMESIZE_QVGA;
  cfg.jpeg_quality = 12;
  cfg.fb_count = psramFound() ? 2 : 1;
  cfg.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  cfg.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("camera         : INIT FAILED (0x%04x)\n", err);
    Serial.println(F("                 0x105 ESP_ERR_NOT_FOUND is usually the ribbon:"));
    Serial.println(F("                 unseat it, check it is square, reseat and retry."));
    Serial.println(F("                 A brownout at this moment looks the same — check"));
    Serial.println(F("                 the reset reason on the next boot before blaming"));
    Serial.println(F("                 the connector."));
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s == nullptr) {
    Serial.println(F("camera         : initialised but no sensor handle — abnormal"));
    return false;
  }
  Serial.printf("camera         : OK — sensor PID 0x%02X (%s)\n", s->id.PID, sensorName(s->id.PID));
  Serial.println(F("                 QVGA 320x240, JPEG q12, auto AWB/AEC for now."));
  Serial.println(F("                 Locking those is 2.0b.2's job, not this step's."));
  return true;
}

/// Grab one frame and report only its shape. This is the pass/fail evidence for 2.0b.1:
/// a plausible, varying JPEG length means the sensor is clocking real pixels out.
void captureOnce() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) {
    Serial.println(F("capture        : FAILED — no frame buffer returned"));
    return;
  }
  Serial.printf("capture        : %ux%u, %u bytes JPEG\n",
                (unsigned)fb->width, (unsigned)fb->height, (unsigned)fb->len);
  esp_camera_fb_return(fb);
}

/// Dump one frame as base64 between markers, for `tools/cam_grab.py` to decode into a
/// real image file. Slow — roughly two seconds at 115200 — and that is fine: this exists
/// so 2.0b.1 can prove *the picture is a picture* (in focus, exposed, right way up)
/// without Wi-Fi, credentials, or an arena. Bulk capture is 2.0b.2 over the network.
void dumpFrame() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) {
    Serial.println(F("dump           : FAILED — no frame buffer returned"));
    return;
  }

  static const char *kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  Serial.printf("---BEGIN JPEG %u---\n", (unsigned)fb->len);
  const uint8_t *p = fb->buf;
  size_t n = fb->len;
  size_t col = 0;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)p[i] << 16;
    if (i + 1 < n) v |= (uint32_t)p[i + 1] << 8;
    if (i + 2 < n) v |= p[i + 2];

    char out[4];
    out[0] = kB64[(v >> 18) & 0x3F];
    out[1] = kB64[(v >> 12) & 0x3F];
    out[2] = (i + 1 < n) ? kB64[(v >> 6) & 0x3F] : '=';
    out[3] = (i + 2 < n) ? kB64[v & 0x3F] : '=';
    Serial.write(out, 4);

    col += 4;
    if (col >= 76) {
      Serial.println();
      col = 0;
    }
  }
  if (col != 0) Serial.println();
  Serial.println(F("---END JPEG---"));

  esp_camera_fb_return(fb);
}

void printHelp() {
  Serial.println();
  Serial.println(F("commands: i = identity   c = capture (size only)"));
  Serial.println(F("          d = dump frame as base64   h = this help"));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(kStatusLed, OUTPUT);
  digitalWrite(kStatusLed, HIGH);  // active LOW, so HIGH is off

  printIdentity();
  g_camera_ok = startCamera();
  if (g_camera_ok) captureOnce();
  printHelp();
}

void loop() {
  const uint32_t now = millis();

  if (now - g_last_blink >= kBlinkIntervalMs) {
    g_last_blink = now;
    g_led = !g_led;
    digitalWrite(kStatusLed, g_led ? LOW : HIGH);
  }

  if (now - g_last_beat >= kHeartbeatMs) {
    g_last_beat = now;
    Serial.printf("[%lu] alive, beat %lu, camera %s, heap %u KB\n",
                  (unsigned long)now, (unsigned long)++g_beats,
                  g_camera_ok ? "ok" : "DOWN", (unsigned)(ESP.getFreeHeap() / 1024));
  }

  while (Serial.available() > 0) {
    const int ch = Serial.read();
    switch (ch) {
      case 'i': printIdentity(); break;
      case 'c':
        if (g_camera_ok) captureOnce(); else Serial.println(F("camera is down"));
        break;
      case 'd':
        if (g_camera_ok) dumpFrame(); else Serial.println(F("camera is down"));
        break;
      case 'h': printHelp(); break;
      case '\r':
      case '\n': break;
      default: break;
    }
  }
}
