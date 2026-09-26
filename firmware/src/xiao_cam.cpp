// XIAO ESP32S3 Sense camera bring-up — IMPLEMENTATION_PLAN.md step 2.0b.1x.
//
// The same step as cam_check.cpp, on the board that was actually bought. It exists for
// the same reason: prove the silicon and the sensor before anything downstream is allowed
// to be suspected. A bad discrimination result at 2.3 has to mean "colour does not
// separate" — that inference is only safe once the camera itself is known good.
//
//   python -m platformio run -e xiaocam -t upload -t monitor
//
// WHY THIS IS A SEPARATE FILE FROM cam_check.cpp, rather than an #ifdef inside it:
// they are different silicon. This is an ESP32-S3 with 8 MB of OPI PSRAM and native USB;
// cam_check.cpp's board is a plain ESP32-D0WD with 4 MB of quad PSRAM and no USB at all.
// Every constant below differs from that file's, and merging them would produce a sketch
// where the *wrong* pin map is one preprocessor mistake away from silently compiling.
// cam_check.cpp stays for the ESP32-CAM, unmodified.
//
// What this board changes for the better, relative to what step 2.0 assumed:
//
//   * No FTDI adapter and no GPIO0 strap. It enumerates as USB CDC over the same cable
//     that powers it. The "flashing wastes afternoons" warning in [env:camcheck] does not
//     apply here.
//   * 8 MB PSRAM instead of 4, so frame size is not the constraint it was.
//   * The serial link is native USB, not a 115200 UART, so the base64 frame dump below
//     comes out in well under a second rather than the ~2 s cam_check.cpp warns about.
//
// What it does NOT change:
//
//   * Still no Wi-Fi and no secrets.h, for step 1.1's reason — a camera board that cannot
//     join an AP must still be provable, otherwise a wrong password looks identical to a
//     dead sensor. Streaming is the next step, not this one.
//   * Still no detection, no thresholds, no classification.
//
// If the upload fails to find a port: hold the BOOT button, tap RESET, release BOOT. That
// forces the ROM bootloader. It is only needed when a previous sketch has wedged USB.

#include <Arduino.h>
#include <esp_camera.h>
#include <esp_system.h>

namespace {

// XIAO ESP32S3 Sense pin map. The OV2640 sits on the Sense expansion board and is
// hard-wired to these, so they are the board's and not a choice. Listed in full rather
// than pulled from a vendor header for the same reason cam_check.cpp does it: a
// wrong-module diagnosis should be a diff against this block.
//
// NOTE the camera is on the EXPANSION board. If the two halves are not fully seated —
// they need a firm press until the connector clicks — init fails exactly as it does with
// a bad ribbon on an ESP32-CAM.
constexpr int kPwdn = -1;   // not broken out
constexpr int kReset = -1;  // not broken out
constexpr int kXclk = 10;
constexpr int kSiod = 40;
constexpr int kSioc = 39;
constexpr int kY9 = 48;
constexpr int kY8 = 11;
constexpr int kY7 = 12;
constexpr int kY6 = 14;
constexpr int kY5 = 16;
constexpr int kY4 = 18;
constexpr int kY3 = 17;
constexpr int kY2 = 15;
constexpr int kVsync = 38;
constexpr int kHref = 47;
constexpr int kPclk = 13;

// The single user LED, active LOW. It shares GPIO21 with the Sense board's microSD chip
// select — harmless here because nothing below touches the SD card, but worth knowing
// before anyone adds card logging to this sketch and wonders why the LED stutters.
constexpr int kStatusLed = 21;

constexpr uint32_t kBlinkIntervalMs = 500;
constexpr uint32_t kHeartbeatMs = 5000;

// USB CDC only exists once the host opens the port, and anything printed before that is
// lost. Wait briefly for a monitor to attach, but never block forever: the board must
// still run standalone on a USB charger with nothing listening.
constexpr uint32_t kSerialWaitMs = 3000;

uint32_t g_last_blink = 0;
uint32_t g_last_beat = 0;
uint32_t g_beats = 0;
bool g_led = false;
bool g_camera_ok = false;

/// Same reset-cause decode as bringup.cpp and cam_check.cpp. Brownout matters less on
/// this board than on an ESP32-CAM — USB supplies a proper 5 V and the regulator is
/// on-module — but it is still the first thing to rule out if the sensor misbehaves on a
/// long cable or a hub.
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
    case ESP_RST_BROWNOUT:  return "BROWNOUT - the supply sagged, not the code";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "unknown";
  }
}

/// Sensor part number -> name. The Sense ships an OV2640; anything else means the
/// expansion board is not the one this plan was written against, and the optics
/// assumptions behind 2.3's range figures need rechecking before a corpus is shot.
const char *sensorName(uint16_t pid) {
  switch (pid) {
    case 0x26: return "OV2640";
    case 0x36: return "OV3660";
    case 0x56: return "OV5640";
    case 0x77: return "OV7725";
    case 0x76: return "OV7670";
    default:   return "UNKNOWN - not the module this step assumes";
  }
}

const char *chipModelName(esp_chip_model_t model) {
  switch (model) {
    case CHIP_ESP32:   return "ESP32 - WRONG BOARD for this env";
    case CHIP_ESP32S2: return "ESP32-S2 - WRONG BOARD for this env";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C3: return "ESP32-C3 - WRONG BOARD for this env";
    default:           return "unrecognised";
  }
}

void printIdentity() {
  esp_chip_info_t info;
  esp_chip_info(&info);

  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  Serial.println();
  Serial.println(F("=== XIAO ESP32S3 Sense camera bring-up - step 2.0b.1x ==="));
  Serial.printf("chip           : %s rev %d, %d core(s)\n",
                chipModelName(info.model), info.revision, info.cores);
  Serial.printf("flash          : %u MB\n",
                (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.printf("MAC (STA)      : %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("reset reason   : %s\n", resetReason());

  // 8 MB of OPI PSRAM is what makes this board the right one for an on-board detector at
  // all. If it reports none, the memory_type in platformio.ini is wrong (it must be
  // qio_opi) — the silicon is almost certainly fine.
  if (psramFound()) {
    Serial.printf("PSRAM          : yes, %u KB free of %u KB\n",
                  (unsigned)(ESP.getFreePsram() / 1024),
                  (unsigned)(ESP.getPsramSize() / 1024));
  } else {
    Serial.println(F("PSRAM          : NONE - 8 MB expected on this board. Check that"));
    Serial.println(F("                 board_build.arduino.memory_type is qio_opi; OPI"));
    Serial.println(F("                 PSRAM is invisible without it."));
  }
  Serial.printf("heap free      : %u KB\n", (unsigned)(ESP.getFreeHeap() / 1024));
}

/// Bring the OV2640 up at the resolution the detector will actually run at.
///
/// QVGA (320x240) matches what the host-side POC in vision/ captures and trains on, and
/// matching them is the point: shooting a corpus at a resolution the detector never sees
/// measures the wrong optics. JPEG is used only so a frame fits down the wire for
/// inspection — an on-board detector works in RGB565 with no decode step.
///
/// AWB and AEC are left on AUTO here deliberately. Locking them is the next step's job,
/// and it needs a scene and a reference card to lock *against*; locking them now, to
/// whatever happens to be on the bench, would bake in a wrong exposure and make this
/// step's pass/fail harder to read rather than easier.
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
    Serial.println(F("                 0x105 ESP_ERR_NOT_FOUND on this board is almost"));
    Serial.println(F("                 always the expansion connector, not the sensor:"));
    Serial.println(F("                 separate the two halves and press them back"));
    Serial.println(F("                 together firmly until they click, then retry."));
    Serial.println(F("                 If it persists, check the camera ribbon's own"));
    Serial.println(F("                 latch on the Sense board."));
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s == nullptr) {
    Serial.println(F("camera         : initialised but no sensor handle - abnormal"));
    return false;
  }
  Serial.printf("camera         : OK - sensor PID 0x%02X (%s)\n",
                s->id.PID, sensorName(s->id.PID));
  Serial.println(F("                 QVGA 320x240, JPEG q12, auto AWB/AEC for now."));
  Serial.println(F("                 Locking those is the next step's job, not this one's."));
  return true;
}

/// Grab one frame and report only its shape. This is the pass/fail evidence: a plausible,
/// varying JPEG length means the sensor is clocking real pixels out. A length that never
/// changes between captures means it is not.
void captureOnce() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) {
    Serial.println(F("capture        : FAILED - no frame buffer returned"));
    return;
  }
  Serial.printf("capture        : %ux%u, %u bytes JPEG\n",
                (unsigned)fb->width, (unsigned)fb->height, (unsigned)fb->len);
  esp_camera_fb_return(fb);
}

/// Dump one frame as base64 between markers, for tools/cam_grab.py to decode into a real
/// image file. This exists so bring-up can prove *the picture is a picture* — in focus,
/// exposed, right way up — with no Wi-Fi, no credentials and no arena.
void dumpFrame() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) {
    Serial.println(F("dump           : FAILED - no frame buffer returned"));
    return;
  }

  static const char *kB64 =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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

/// Dump a short burst. Bring-up wants more than one frame before it believes the sensor:
/// a single good JPEG can come out of a sensor that then wedges, and varying lengths
/// across a burst are the cheapest evidence that it is genuinely free-running.
void burst(int n) {
  for (int i = 0; i < n; ++i) {
    captureOnce();
    delay(100);
  }
}

void printHelp() {
  Serial.println();
  Serial.println(F("commands: i = identity        c = capture (size only)"));
  Serial.println(F("          b = burst of 10     d = dump frame as base64"));
  Serial.println(F("          h = this help"));
}

}  // namespace

void setup() {
  Serial.begin(115200);

  // Native USB: Serial is only true once a host opens the port. Bounded, so the board
  // still runs with nothing attached.
  const uint32_t start = millis();
  while (!Serial && (millis() - start) < kSerialWaitMs) {
    delay(10);
  }
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
    Serial.printf("[%lu] alive, beat %lu, camera %s, heap %u KB, psram %u KB\n",
                  (unsigned long)now, (unsigned long)++g_beats,
                  g_camera_ok ? "ok" : "DOWN",
                  (unsigned)(ESP.getFreeHeap() / 1024),
                  (unsigned)(ESP.getFreePsram() / 1024));
  }

  while (Serial.available() > 0) {
    const int ch = Serial.read();
    switch (ch) {
      case 'i': printIdentity(); break;
      case 'c':
        if (g_camera_ok) captureOnce(); else Serial.println(F("camera is down"));
        break;
      case 'b':
        if (g_camera_ok) burst(10); else Serial.println(F("camera is down"));
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
