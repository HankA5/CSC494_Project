// ============================================================
//  Lizard Monitor — XIAO ESP32-C6 Sketch
//  Board: XIAO_ESP32C6
//  Reads AHT10 sensor, receives ESP32-CAM position data,
//  logs everything for one hour, emails a report.
//
//  This build adds an ESP32-CAM handshake at startup, mirroring
//  the AHT10 "Looking for..." pattern. Pair with the companion
//  ESP32-CAM sketch, which sends "BOOT,CAM-READY" at boot.
// ============================================================
 
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <WiFi.h>
#include <ESP_Mail_Client.h>
 
// ── Wi-Fi credentials ──
#define WIFI_SSID     "HLAPTOP4093"
#define WIFI_PASSWORD "LizardExperiment"
 
// ── Gmail credentials ──
#define SMTP_HOST     "smtp.gmail.com"
#define SMTP_PORT     465
#define SENDER_EMAIL  "hankslaby@gmail.com"
#define SENDER_PASS   "hkby myju gzxq yqoc"
#define RECIPIENT     "hankslaby@gmail.com"
 
// ── UART from ESP32-CAM ──
// D6 = GPIO16 (TX to CAM), D7 = GPIO17 (RX from CAM)
#define XIAO_RX 17
#define XIAO_TX 16
 
// ── Timing ──
#define SENSOR_INTERVAL_MS  60000
#define REPORT_INTERVAL_MS  3600000
 
// ── CAM handshake timeout ──
// How long to wait for the CAM's boot line before giving up
// and continuing without it.
#define CAM_HANDSHAKE_TIMEOUT_MS  15000
 
// ── Data storage (60 readings max, one per minute) ──
#define MAX_READINGS 60
float tempReadings[MAX_READINGS];
float humReadings[MAX_READINGS];
int   hotReadings[MAX_READINGS];
int   coolReadings[MAX_READINGS];
int   noneReadings[MAX_READINGS];
int   readingCount = 0;
 
// ── Pending CAM data ──
int pendingHot  = 0;
int pendingCool = 0;
int pendingNone = 0;
bool pendingCamReady = false;
 
// ── Globals ──
Adafruit_AHTX0 aht;
SMTPSession smtp;
unsigned long lastSensorTime = 0;
unsigned long lastReportTime = 0;
String camBuffer = "";
bool   camLinkUp = false;   // set true once the CAM greets us
 
// ============================================================
void setup() {
  delay(3000);
  Serial.begin(115200);
  Serial.println("Booting XIAO ESP32-C6...");
 
  // ── UART from ESP32-CAM ──
  Serial1.begin(9600, SERIAL_8N1, XIAO_RX, XIAO_TX);
  Serial.println("UART ready.");
 
  // ── AHT10 sensor ──
  Wire.begin(22, 23);
  delay(500);
  Serial.println("Looking for AHT10...");
  while (!aht.begin(&Wire)) {
    Serial.println("AHT10 not found — retrying in 1 second...");
    delay(1000);
  }
  Serial.println("AHT10 ready.");
 
  // ── ESP32-CAM handshake ──
  // Wait up to CAM_HANDSHAKE_TIMEOUT_MS for ANY line to arrive
  // on Serial1. The companion CAM sketch sends "BOOT,CAM-READY"
  // right after its own setup, so under normal conditions this
  // returns in under 2 seconds.
  Serial.println("Looking for ESP32-CAM on UART...");
  unsigned long waitStart = millis();
  unsigned long lastTick  = 0;
 
  while (!camLinkUp &&
         (millis() - waitStart) < CAM_HANDSHAKE_TIMEOUT_MS) {
    while (Serial1.available()) {
      char c = Serial1.read();
      if (c == '\r') continue;
      if (c == '\n') {
        if (camBuffer.length() > 0) {
          camBuffer.trim();
          Serial.println("ESP32-CAM said: " + camBuffer);
          camBuffer = "";
          camLinkUp = true;
          break;
        }
        camBuffer = "";
      } else {
        camBuffer += c;
        if (camBuffer.length() > 200) camBuffer = "";
      }
    }
    if (!camLinkUp && millis() - lastTick >= 1000) {
      lastTick = millis();
      Serial.println("ESP32-CAM not responding yet — retrying...");
    }
    delay(20);
  }
 
  if (camLinkUp) {
    Serial.println("ESP32-CAM detected. UART link up.");
  } else {
    Serial.println("ESP32-CAM did not respond within 15 seconds.");
    Serial.println("Continuing without it — sensor data will still be");
    Serial.println("logged and emailed, CAM fields will stay at 0.");
  }
 
  // ── Wi-Fi ──
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  Serial.printf("Found %d networks:\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("  %s  (%d dBm)  ch%d\n",
      WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi connected.");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWi-Fi failed — check SSID and password.");
    Serial.println("Continuing without Wi-Fi — email will not send.");
  }
 
  lastSensorTime = millis();
  lastReportTime = millis();
  Serial.println("XIAO ESP32-C6 ready. Monitoring started.");
}
 
// ============================================================
void loop() {
  // ── Read incoming UART data from ESP32-CAM ──
  while (Serial1.available()) {
    char c = Serial1.read();
    camBuffer += c;
    if (c == '\n') {
      parseCamData(camBuffer);
      camBuffer = "";
    }
  }
 
  // ── Read AHT10 sensor every minute ──
  if (millis() - lastSensorTime >= SENSOR_INTERVAL_MS) {
    lastSensorTime = millis();
    readSensor();
  }
 
  // ── Send email report every hour ──
  if (millis() - lastReportTime >= REPORT_INTERVAL_MS) {
    lastReportTime = millis();
    sendReport();
  }
}
 
// ============================================================
void parseCamData(String line) {
  line.trim();
  Serial.println("Received from CAM: " + line);
 
  // If the CAM re-boots mid-run, treat its greeting as a link
  // recovery event rather than trying to parse it as data.
  if (line.startsWith("BOOT,")) {
    camLinkUp = true;
    Serial.println("  CAM boot message — link (re)established.");
    return;
  }
 
  int h = 0, c = 0, n = 0;
  int idx = 0;
  String parts[6];
  int partCount = 0;
 
  for (int i = 0; i <= line.length() && partCount < 6; i++) {
    if (i == line.length() || line[i] == ',') {
      parts[partCount++] = line.substring(idx, i);
      idx = i + 1;
    }
  }
 
  if (partCount == 6) {
    h = parts[1].toInt();
    c = parts[3].toInt();
    n = parts[5].toInt();
    Serial.printf("  Parsed — Hot: %d  Cool: %d  None: %d\n", h, c, n);
    camLinkUp = true;   // any valid data proves the link is up
  } else {
    Serial.println("  Could not parse CAM data.");
  }
 
  storePendingCamData(h, c, n);
}
 
// ============================================================
void storePendingCamData(int h, int c, int n) {
  pendingHot  = h;
  pendingCool = c;
  pendingNone = n;
  pendingCamReady = true;
}
 
// ============================================================
void readSensor() {
  if (readingCount >= MAX_READINGS) return;
 
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
 
  float t = temp.temperature;
  float h = humidity.relative_humidity;
 
  tempReadings[readingCount] = t;
  humReadings[readingCount]  = h;
  hotReadings[readingCount]  = pendingHot;
  coolReadings[readingCount] = pendingCool;
  noneReadings[readingCount] = pendingNone;
 
  Serial.printf("Reading %d — Temp: %.1f C  Humidity: %.1f%%  Hot: %d  Cool: %d  None: %d\n",
    readingCount + 1, t, h,
    pendingHot, pendingCool, pendingNone);
 
  readingCount++;
 
  pendingHot       = 0;
  pendingCool      = 0;
  pendingNone      = 0;
  pendingCamReady  = false;
}
 
// ============================================================
void sendReport() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No Wi-Fi — skipping email.");
    return;
  }
  if (readingCount == 0) {
    Serial.println("No data yet — skipping email.");
    return;
  }
 
  Serial.println("Building report...");
 
  float avgTemp = 0, avgHum = 0;
  float minTemp = tempReadings[0], maxTemp = tempReadings[0];
  int totalHot = 0, totalCool = 0, totalNone = 0;
 
  for (int i = 0; i < readingCount; i++) {
    avgTemp   += tempReadings[i];
    avgHum    += humReadings[i];
    totalHot  += hotReadings[i];
    totalCool += coolReadings[i];
    totalNone += noneReadings[i];
    if (tempReadings[i] < minTemp) minTemp = tempReadings[i];
    if (tempReadings[i] > maxTemp) maxTemp = tempReadings[i];
  }
  avgTemp /= readingCount;
  avgHum  /= readingCount;
 
  int totalObservations = totalHot + totalCool + totalNone;
  float hotPct  = totalObservations > 0 ? (totalHot  * 100.0 / totalObservations) : 0;
  float coolPct = totalObservations > 0 ? (totalCool * 100.0 / totalObservations) : 0;
 
  String dominantSide = (totalHot > totalCool) ? "HOT SIDE" : "COOL SIDE";
  if (totalHot == totalCool) dominantSide = "EQUAL TIME ON BOTH SIDES";
 
  String body = "LIZARD TANK MONITOR — HOURLY REPORT\n";
  body += "=====================================\n\n";
  body += "CAM LINK STATUS\n";
  body += "---------------\n";
  body += camLinkUp ? "ESP32-CAM link: UP\n\n"
                    : "ESP32-CAM link: DOWN (no data received)\n\n";
  body += "LIZARD POSITION SUMMARY\n";
  body += "-----------------------\n";
  body += "Dominant side:  " + dominantSide + "\n";
  body += "Hot side:       " + String(hotPct,  1) + "% of observations\n";
  body += "Cool side:      " + String(coolPct, 1) + "% of observations\n\n";
  body += "TEMPERATURE (hot spot)\n";
  body += "----------------------\n";
  body += "Average:  " + String(avgTemp, 1) + " C  (" + String((avgTemp * 9.0/5.0) + 32, 1) + " F)\n";
  body += "Min:      " + String(minTemp, 1) + " C  (" + String((minTemp * 9.0/5.0) + 32, 1) + " F)\n";
  body += "Max:      " + String(maxTemp, 1) + " C  (" + String((maxTemp * 9.0/5.0) + 32, 1) + " F)\n\n";
  body += "HUMIDITY (hot spot)\n";
  body += "-------------------\n";
  body += "Average:  " + String(avgHum, 1) + "%\n\n";
  body += "MINUTE-BY-MINUTE LOG\n";
  body += "--------------------\n";
 
  for (int i = 0; i < readingCount; i++) {
    body += "Min " + String(i + 1) + ": ";
    body += String(tempReadings[i], 1) + "C  ";
    body += String(humReadings[i],  1) + "%  ";
    body += "Hot:"  + String(hotReadings[i])  + " ";
    body += "Cool:" + String(coolReadings[i]) + " ";
    body += "None:" + String(noneReadings[i]) + "\n";
  }
 
  body += "\nEnd of report.\n";
 
  ESP_Mail_Session session;
  session.server.host_name  = SMTP_HOST;
  session.server.port       = SMTP_PORT;
  session.login.email       = SENDER_EMAIL;
  session.login.password    = SENDER_PASS;
  session.login.user_domain = "";
 
  SMTP_Message message;
  message.sender.name  = "Lizard Monitor";
  message.sender.email = SENDER_EMAIL;
  message.subject      = "Lizard Tank Report — " + String(readingCount) + " min of data";
  message.addRecipient("Hank", RECIPIENT);
  message.text.content = body.c_str();
  message.text.charSet = "utf-8";
 
  smtp.debug(1);
 
  if (!smtp.connect(&session)) {
    Serial.println("SMTP connect failed: " + smtp.errorReason());
    return;
  }
 
  if (!MailClient.sendMail(&smtp, &message)) {
    Serial.println("Email send failed: " + smtp.errorReason());
  } else {
    Serial.println("Email sent successfully.");
  }
 
  smtp.closeSession();
  readingCount = 0;
}