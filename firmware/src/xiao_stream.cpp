// XIAO ESP32S3 Sense Wi-Fi camera — IMPLEMENTATION_PLAN.md step 2.0b.2.
//
// Bring-up (2.0b.1x, xiao_cam.cpp) proved the sensor over USB. This adds the network, so
// frames can be pulled in bulk by the host without a cable, and so the board can be aimed
// and focused while watching a live view. It is the step the corpus gets shot with.
//
//   python -m platformio run -e xiaostream -t upload -t monitor
//
// Endpoints, once it prints its IP:
//
//   GET :80/         a small status page with a live view and links
//   GET :81/stream   MJPEG — this is the "IP camera" view; open it in a browser
//   GET :80/snap     one JPEG, which is what the host capture tool pulls
//   GET :80/status   JSON: IP, RSSI, frame size, and whether the sensor is locked
//   GET :80/lock     LOCK auto-exposure, auto-gain and auto-white-balance where they are
//   GET :80/unlock   hand them back to the sensor's own control loops
//   GET :80/control?var=<name>&val=<n>    one sensor register, by name
//
// WHY /lock EXISTS, AND WHY IT IS THE POINT OF THIS STEP:
// step 2.0's decision record names lighting as the thing that replaced risk R2, and the
// failure it describes is a detector that learned the room rather than the rock. An
// OV2640 left on auto re-white-balances every time the scene changes, so a green rock
// filling the frame drags the whole image magenta and the next shot of the same rock is a
// different colour. Locking is therefore not a refinement to add later — a corpus shot
// unlocked is a corpus that teaches the wrong thing. Aim the camera at a neutral card,
// call /lock, then shoot.
//
// WI-FI JOIN IS BOUNDED, DELIBERATELY. main.cpp carries known defect F7: an unbounded
// `while (WiFi.status() != WL_CONNECTED)` in setup(), which hangs the board forever on a
// wrong password and is indistinguishable from dead silicon. That bug is not repeated
// here. This sketch gives the join a deadline, reports the actual failure, and then keeps
// running with the camera alive over USB — so a credentials problem looks like a
// credentials problem, and the board stays diagnosable. When 1.2 fixes F7 in main.cpp,
// joinWiFi() below is the shape to copy.
//
// ANTENNA: the XIAO ESP32S3 ships with a separate U.FL antenna that has to be clipped on.
// Without it the radio still works, but only across a desk. Weak RSSI or a join that
// times out next to the router almost always means the antenna is still in the bag.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <esp_system.h>

#include "secrets.h"

namespace {

// Identical pin map to xiao_cam.cpp. Repeated rather than shared through a header
// because these two sketches are bench instruments that must stay independently
// readable — and because a pin map is the first thing to diff when a board misbehaves.
constexpr int kPwdn = -1;
constexpr int kReset = -1;
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

constexpr int kStatusLed = 21;  // active LOW

// TWO servers, on two ports, and this is not a stylistic choice.
//
// An MJPEG handler never returns — it writes frames until the client goes away. On a
// single-threaded server that starves every other endpoint: with a stream open, /lock and
// /status simply time out. That was measured on the first version of this sketch, not
// theorised.
//
// esp_http_server runs each instance in its own FreeRTOS task, so control stays live
// while the stream runs. Port 81 for the stream follows Espressif's own camera example,
// which is what anyone debugging this will expect to find. The rover controller also
// serves on 81, but it is a different board at a different address, so nothing collides.
constexpr uint16_t kControlPort = 80;
constexpr uint16_t kStreamPort = 81;

constexpr uint32_t kJoinTimeoutMs = 20000;
constexpr uint32_t kRejoinIntervalMs = 30000;
constexpr uint32_t kHeartbeatMs = 10000;
constexpr uint32_t kSerialWaitMs = 3000;

const char *kStreamContentType = "multipart/x-mixed-replace;boundary=xiaoframe";
const char *kStreamBoundary = "\r\n--xiaoframe\r\n";
const char *kStreamPart = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t g_control_httpd = nullptr;
httpd_handle_t g_stream_httpd = nullptr;

bool g_camera_ok = false;
bool g_locked = false;
bool g_servers_up = false;
uint32_t g_last_beat = 0;
uint32_t g_last_rejoin = 0;
volatile uint32_t g_frames_served = 0;

const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external pin";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic / exception";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_BROWNOUT:  return "BROWNOUT - the supply sagged, not the code";
    default:                return "other";
  }
}

const char *sensorName(uint16_t pid) {
  switch (pid) {
    case 0x26: return "OV2640";
    case 0x36: return "OV3660";
    case 0x56: return "OV5640";
    default:   return "UNKNOWN - not the module this step assumes";
  }
}

bool startCamera() {
  camera_config_t cfg = {};
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer = LEDC_TIMER_0;
  cfg.pin_d0 = kY2;  cfg.pin_d1 = kY3;  cfg.pin_d2 = kY4;  cfg.pin_d3 = kY5;
  cfg.pin_d4 = kY6;  cfg.pin_d5 = kY7;  cfg.pin_d6 = kY8;  cfg.pin_d7 = kY9;
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

  // VGA here, not the QVGA of bring-up. Annotation wants detail the detector does not:
  // a bounding box drawn on a 320x240 thumbnail is imprecise, and a training pipeline can
  // always downscale, while it can never invent pixels back. /control?var=framesize lets
  // this drop to QVGA when the question is what the detector will actually see.
  cfg.frame_size = FRAMESIZE_VGA;
  cfg.jpeg_quality = 10;
  cfg.fb_count = psramFound() ? 2 : 1;
  cfg.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  cfg.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("camera         : INIT FAILED (0x%04x) - reseat the expansion board\n",
                  err);
    return false;
  }
  sensor_t *s = esp_camera_sensor_get();
  if (s == nullptr) {
    Serial.println(F("camera         : no sensor handle - abnormal"));
    return false;
  }
  Serial.printf("camera         : OK - sensor PID 0x%02X (%s), VGA 640x480 JPEG q10\n",
                s->id.PID, sensorName(s->id.PID));
  return true;
}

/// Join with a deadline. Returns false rather than blocking forever — see the F7 note in
/// the file header. The failure reason is printed because "could not connect" is not
/// actionable, while "no AP with that SSID" and "wrong password" are different problems.
bool joinWiFi() {
  Serial.printf("wifi           : joining \"%s\" ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // sleep adds tens of ms of jitter to every frame request
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < kJoinTimeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("wifi           : OK - IP %s, RSSI %d dBm, channel %d\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
    Serial.printf("                 open  http://%s/  in a browser\n",
                  WiFi.localIP().toString().c_str());
    return true;
  }

  switch (WiFi.status()) {
    case WL_NO_SSID_AVAIL:
      Serial.println(F("wifi           : FAILED - no AP with that SSID is on the air."));
      Serial.println(F("                 Check spelling in secrets.h, and that the AP is"));
      Serial.println(F("                 2.4 GHz: this radio cannot see a 5 GHz-only SSID."));
      break;
    case WL_CONNECT_FAILED:
      Serial.println(F("wifi           : FAILED - AP found, association refused. This is"));
      Serial.println(F("                 usually the password."));
      break;
    default:
      Serial.printf("wifi           : FAILED - status %d after %lu ms. If the AP is close,\n",
                    (int)WiFi.status(), (unsigned long)kJoinTimeoutMs);
      Serial.println(F("                 check the U.FL antenna is clipped on."));
      break;
  }
  Serial.println(F("                 Camera stays alive over USB; retrying in background."));
  return false;
}

// ----------------------------------------------------------------------- HTTP handlers

esp_err_t sendText(httpd_req_t *req, const char *status, const char *body) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t snapHandler(httpd_req_t *req) {
  if (!g_camera_ok) return sendText(req, "503 Service Unavailable", "camera down");

  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) return sendText(req, "500 Internal Server Error", "no frame");

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  // No-store matters: a browser or a capture loop that caches /snap will happily hand
  // back the same photograph for an entire session, which looks exactly like a frozen
  // sensor.
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  ++g_frames_served;
  return res;
}

esp_err_t streamHandler(httpd_req_t *req) {
  if (!g_camera_ok) return sendText(req, "503 Service Unavailable", "camera down");

  esp_err_t res = httpd_resp_set_type(req, kStreamContentType);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  char part[72];
  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == nullptr) {
      res = ESP_FAIL;
      break;
    }

    const size_t hlen = snprintf(part, sizeof(part), kStreamPart, (unsigned)fb->len);

    // Every chunk is checked: a client closing the tab shows up here as a send failure,
    // and continuing would leak a frame buffer on each pass.
    res = httpd_resp_send_chunk(req, kStreamBoundary, strlen(kStreamBoundary));
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, part, hlen);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);

    esp_camera_fb_return(fb);
    if (res != ESP_OK) break;
    ++g_frames_served;
  }
  return res;
}

esp_err_t statusHandler(httpd_req_t *req) {
  sensor_t *s = esp_camera_sensor_get();
  String json = "{";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"rssi\":" + String(WiFi.RSSI());
  json += ",\"camera\":" + String(g_camera_ok ? "true" : "false");
  json += ",\"locked\":" + String(g_locked ? "true" : "false");
  json += ",\"frames_served\":" + String((unsigned long)g_frames_served);
  json += ",\"heap_kb\":" + String(ESP.getFreeHeap() / 1024);
  json += ",\"psram_kb\":" + String(ESP.getFreePsram() / 1024);
  if (s != nullptr) {
    json += ",\"framesize\":" + String((int)s->status.framesize);
    json += ",\"quality\":" + String(s->status.quality);
    json += ",\"awb\":" + String(s->status.awb);
    json += ",\"aec\":" + String(s->status.aec);
    json += ",\"agc\":" + String(s->status.agc);
  }
  json += "}";

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
}

/// Freeze the sensor's own control loops where they currently sit. Call it with a neutral
/// grey or white card filling the frame, under the light the corpus will be shot in.
esp_err_t lockHandler(httpd_req_t *req) {
  sensor_t *s = esp_camera_sensor_get();
  if (s == nullptr) return sendText(req, "503 Service Unavailable", "no sensor");

  // Order matters: gain and exposure first, white balance last. Turning AWB off while AEC
  // is still hunting locks a white balance that was measured against a brightness the
  // sensor is about to leave.
  s->set_gain_ctrl(s, 0);      // AGC off
  s->set_exposure_ctrl(s, 0);  // AEC off
  s->set_whitebal(s, 0);       // AWB off
  s->set_awb_gain(s, 0);       // and its gain stage
  g_locked = true;
  Serial.println(F("sensor         : AGC/AEC/AWB locked at current values"));
  return sendText(req, "200 OK", "locked");
}

esp_err_t unlockHandler(httpd_req_t *req) {
  sensor_t *s = esp_camera_sensor_get();
  if (s == nullptr) return sendText(req, "503 Service Unavailable", "no sensor");

  s->set_gain_ctrl(s, 1);
  s->set_exposure_ctrl(s, 1);
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 1);
  g_locked = false;
  Serial.println(F("sensor         : AGC/AEC/AWB returned to auto"));
  return sendText(req, "200 OK", "unlocked");
}

esp_err_t controlHandler(httpd_req_t *req) {
  sensor_t *s = esp_camera_sensor_get();
  if (s == nullptr) return sendText(req, "503 Service Unavailable", "no sensor");

  char query[128];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
    return sendText(req, "400 Bad Request", "need ?var=<name>&val=<n>");

  char var[32];
  char val[32];
  if (httpd_query_key_value(query, "var", var, sizeof(var)) != ESP_OK ||
      httpd_query_key_value(query, "val", val, sizeof(val)) != ESP_OK)
    return sendText(req, "400 Bad Request", "need ?var=<name>&val=<n>");

  const int v = atoi(val);
  int res = -1;
  if (!strcmp(var, "framesize"))       res = s->set_framesize(s, (framesize_t)v);
  else if (!strcmp(var, "quality"))    res = s->set_quality(s, v);
  else if (!strcmp(var, "brightness")) res = s->set_brightness(s, v);
  else if (!strcmp(var, "contrast"))   res = s->set_contrast(s, v);
  else if (!strcmp(var, "saturation")) res = s->set_saturation(s, v);
  else if (!strcmp(var, "awb"))        res = s->set_whitebal(s, v);
  else if (!strcmp(var, "awb_gain"))   res = s->set_awb_gain(s, v);
  else if (!strcmp(var, "wb_mode"))    res = s->set_wb_mode(s, v);
  else if (!strcmp(var, "aec"))        res = s->set_exposure_ctrl(s, v);
  else if (!strcmp(var, "aec_value"))  res = s->set_aec_value(s, v);
  else if (!strcmp(var, "agc"))        res = s->set_gain_ctrl(s, v);
  else if (!strcmp(var, "agc_gain"))   res = s->set_agc_gain(s, v);
  else if (!strcmp(var, "hmirror"))    res = s->set_hmirror(s, v);
  else if (!strcmp(var, "vflip"))      res = s->set_vflip(s, v);
  else return sendText(req, "400 Bad Request", "unknown var");

  // A manual aec_value or agc_gain only takes effect with the matching auto loop off, and
  // silently does nothing otherwise. Say so rather than letting it look broken.
  if ((!strcmp(var, "aec_value") || !strcmp(var, "agc_gain")) && !g_locked)
    Serial.println(F("sensor         : NOTE - manual exposure/gain needs /lock first"));

  return res == 0 ? sendText(req, "200 OK", "ok")
                  : sendText(req, "500 Internal Server Error", "failed");
}

esp_err_t rootHandler(httpd_req_t *req) {
  const String ip = WiFi.localIP().toString();
  String page = F("<!doctype html><meta name=viewport content='width=device-width'>"
                  "<style>body{font:15px system-ui;margin:2rem;max-width:44rem}"
                  "img{width:100%;border:1px solid #888}"
                  "a{display:inline-block;margin-right:1rem}</style>"
                  "<h2>XIAO ESP32S3 Sense &mdash; step 2.0b.2</h2>");
  page += "<img src='http://" + ip + ":" + String(kStreamPort) + "/stream'>";
  page += "<p><a href='/snap'>/snap</a><a href='/status'>/status</a>"
          "<a href='/lock'>/lock</a><a href='/unlock'>/unlock</a></p>";
  page += F("<p>Aim a neutral grey or white card at the lens under the light you will "
            "shoot in, then hit <b>/lock</b> before capturing a corpus. Left on auto, the "
            "sensor re-balances every time the scene changes, and a green rock filling "
            "the frame drags the whole image magenta.</p>");
  page += "<p>IP " + ip + ", RSSI " + String(WiFi.RSSI()) + " dBm, sensor " +
          (g_locked ? "<b>locked</b>" : "auto") + ".</p>";

  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
}

void startServers() {
  if (g_servers_up) return;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = kControlPort;
  config.ctrl_port = 32768;
  config.max_uri_handlers = 8;

  httpd_uri_t root_uri    = {"/",        HTTP_GET, rootHandler,    nullptr};
  httpd_uri_t snap_uri    = {"/snap",    HTTP_GET, snapHandler,    nullptr};
  httpd_uri_t status_uri  = {"/status",  HTTP_GET, statusHandler,  nullptr};
  httpd_uri_t lock_uri    = {"/lock",    HTTP_GET, lockHandler,    nullptr};
  httpd_uri_t unlock_uri  = {"/unlock",  HTTP_GET, unlockHandler,  nullptr};
  httpd_uri_t control_uri = {"/control", HTTP_GET, controlHandler, nullptr};

  if (httpd_start(&g_control_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(g_control_httpd, &root_uri);
    httpd_register_uri_handler(g_control_httpd, &snap_uri);
    httpd_register_uri_handler(g_control_httpd, &status_uri);
    httpd_register_uri_handler(g_control_httpd, &lock_uri);
    httpd_register_uri_handler(g_control_httpd, &unlock_uri);
    httpd_register_uri_handler(g_control_httpd, &control_uri);
    Serial.printf("http           : control on port %u\n", kControlPort);
  } else {
    Serial.println(F("http           : control server FAILED to start"));
  }

  // Second instance, second task, second port — see the note at kControlPort.
  config.server_port = kStreamPort;
  config.ctrl_port = 32769;
  httpd_uri_t stream_uri = {"/stream", HTTP_GET, streamHandler, nullptr};
  if (httpd_start(&g_stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(g_stream_httpd, &stream_uri);
    Serial.printf("http           : stream  on port %u  (http://%s:%u/stream)\n",
                  kStreamPort, WiFi.localIP().toString().c_str(), kStreamPort);
  } else {
    Serial.println(F("http           : stream server FAILED to start"));
  }

  g_servers_up = true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && (millis() - start) < kSerialWaitMs) delay(10);
  delay(200);

  pinMode(kStatusLed, OUTPUT);
  digitalWrite(kStatusLed, HIGH);

  Serial.println();
  Serial.println(F("=== XIAO ESP32S3 Sense Wi-Fi camera - step 2.0b.2 ==="));
  Serial.printf("reset reason   : %s\n", resetReason());
  Serial.printf("PSRAM          : %s\n",
                psramFound() ? "yes" : "NONE - check memory_type is qio_opi");

  g_camera_ok = startCamera();
  if (joinWiFi()) startServers();
}

void loop() {
  const uint32_t now = millis();

  // The servers run in their own tasks, so this loop only supervises the link and
  // reports. It must not busy-wait: delay() below is what lets the idle task feed the
  // watchdog.
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(kStatusLed, LOW);  // solid on = joined and serving
    if (!g_servers_up) startServers();
  } else {
    digitalWrite(kStatusLed, ((now / 500) % 2) ? LOW : HIGH);  // slow blink = no link
    if (now - g_last_rejoin >= kRejoinIntervalMs) {
      g_last_rejoin = now;
      if (joinWiFi()) startServers();
    }
  }

  if (now - g_last_beat >= kHeartbeatMs) {
    g_last_beat = now;
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[%lu] %s  RSSI %d dBm  frames %lu  heap %u KB  sensor %s\n",
                    (unsigned long)now, WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                    (unsigned long)g_frames_served, (unsigned)(ESP.getFreeHeap() / 1024),
                    g_locked ? "LOCKED" : "auto");
    } else {
      Serial.printf("[%lu] wifi down, camera %s, retrying\n",
                    (unsigned long)now, g_camera_ok ? "ok" : "DOWN");
    }
  }

  delay(20);
}
