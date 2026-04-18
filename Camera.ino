// ============================================================
//  ESP32-CAM Lizard Watcher
//  Board: AI-Thinker ESP32-CAM
//
//  Watches a tank, splits the frame into a COOL (left) half
//  and a HOT (right) half, and counts per-frame motion
//  detections in each half via frame differencing.
//
//  Every 10 seconds it sends one CSV line to the XIAO ESP32-C6
//  over UART at 9600 baud:
//      Hot,<hotCount>,Cool,<coolCount>,None,<noneCount>\n
//
//  Wiring (to XIAO ESP32-C6):
//      CAM GPIO13  (TX)  ->  XIAO D7 / GPIO17 (RX)
//      CAM GND           ->  XIAO GND  (REQUIRED)
//      CAM GPIO12        ->  leave UNCONNECTED, or pull to GND
//                            with a 10k resistor. It is a
//                            strapping pin; a floating HIGH at
//                            boot can prevent the CAM booting.
// ============================================================
 
#include "esp_camera.h"
#include <HardwareSerial.h>
 
// ---- AI-Thinker ESP32-CAM pin map ----
#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27
#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22
 
// ---- UART to XIAO ----
// TX only — CAM never needs to receive from the XIAO.
#define TX_TO_XIAO 13
HardwareSerial Link(2);     // use UART2; pins are remapped below
 
// ---- Timing ----
#define REPORT_INTERVAL_MS  10000   // send a line every 10 s
 
// ---- Detection tuning ----
// A pixel is "moving" if its brightness changed by more than
// this much (0-255) since the last frame.
#define PIXEL_DIFF_THRESHOLD     25
// A half-frame must have at least this many moving pixels
// to be considered an actual detection rather than noise.
#define REGION_MOTION_THRESHOLD  60
 
// ---- State ----
uint8_t  *prevFrame   = nullptr;
size_t    prevFrameLen = 0;
bool      havePrev     = false;
 
int hotCount  = 0;
int coolCount = 0;
int noneCount = 0;
 
unsigned long lastReportMs = 0;
 
// ============================================================
void setup() {
  Serial.begin(115200);                                // USB debug
  delay(300);
  Serial.println();
  Serial.println("ESP32-CAM Lizard Watcher booting...");
 
  // UART to XIAO (TX only — pass -1 for RX)
  Link.begin(9600, SERIAL_8N1, /*rx*/ -1, /*tx*/ TX_TO_XIAO);
  Serial.println("UART to XIAO ready on GPIO13 @ 9600.");
 
  // ---- Camera config ----
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_GRAYSCALE;           // 1 byte / pixel
  config.frame_size   = FRAMESIZE_QQVGA;               // 160 x 120
  config.fb_count     = 1;
  config.fb_location  = CAMERA_FB_IN_DRAM;
  config.grab_mode    = CAMERA_GRAB_LATEST;
 
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x — halting.\n", err);
    while (true) { delay(1000); }
  }
  Serial.println("Camera init OK.");
 
  lastReportMs = millis();
}
 
// ============================================================
void loop() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Frame grab failed.");
    delay(50);
    return;
  }
 
  // On the very first frame we just cache it and move on.
  if (!havePrev) {
    prevFrame = (uint8_t*) malloc(fb->len);
    if (prevFrame) {
      memcpy(prevFrame, fb->buf, fb->len);
      prevFrameLen = fb->len;
      havePrev = true;
    } else {
      Serial.println("malloc for prevFrame failed.");
    }
    esp_camera_fb_return(fb);
    return;
  }
 
  // Safety check: frame size shouldn't change, but just in case.
  if (fb->len != prevFrameLen) {
    free(prevFrame);
    prevFrame = (uint8_t*) malloc(fb->len);
    if (prevFrame) {
      memcpy(prevFrame, fb->buf, fb->len);
      prevFrameLen = fb->len;
    } else {
      havePrev = false;
    }
    esp_camera_fb_return(fb);
    return;
  }
 
  // ---- Frame differencing, split into left (cool) / right (hot) ----
  const int w      = fb->width;
  const int h      = fb->height;
  const int halfW  = w / 2;
 
  int leftMotion  = 0;
  int rightMotion = 0;
 
  for (int y = 0; y < h; y++) {
    const uint8_t *curRow  = fb->buf   + y * w;
    const uint8_t *prevRow = prevFrame + y * w;
    for (int x = 0; x < w; x++) {
      int diff = (int)curRow[x] - (int)prevRow[x];
      if (diff < 0) diff = -diff;
      if (diff > PIXEL_DIFF_THRESHOLD) {
        if (x < halfW) leftMotion++;
        else           rightMotion++;
      }
    }
  }
 
  // Classify this single frame.
  if (rightMotion > REGION_MOTION_THRESHOLD &&
      rightMotion > leftMotion) {
    hotCount++;
  } else if (leftMotion > REGION_MOTION_THRESHOLD &&
             leftMotion > rightMotion) {
    coolCount++;
  } else {
    noneCount++;
  }
 
  // Current frame becomes the new baseline.
  memcpy(prevFrame, fb->buf, fb->len);
  esp_camera_fb_return(fb);
 
  // ---- Periodic report to XIAO ----
  if (millis() - lastReportMs >= REPORT_INTERVAL_MS) {
    String line =
      "Hot,"  + String(hotCount)  +
      ",Cool," + String(coolCount) +
      ",None," + String(noneCount);
 
    Link.println(line);                     // -> XIAO
    Serial.println("Sent: " + line);        // -> USB for debugging
 
    hotCount = coolCount = noneCount = 0;
    lastReportMs = millis();
  }
 
  // Small yield so we don't hammer the camera driver.
  delay(20);
}
 
// ============================================================
//  NOTE on stationary lizards
//  --------------------------
//  Frame differencing only registers CHANGE between consecutive
//  frames. A lizard that sits perfectly still will be counted as
//  "None" even though it is clearly on one side. If that becomes
//  a problem in practice, replace the frame-to-frame diff above
//  with background subtraction:
//
//    - keep a "background" buffer that is an exponential moving
//      average of recent frames (e.g.  bg = 0.98*bg + 0.02*cur ),
//    - compare each new frame to that background instead of to
//      the previous frame,
//    - everything else stays the same.
//
//  That will detect a stationary lizard because it differs from
//  the empty-tank background, and the background will slowly
//  adapt if lighting drifts.
// ============================================================