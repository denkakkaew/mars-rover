// XIAO ESP32S3 Sense live rock detection — IMPLEMENTATION_PLAN.md step 2.0e.
//
// The payoff of 2.0b/2.0c/2.0d: the model trained on the auto-labelled corpus, running on
// the board, naming rocks in a live view. Point a browser at the board and the rock has its
// element written over it.
//
//   python -m platformio run -e xiaodetect -t upload -t monitor
//
// Endpoints, once it prints its IP:
//
//   GET :80/         the live view WITH detections drawn over it — this is the demo
//   GET :81/stream   raw MJPEG, no overlay
//   GET :80/detect   JSON: the most recent detections, in VGA frame coordinates
//   GET :80/snap · /status · /lock · /unlock   as in xiao_stream.cpp
//
// HOW THE OVERLAY IS DRAWN, AND WHY NOT ON THE BOARD. Boxes are not burned into the JPEG.
// Doing that would mean decoding every frame to RGB, drawing, and re-encoding — three
// expensive steps on a board that is already spending most of its time on inference, and
// the stream would slow to the inference rate. Instead the board serves the untouched
// stream and a small JSON of what it last saw, and the page at / draws the boxes on a
// canvas over the <img>. The browser does the compositing for free, and the video stays as
// fast as the network allows even when inference is slow.
//
// INFERENCE RUNS IN ITS OWN TASK, not in the stream handler. An MJPEG handler never
// returns, so anything folded into it runs at the stream's rate and blocks it in turn.
// The detector task grabs its own frames on a timer; a mutex keeps the two off the camera
// at the same moment, because esp_camera_fb_get() is not safe to call from two tasks.
//
// THE MODEL SEES A CENTRE CROP, NOT THE WHOLE FRAME. It was trained at 160x160 with Edge
// Impulse's "fit shortest axis", which centre-crops to a square and scales. A 640x480
// frame therefore reaches the model as its middle 480x480 — the left and right 80 px
// columns are invisible to the detector, though they are still in the video. Coordinates
// are mapped back to full-frame pixels before they leave the board (see kCropX), so the
// page can draw them straight onto the video without knowing any of this.
//
// A MODEL IS A FLASH, NOT A SETTING. Retraining means re-exporting from Edge Impulse,
// replacing firmware/lib/EiRockModel and reflashing. That is the cost of the CNN deviation
// recorded at step 2.0c, and it is why step 2.6's promise of runtime-settable detector
// thresholds does not survive. Only the confidence cut-off below is tunable at runtime,
// and that is a filter on the model's output, not the model.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <img_converters.h>

#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "secrets.h"

namespace {

// ----------------------------------------------------------------------- board constants

// XIAO ESP32S3 Sense camera pins — same as xiao_cam.cpp / xiao_stream.cpp.
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

constexpr uint16_t kControlPort = 80;
constexpr uint16_t kStreamPort = 81;

constexpr uint32_t kJoinTimeoutMs = 20000;
constexpr uint32_t kRejoinIntervalMs = 30000;
constexpr uint32_t kHeartbeatMs = 10000;
constexpr uint32_t kSerialWaitMs = 3000;

// Frames the detector works from. VGA because that is what the corpus was shot at, so the
// rock subtends the same fraction of the frame here as it did in training — and FOMO is
// sensitive to exactly that (see vision/TRAINING.md section 5.2).
constexpr int kFrameW = 640;
constexpr int kFrameH = 480;

// Centre-crop geometry for "fit shortest axis": the square the model actually sees.
constexpr int kCropSide = kFrameH;
constexpr int kCropX = (kFrameW - kCropSide) / 2;
constexpr int kCropY = 0;

constexpr int kModelW = EI_CLASSIFIER_INPUT_WIDTH;
constexpr int kModelH = EI_CLASSIFIER_INPUT_HEIGHT;

// How often the detector task takes a frame. Inference holds the camera mutex for its
// whole run, so back-to-back inference would starve the video. A gap keeps the stream
// watchable; raise it if the view stutters, drop it to 0 to measure the true frame rate.
constexpr uint32_t kDetectPeriodMs = 500;

constexpr int kMaxReported = 8;

// The stream handler sleeps this long between frames so the detector can get a look in.
// It costs the video a few frames a second and is the difference between a detector that
// runs and one that starves.
constexpr uint32_t kStreamYieldMs = 15;

// A VGA JPEG at quality 10 is tens of kilobytes; this is generous headroom so that a
// detailed scene never overruns it.
constexpr size_t kJpegCapacity = 192 * 1024;

const char *kStreamContentType = "multipart/x-mixed-replace;boundary=xiaoframe";
const char *kStreamBoundary = "\r\n--xiaoframe\r\n";
const char *kStreamPart = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ----------------------------------------------------------------------- state

httpd_handle_t g_control_httpd = nullptr;
httpd_handle_t g_stream_httpd = nullptr;
bool g_servers_up = false;
bool g_camera_ok = false;
bool g_locked = false;
uint32_t g_frames_served = 0;
uint32_t g_last_rejoin = 0;
uint32_t g_last_beat = 0;

SemaphoreHandle_t g_cam_mutex = nullptr;   // guards esp_camera_fb_get/return
SemaphoreHandle_t g_det_mutex = nullptr;   // guards the block below

uint8_t *g_jpeg = nullptr;    // a private copy of one JPEG, so the camera can be released
uint8_t *g_rgb = nullptr;     // one decoded VGA frame, RGB888, in PSRAM
uint8_t *g_input = nullptr;   // the model's 160x160 RGB888 input, in PSRAM

struct Detection {
  char label[24];
  int x, y, w, h;   // full-frame pixels, already mapped out of model space
  float value;
};

Detection g_det[kMaxReported];
int g_det_count = 0;
uint32_t g_det_seq = 0;          // increments per inference, so the page can spot staleness
uint32_t g_det_age_ms = 0;
int32_t g_dsp_ms = 0;
int32_t g_infer_ms = 0;
int32_t g_decode_ms = 0;
float g_threshold = EI_CLASSIFIER_OBJECT_DETECTION_THRESHOLD;

// Why the detector is not producing results, counted rather than guessed. The first
// bring-up attempt returned seq 0 forever with nothing on the serial line, because every
// failure path here was a bare `return false`. Each one now says so once, and keeps a
// tally that /detect reports -- a detector that is quietly doing nothing must not look the
// same as one that is looking at an empty floor.
uint32_t g_n_grab = 0;
uint32_t g_fail_fb = 0;       // esp_camera_fb_get() returned nothing
uint32_t g_fail_mutex = 0;    // could not get on the camera within the timeout
uint32_t g_fail_decode = 0;   // fmt2rgb888() refused the JPEG
uint32_t g_fail_resize = 0;   // crop_and_interpolate_rgb888() refused
uint32_t g_fail_infer = 0;    // run_classifier() returned an error
int g_last_infer_err = 0;

/// Print once per distinct failure, not once per loop: at two frames a second a broken
/// path would otherwise bury the log it is trying to explain.
void sayOnce(uint32_t count, const char *what) {
  if (count == 1) Serial.printf("detect         : FAILING at %s\n", what);
}

uint32_t g_psram_allocs = 0;

// Allocations at or above this go to PSRAM in preference to internal RAM.
constexpr size_t kBigAlloc = 64 * 1024;

// ----------------------------------------------------------------------- allocation
//
// The SDK's own ei_malloc() for the S3 is aligned_alloc(), which can only ever hand back
// INTERNAL RAM. This model's tensor arena is around 300 KB, and after Wi-Fi, lwIP and two
// HTTP servers have taken their share there is not that much internal RAM left — the
// allocation fails, run_classifier() returns an allocation error, and nothing about that
// message points at the real cause. So: internal first, because it is several times
// faster, and PSRAM rather than failure. The count is reported at /status, because an
// arena that silently landed in PSRAM is the explanation for inference being slower than
// the estimate.
//
// These are strong definitions of symbols the SDK declares weak, which is the documented
// way to replace them; they take the header's C++ linkage, not extern "C", or the
// names do not match and the weak versions win silently. 16-byte alignment is not
// decoration — esp-nn issue #7.

}  // namespace

static void *eiAlloc(size_t size) {
  // Big blocks go to PSRAM FIRST, small ones to internal RAM. The tensor arena is around
  // 300 KB and internal RAM is only 320 KB in total, shared with Wi-Fi, lwIP, the camera
  // driver's DMA descriptors and two HTTP servers. Taking the arena from there does not
  // fail cleanly -- it starves those, and the board dies somewhere else entirely: the
  // first bring-up run panicked in ll_cam_memcpy and in the Wi-Fi stack's esf_buf_alloc,
  // neither of which mentions inference. PSRAM is slower, and slower is the right trade.
  const uint32_t first = (size >= kBigAlloc) ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
  const uint32_t second = (size >= kBigAlloc) ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM;

  void *p = heap_caps_aligned_alloc(16, size, first | MALLOC_CAP_8BIT);
  if (p == nullptr) p = heap_caps_aligned_alloc(16, size, second | MALLOC_CAP_8BIT);
  if (p != nullptr && size >= kBigAlloc) ++g_psram_allocs;
  return p;
}

void *ei_malloc(size_t size) { return eiAlloc(size); }

void *ei_calloc(size_t nitems, size_t size) {
  void *p = eiAlloc(nitems * size);
  if (p != nullptr) memset(p, 0, nitems * size);
  return p;
}

void ei_free(void *ptr) {
  // MUST be heap_caps_aligned_free, to match heap_caps_aligned_alloc above.
  //
  // This one cost a debugging session. On this IDF, aligned_alloc goes through
  // tlsf_memalign_offs and hands back a pointer that sits *inside* its block, with the
  // offset recorded ahead of it. heap_caps_free assumes the pointer is the start of a
  // block, so it frees the wrong thing and leaves the heap's free list pointing at
  // nonsense. Nothing fails at that moment -- the next allocation walks the broken list
  // and panics inside tlsf, which reads as a heap bug in ESP-IDF rather than as a
  // mismatched free here. The board crash-looped in exactly that shape.
  //
  // The deprecation warning is about the *name*: a later IDF unified the two. While
  // heap_caps_aligned_alloc exists as its own function, its own free is the right pair.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  if (ptr != nullptr) heap_caps_aligned_free(ptr);
#pragma GCC diagnostic pop
}

namespace {

// ----------------------------------------------------------------------- camera + wi-fi

/// Internal RAM is the scarce one and the one whose exhaustion kills other drivers, so it
/// is reported as both total free and largest contiguous block -- a heap that is free but
/// fragmented fails a large allocation while looking healthy.
void reportMemory(const char *when) {
  Serial.printf("memory %-8s: internal %u KB free (largest block %u KB), PSRAM %u KB free\n",
                when,
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic / exception";
    case ESP_RST_INT_WDT:  return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_BROWNOUT: return "brownout - check the supply";
    default:               return "other";
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
  cfg.frame_size = FRAMESIZE_VGA;
  cfg.jpeg_quality = 10;
  cfg.fb_count = psramFound() ? 2 : 1;
  cfg.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  // WHEN_EMPTY, not LATEST. Two tasks pull frames here -- the stream and the detector --
  // and LATEST lets the driver recycle a buffer that a consumer may still be reading.
  cfg.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  const esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("camera         : INIT FAILED (0x%04x) - reseat the expansion board\n",
                  err);
    return false;
  }
  Serial.println(F("camera         : OK - VGA 640x480 JPEG q10"));
  return true;
}

/// Join with a deadline — see the F7 note in xiao_stream.cpp. Never block forever here.
bool joinWiFi() {
  Serial.printf("wifi           : joining \"%s\" ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < kJoinTimeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("wifi           : OK - IP %s, RSSI %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    Serial.printf("                 open  http://%s/  for the detection view\n",
                  WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.printf("wifi           : FAILED - status %d. Camera stays alive over USB.\n",
                (int)WiFi.status());
  return false;
}

// ----------------------------------------------------------------------- inference

/// Edge Impulse pulls features through this rather than taking a buffer, so that a signal
/// can be larger than RAM. Ours is not, so it is a straight read out of g_input, packing
/// each pixel into the 0xRRGGBB float the image block expects.
int eiGetData(size_t offset, size_t length, float *out) {
  size_t px = offset * 3;
  for (size_t i = 0; i < length; ++i) {
    out[i] = (float)((g_input[px] << 16) + (g_input[px + 1] << 8) + g_input[px + 2]);
    px += 3;
  }
  return EIDSP_OK;
}

bool grabAndPrepare() {
  const uint32_t t0 = millis();

  ++g_n_grab;

  // THE MUTEX IS HELD ONLY LONG ENOUGH TO COPY THE JPEG OUT, not for the decode. The
  // first bring-up attempt decoded inside it, which meant the detector owned the camera
  // for ~100 ms at a time while the stream handler -- which runs at the HTTP server's
  // priority, above this task -- wanted it back every frame. The detector lost 23 of its
  // first 24 attempts and never ran once. A JPEG is tens of kilobytes; copying it is
  // cheap, and everything expensive then happens with the camera free.
  if (xSemaphoreTake(g_cam_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
    sayOnce(++g_fail_mutex, "the camera mutex - the stream is holding it");
    return false;
  }

  size_t len = 0;
  int w = 0, h = 0;
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) {
    sayOnce(++g_fail_fb, "esp_camera_fb_get - no frame from the sensor");
  } else {
    if (fb->len <= kJpegCapacity) {
      memcpy(g_jpeg, fb->buf, fb->len);
      len = fb->len;
      w = (int)fb->width;
      h = (int)fb->height;
    } else {
      sayOnce(++g_fail_fb, "the JPEG copy - frame larger than the buffer");
    }
    esp_camera_fb_return(fb);
  }
  xSemaphoreGive(g_cam_mutex);

  if (len == 0) return false;
  if (w != kFrameW || h != kFrameH) {
    Serial.printf("detect         : frame is %dx%d, expected %dx%d\n",
                  w, h, kFrameW, kFrameH);
    return false;
  }

  if (!fmt2rgb888(g_jpeg, len, PIXFORMAT_JPEG, g_rgb)) {
    sayOnce(++g_fail_decode, "fmt2rgb888 - the JPEG would not decode");
    return false;
  }
  g_decode_ms = (int32_t)(millis() - t0);

  // Centre-crop to a square and scale to the model's input, which is what "fit shortest
  // axis" did to every training image. Getting this wrong does not fail loudly -- it just
  // quietly costs accuracy, because the model then sees a differently framed world than
  // it was trained on.
  const int rc = ei::image::processing::crop_and_interpolate_rgb888(
      g_rgb, kFrameW, kFrameH, g_input, kModelW, kModelH);
  if (rc != EIDSP_OK) {
    sayOnce(++g_fail_resize, "crop_and_interpolate_rgb888");
    return false;
  }
  return true;
}

void publish(const ei_impulse_result_t &result) {
  xSemaphoreTake(g_det_mutex, portMAX_DELAY);
  g_det_count = 0;
  for (uint32_t i = 0; i < result.bounding_boxes_count && g_det_count < kMaxReported; ++i) {
    const auto &bb = result.bounding_boxes[i];
    if (bb.value < g_threshold) continue;

    Detection &d = g_det[g_det_count++];
    snprintf(d.label, sizeof(d.label), "%s", bb.label);
    d.value = bb.value;
    // Model space -> crop space -> full frame. FOMO's "box" is one grid cell, so treat it
    // as a centroid marker rather than an outline of the rock.
    const float scale = (float)kCropSide / (float)kModelW;
    d.x = kCropX + (int)(bb.x * scale);
    d.y = kCropY + (int)(bb.y * scale);
    d.w = (int)(bb.width * scale);
    d.h = (int)(bb.height * scale);
  }
  g_dsp_ms = (int32_t)(result.timing.dsp_us / 1000);
  g_infer_ms = (int32_t)(result.timing.classification_us / 1000);
  g_det_age_ms = millis();
  ++g_det_seq;
  xSemaphoreGive(g_det_mutex);
}

void detectTask(void *) {
  // Let the camera and the network settle before the first grab. The detector is the only
  // thing in this sketch that touches the sensor unprompted, and on the first bring-up run
  // it did so within a millisecond of the servers coming up, which is when the driver
  // panicked. Then throw two frames away: the first frames after init carry the sensor's
  // start-up state, not the scene.
  vTaskDelay(pdMS_TO_TICKS(2000));
  for (int i = 0; i < 2; ++i) {
    if (xSemaphoreTake(g_cam_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb != nullptr) esp_camera_fb_return(fb);
      xSemaphoreGive(g_cam_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  Serial.println(F("detect         : warm-up done, first inference now"));
  reportMemory("pre-infer");

  ei_impulse_result_t result = {0};
  signal_t signal;
  signal.total_length = (size_t)kModelW * kModelH;
  signal.get_data = &eiGetData;

  while (true) {
    if (!g_camera_ok) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (grabAndPrepare()) {
      const EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
      if (err == EI_IMPULSE_OK) {
        if (g_det_seq == 0) reportMemory("post-infer");
        publish(result);
      } else {
        Serial.printf("detect         : run_classifier failed (%d)\n", (int)err);
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(kDetectPeriodMs));
  }
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
  xSemaphoreTake(g_cam_mutex, portMAX_DELAY);
  camera_fb_t *fb = esp_camera_fb_get();
  esp_err_t res = ESP_FAIL;
  if (fb != nullptr) {
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    ++g_frames_served;
  }
  xSemaphoreGive(g_cam_mutex);
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
    // The mutex is taken per frame, not for the whole stream: the detector has to be able
    // to get in between frames, or it never runs at all.
    xSemaphoreTake(g_cam_mutex, portMAX_DELAY);
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == nullptr) {
      xSemaphoreGive(g_cam_mutex);
      res = ESP_FAIL;
      break;
    }
    const size_t hlen = snprintf(part, sizeof(part), kStreamPart, (unsigned)fb->len);
    res = httpd_resp_send_chunk(req, kStreamBoundary, strlen(kStreamBoundary));
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, part, hlen);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    xSemaphoreGive(g_cam_mutex);

    if (res != ESP_OK) break;
    ++g_frames_served;

    // Yield between frames. This handler runs above the detector's priority and would
    // otherwise re-take the mutex before the detector is ever scheduled -- which is
    // exactly what happened on the first bring-up run.
    vTaskDelay(pdMS_TO_TICKS(kStreamYieldMs));
  }
  return res;
}

esp_err_t detectHandler(httpd_req_t *req) {
  String json = "{";
  xSemaphoreTake(g_det_mutex, portMAX_DELAY);
  json += "\"seq\":" + String((unsigned long)g_det_seq);
  json += ",\"age_ms\":" + String((unsigned long)(millis() - g_det_age_ms));
  json += ",\"dsp_ms\":" + String(g_dsp_ms);
  json += ",\"infer_ms\":" + String(g_infer_ms);
  json += ",\"decode_ms\":" + String(g_decode_ms);
  json += ",\"threshold\":" + String(g_threshold, 2);
  json += ",\"grabs\":" + String((unsigned long)g_n_grab);
  json += ",\"fail_mutex\":" + String((unsigned long)g_fail_mutex);
  json += ",\"fail_fb\":" + String((unsigned long)g_fail_fb);
  json += ",\"fail_decode\":" + String((unsigned long)g_fail_decode);
  json += ",\"fail_resize\":" + String((unsigned long)g_fail_resize);
  json += ",\"fail_infer\":" + String((unsigned long)g_fail_infer);
  json += ",\"last_infer_err\":" + String(g_last_infer_err);
  json += ",\"frame_w\":" + String(kFrameW) + ",\"frame_h\":" + String(kFrameH);
  json += ",\"detections\":[";
  for (int i = 0; i < g_det_count; ++i) {
    if (i) json += ",";
    json += "{\"label\":\"" + String(g_det[i].label) + "\"";
    json += ",\"x\":" + String(g_det[i].x) + ",\"y\":" + String(g_det[i].y);
    json += ",\"w\":" + String(g_det[i].w) + ",\"h\":" + String(g_det[i].h);
    json += ",\"value\":" + String(g_det[i].value, 3) + "}";
  }
  xSemaphoreGive(g_det_mutex);
  json += "]}";

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
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
  json += ",\"psram_allocs\":" + String((unsigned long)g_psram_allocs);
  json += ",\"model\":\"" EI_CLASSIFIER_PROJECT_NAME "\"";
  json += ",\"model_input\":" + String(kModelW);
  if (s != nullptr) {
    json += ",\"framesize\":" + String((int)s->status.framesize);
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

/// Same reasoning as xiao_stream.cpp: the corpus was shot locked, so inference must run
/// locked too, or the model meets colours it never saw in training.
esp_err_t lockHandler(httpd_req_t *req) {
  sensor_t *s = esp_camera_sensor_get();
  if (s == nullptr) return sendText(req, "503 Service Unavailable", "no sensor");
  s->set_gain_ctrl(s, 0);
  s->set_exposure_ctrl(s, 0);
  s->set_whitebal(s, 0);
  s->set_awb_gain(s, 0);
  g_locked = true;
  Serial.println(F("sensor         : LOCKED (agc/aec/awb off)"));
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
  return sendText(req, "200 OK", "unlocked");
}

/// One runtime knob: the confidence a detection needs before it is reported. This filters
/// the model's output; it does not change the model. /threshold?val=0.7
esp_err_t thresholdHandler(httpd_req_t *req) {
  char query[48];
  char val[16];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
      httpd_query_key_value(query, "val", val, sizeof(val)) == ESP_OK) {
    const float v = atof(val);
    if (v > 0.0f && v <= 1.0f) {
      g_threshold = v;
      Serial.printf("detect         : threshold now %.2f\n", g_threshold);
    }
  }
  char body[32];
  snprintf(body, sizeof(body), "%.2f", g_threshold);
  return sendText(req, "200 OK", body);
}

esp_err_t rootHandler(httpd_req_t *req) {
  const String ip = WiFi.localIP().toString();
  String page = F("<!doctype html><meta name=viewport content='width=device-width'>"
                  "<style>"
                  "body{font:15px system-ui;margin:1.5rem;max-width:46rem;background:#111;"
                  "color:#eee}"
                  "#wrap{position:relative;display:inline-block;width:100%}"
                  "#v{width:100%;display:block;border:1px solid #444}"
                  "#c{position:absolute;left:0;top:0;width:100%;height:100%}"
                  "#hud{font:600 20px system-ui;margin:.6rem 0;min-height:1.4em}"
                  "#t{color:#888;font-size:13px}"
                  "a{color:#6cf;margin-right:1rem}"
                  "</style>"
                  "<h2>Rock detection &mdash; step 2.0e</h2>"
                  "<div id=wrap><img id=v><canvas id=c></canvas></div>"
                  "<div id=hud>&nbsp;</div><div id=t>starting...</div>");
  page += "<p><a href='/detect'>/detect</a><a href='/status'>/status</a>"
          "<a href='/lock'>/lock</a><a href='/unlock'>/unlock</a>"
          "<a href='http://" + ip + ":" + String(kStreamPort) + "/stream'>raw stream</a></p>";
  page += F("<p style='color:#888'>Boxes are drawn here in the browser, not burned into "
            "the video &mdash; the board has better things to do than re-encode JPEG. "
            "FOMO reports a centroid, so the square marks where the rock is, not its "
            "outline.</p>");
  page += "<script>";
  page += "var ip='" + ip + "',sp='" + String(kStreamPort) + "';";
  page += F("document.getElementById('v').src='http://'+ip+':'+sp+'/stream';"
            "var c=document.getElementById('c'),g=c.getContext('2d'),"
            "v=document.getElementById('v'),hud=document.getElementById('hud'),"
            "t=document.getElementById('t');"
            "function poll(){fetch('/detect').then(r=>r.json()).then(d=>{"
            "c.width=d.frame_w;c.height=d.frame_h;"
            "g.clearRect(0,0,c.width,c.height);"
            "var names=[];"
            "d.detections.forEach(function(b){"
            "g.strokeStyle='#0f0';g.lineWidth=4;"
            "g.strokeRect(b.x,b.y,Math.max(b.w,24),Math.max(b.h,24));"
            "g.fillStyle='#0f0';g.font='bold 22px system-ui';"
            "g.fillText(b.label+' '+(b.value*100).toFixed(0)+'%',b.x,Math.max(b.y-8,22));"
            "names.push(b.label.toUpperCase());});"
            "hud.textContent=names.length?names.join(', ')+' DETECTED':'no rock in view';"
            "hud.style.color=names.length?'#0f0':'#888';"
            "t.textContent='inference '+d.infer_ms+' ms, decode '+d.decode_ms"
            "+' ms, last result '+d.age_ms+' ms ago, threshold '+d.threshold;"
            "}).catch(e=>{t.textContent='board not answering';});}"
            "setInterval(poll,400);poll();");
  page += "</script>";

  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
}

void startServers() {
  if (g_servers_up) return;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = kControlPort;
  config.ctrl_port = 32768;
  config.max_uri_handlers = 10;

  httpd_uri_t root_uri      = {"/",          HTTP_GET, rootHandler,      nullptr};
  httpd_uri_t snap_uri      = {"/snap",      HTTP_GET, snapHandler,      nullptr};
  httpd_uri_t detect_uri    = {"/detect",    HTTP_GET, detectHandler,    nullptr};
  httpd_uri_t status_uri    = {"/status",    HTTP_GET, statusHandler,    nullptr};
  httpd_uri_t lock_uri      = {"/lock",      HTTP_GET, lockHandler,      nullptr};
  httpd_uri_t unlock_uri    = {"/unlock",    HTTP_GET, unlockHandler,    nullptr};
  httpd_uri_t threshold_uri = {"/threshold", HTTP_GET, thresholdHandler, nullptr};

  if (httpd_start(&g_control_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(g_control_httpd, &root_uri);
    httpd_register_uri_handler(g_control_httpd, &snap_uri);
    httpd_register_uri_handler(g_control_httpd, &detect_uri);
    httpd_register_uri_handler(g_control_httpd, &status_uri);
    httpd_register_uri_handler(g_control_httpd, &lock_uri);
    httpd_register_uri_handler(g_control_httpd, &unlock_uri);
    httpd_register_uri_handler(g_control_httpd, &threshold_uri);
    Serial.printf("http           : control on port %u\n", kControlPort);
  } else {
    Serial.println(F("http           : control server FAILED to start"));
  }

  // Second server on its own port — an MJPEG handler never returns, so sharing one
  // instance starves every other endpoint. That was measured at 2.0b.2.
  config.server_port = kStreamPort;
  config.ctrl_port = 32769;
  httpd_uri_t stream_uri = {"/stream", HTTP_GET, streamHandler, nullptr};
  if (httpd_start(&g_stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(g_stream_httpd, &stream_uri);
    Serial.printf("http           : stream  on port %u\n", kStreamPort);
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
  Serial.println(F("=== XIAO ESP32S3 Sense live rock detection - step 2.0e ==="));
  Serial.printf("reset reason   : %s\n", resetReason());
  Serial.printf("PSRAM          : %s\n",
                psramFound() ? "yes" : "NONE - check memory_type is qio_opi");
  Serial.printf("model          : %s, %dx%d, %d class(es), threshold %.2f\n",
                EI_CLASSIFIER_PROJECT_NAME, kModelW, kModelH,
                (int)EI_CLASSIFIER_LABEL_COUNT, g_threshold);

  reportMemory("at boot");

  g_cam_mutex = xSemaphoreCreateMutex();
  g_det_mutex = xSemaphoreCreateMutex();

  // Camera FIRST, buffers after. The driver wants both internal RAM for its DMA
  // descriptors and PSRAM for its frame buffers, and it should get first pick of each --
  // a megabyte claimed before it starts is a megabyte it cannot have.
  g_camera_ok = startCamera();
  reportMemory("post-cam");

  // A VGA RGB888 frame is 900 KB, which internal RAM does not have, so both buffers are
  // explicitly PSRAM. If they fail the detector stays quiet rather than crashing in a way
  // that looks like a camera fault.
  g_jpeg = (uint8_t *)heap_caps_malloc(kJpegCapacity, MALLOC_CAP_SPIRAM);
  g_rgb = (uint8_t *)heap_caps_malloc((size_t)kFrameW * kFrameH * 3, MALLOC_CAP_SPIRAM);
  g_input = (uint8_t *)heap_caps_malloc((size_t)kModelW * kModelH * 3, MALLOC_CAP_SPIRAM);
  if (g_jpeg == nullptr || g_rgb == nullptr || g_input == nullptr) {
    Serial.println(F("buffers        : FAILED - no PSRAM. Detection disabled."));
  } else {
    Serial.printf("buffers        : %u KB decode + %u KB model input, both PSRAM\n",
                  (unsigned)((size_t)kFrameW * kFrameH * 3 / 1024),
                  (unsigned)((size_t)kModelW * kModelH * 3 / 1024));
  }

  if (joinWiFi()) startServers();
  reportMemory("post-wifi");

  if (g_jpeg != nullptr && g_rgb != nullptr && g_input != nullptr) {
    // Core 1: core 0 carries the Wi-Fi and lwIP tasks, and inference is long enough to
    // hurt them. The stack is generous because the SDK recurses through the graph.
    xTaskCreatePinnedToCore(detectTask, "detect", 16384, nullptr, 3, nullptr, 1);
    Serial.println(F("detect         : task started on core 1"));
  }
}

void loop() {
  const uint32_t now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(kStatusLed, LOW);
    if (!g_servers_up) startServers();
  } else {
    digitalWrite(kStatusLed, ((now / 500) % 2) ? LOW : HIGH);
    if (now - g_last_rejoin >= kRejoinIntervalMs) {
      g_last_rejoin = now;
      if (joinWiFi()) startServers();
    }
  }

  if (now - g_last_beat >= kHeartbeatMs) {
    g_last_beat = now;
    xSemaphoreTake(g_det_mutex, portMAX_DELAY);
    const int n = g_det_count;
    const char *first = n ? g_det[0].label : "-";
    const float conf = n ? g_det[0].value : 0.0f;
    const int32_t infer = g_infer_ms;
    xSemaphoreGive(g_det_mutex);

    Serial.printf("[%lu] %s  infer %ld ms  detections %d (%s %.2f)  heap %u KB\n",
                  (unsigned long)now, WiFi.localIP().toString().c_str(),
                  (long)infer, n, first, conf, (unsigned)(ESP.getFreeHeap() / 1024));
  }

  delay(20);
}
