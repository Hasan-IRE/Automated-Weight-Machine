/*
  ============================================================
       SMART WEIGHING SYSTEM - ESP32-S3-CAM
       SINGLE BOARD ALL-IN-ONE COMPLETE PROJECT
  ============================================================

  Features:
  1. HX711 Weight Measurement & Auto Stability Detection
  2. 0.96" I2C OLED Live Weight & Status Display
  3. Automatic Camera Snapshot upon Stable Weight
  4. Automatic Upload to Local Node.js Backend Server
  5. Built-in Web Server: Live Stream (/stream), Snapshot (/capture), Health (/health), Dashboard (/)
  6. Real-time Serial Commands: Tare (t), Raw (r), Health (h), Calibrate (c <weight>)

  Board: Generic ESP32-S3-WROOM-CAM (N16R8)
  Arduino IDE Settings:
    - Board: ESP32S3 Dev Module
    - USB CDC On Boot: Enabled
    - PSRAM: OPI PSRAM
  ============================================================
*/

#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Wire.h>
#include <HX711.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "esp_camera.h"
#include <math.h>

// ============================================================
// 1. WIFI & BACKEND CONFIGURATION
// ============================================================
const char* WIFI_SSID     = "UFTB-HOSTEL";
const char* WIFI_PASSWORD = "Uftb@2018#";

// Local PC Node.js Backend URL (IPv4 of your computer)
const char* BACKEND_URL   = "http://172.16.114.152:3000/api/upload-image";

// ============================================================
// 2. CAMERA GPIO CONFIGURATION (Generic ESP32-S3-WROOM-CAM)
// ============================================================
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM      4
#define SIOC_GPIO_NUM      5

#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM        8
#define Y3_GPIO_NUM        9
#define Y2_GPIO_NUM       11

#define VSYNC_GPIO_NUM     6
#define HREF_GPIO_NUM      7
#define PCLK_GPIO_NUM     13

// ============================================================
// 3. OLED CONFIGURATION (I2C)
// ============================================================
#define OLED_SDA 1
#define OLED_SCL 2
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool oledAvailable = false;

// ============================================================
// 4. HX711 LOAD CELL CONFIGURATION
// ============================================================
#define HX711_DOUT 47
#define HX711_SCK  48

HX711 scale;
bool scaleReady = false;

// Calibration factor: Use Serial command "c 500" with a known mass (e.g. 500g)
// 420.0f is standard initial factor to prevent raw ADC noise
float CALIBRATION_FACTOR = 420.0f;

// ============================================================
// 5. SYSTEM SETTINGS
// ============================================================
#define SAMPLE_INTERVAL_MS    250
#define STABLE_DURATION_MS    1500
#define STABLE_DELTA_GRAMS    3.0f
#define EMPTY_THRESHOLD_GRAMS 10.0f
#define SAMPLE_COUNT          8

// ============================================================
// 6. SERVER & STATE VARIABLES
// ============================================================
WebServer server(80);
bool cameraReady = false;
String cameraIP = "";

float samples[SAMPLE_COUNT] = {0};
int sampleIndex = 0;
bool samplesFilled = false;

unsigned long lastSampleMs = 0;
unsigned long stableSinceMs = 0;
unsigned long lastPrintMs = 0;
bool captureDone = false;
float currentWeight = 0;
String systemStatus = "STARTING";

// Forward declarations
void processWeight();
void processSerialCommands();

// ============================================================
// 7. OLED DISPLAY
// ============================================================
void showDisplay(float grams, String status) {
  if (!oledAvailable) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("SMART WEIGHING");
  display.drawLine(0, 12, 127, 12, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print(grams, 1);
  display.println(" g");

  display.setTextSize(1);
  display.setCursor(0, 52);
  display.print("Status: ");
  display.println(status);
  display.display();
}

// ============================================================
// 8. WIFI CONNECTION
// ============================================================
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.printf("[WiFi] Connecting to %s ", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Connected successfully!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("[WiFi] Connection failed (Timeout)");
  return false;
}

// ============================================================
// 9. CAMERA INITIALIZATION
// ============================================================
bool initializeCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_VGA; // 640x480
  config.jpeg_quality = 12;
  config.fb_count     = psramFound() ? 2 : 1;
  config.grab_mode    = CAMERA_GRAB_LATEST;
  config.fb_location  = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

  esp_err_t result = esp_camera_init(&config);
  if (result != ESP_OK) {
    Serial.printf("[Camera] Init failed with error 0x%x\n", result);
    return false;
  }
  Serial.println("[Camera] Initialized successfully!");
  return true;
}

// ============================================================
// 10. BACKEND IMAGE UPLOAD
// ============================================================
bool uploadImage(float grams) {
  if (!connectWiFi()) {
    Serial.println("[Upload] Skipped: No Wi-Fi");
    return false;
  }
  if (!cameraReady) {
    Serial.println("[Upload] Skipped: Camera unavailable");
    return false;
  }

  Serial.println("[Camera] Capturing frame for upload...");
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[Camera] Capture failed!");
    return false;
  }

  Serial.printf("[Camera] Frame captured: %u bytes\n", (unsigned int)fb->len);

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, BACKEND_URL)) {
    Serial.println("[Backend] HTTP begin failed");
    esp_camera_fb_return(fb);
    return false;
  }

  http.setTimeout(25000);
  http.addHeader("Content-Type", "image/jpeg");

  String measurementID = String((uint32_t)millis(), HEX) + "-" + String((uint32_t)esp_random(), HEX);
  http.addHeader("X-Measurement-ID", measurementID);
  http.addHeader("X-Weight-Grams", String(grams, 2));

  Serial.printf("[Backend] POSTing to %s ...\n", BACKEND_URL);
  int response = http.POST(fb->buf, fb->len);

  bool success = false;
  if (response >= 200 && response < 300) {
    Serial.printf("[Backend] Upload SUCCESS! Response: %d\n", response);
    Serial.println(http.getString());
    success = true;
  } else {
    Serial.printf("[Backend] Upload FAILED! Response code: %d\n", response);
    if (response > 0) Serial.println(http.getString());
  }

  http.end();
  esp_camera_fb_return(fb);
  return success;
}

// ============================================================
// 11. WEB ENDPOINTS (Capture, Stream, Health, Dashboard)
// ============================================================
void handleCapture() {
  if (!cameraReady) {
    server.send(500, "text/plain", "Camera unavailable");
    return;
  }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Capture failed");
    return;
  }
  WiFiClient client = server.client();
  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handleHealth() {
  String response = "{";
  response += "\"status\":\"online\",";
  response += "\"camera\":" + String(cameraReady ? "true" : "false") + ",";
  response += "\"scale\":" + String(scaleReady ? "true" : "false") + ",";
  response += "\"weight\":" + String(currentWeight, 2);
  response += "}";
  server.send(200, "application/json", response);
}

void handleStream() {
  if (!cameraReady) {
    server.send(500, "text/plain", "Camera unavailable");
    return;
  }
  WiFiClient client = server.client();
  client.print("HTTP/1.1 200 OK\r\n"
               "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
               "Access-Control-Allow-Origin: *\r\n\r\n");

  while (client.connected()) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      delay(10);
      continue;
    }
    client.print("--frame\r\n");
    client.print("Content-Type: image/jpeg\r\n");
    client.printf("Content-Length: %u\r\n\r\n", (unsigned int)fb->len);
    client.write(fb->buf, fb->len);
    client.print("\r\n");
    esp_camera_fb_return(fb);

    // Keep weight processing and serial active even while streaming!
    processWeight();
    processSerialCommands();
    server.handleClient();
    delay(40);
  }
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Weighing System - ESP32-S3</title>
<style>
body{background:#0f172a;color:#f8fafc;font-family:sans-serif;text-align:center;margin:0;padding:20px;}
.card{background:#1e293b;padding:24px;border-radius:16px;max-width:620px;margin:auto;box-shadow:0 8px 24px rgba(0,0,0,0.4);}
h2{margin-top:0;color:#38bdf8;}
.weight{font-size:48px;color:#4ade80;font-weight:bold;margin:12px 0;}
.badge{display:inline-block;padding:4px 12px;border-radius:14px;background:#334155;font-size:14px;}
.stream-box{border-radius:10px;overflow:hidden;background:#000;margin:18px 0;display:inline-block;border:2px solid #334155;}
img{max-width:100%;height:auto;display:block;}
button{padding:10px 18px;margin:6px;border:0;border-radius:8px;background:#2563eb;color:white;cursor:pointer;font-weight:600;}
button:hover{background:#1d4ed8;}
</style>
</head>
<body>
<div class="card">
  <h2>SMART WEIGHING STATION</h2>
  <div class="badge" id="status">Connecting...</div>
  <p style="color:#94a3b8;margin-bottom:4px;">Live Scale Reading</p>
  <div class="weight" id="weight">0.0 g</div>
  <div class="stream-box">
    <img id="stream" src="/stream" alt="Live Camera Stream">
  </div>
  <div>
    <button onclick="snapshot()">Take Snapshot</button>
    <button onclick="tare()">Tare Scale</button>
    <button onclick="refreshStatus()">Refresh</button>
  </div>
</div>
<script>
async function refreshStatus(){
  try{
    let r=await fetch('/health');
    let d=await r.json();
    document.getElementById('weight').innerText=Number(d.weight).toFixed(1)+' g';
    document.getElementById('status').innerText='Scale: '+(d.scale?'READY':'ERROR')+' | Camera: '+(d.camera?'ONLINE':'OFFLINE');
  }catch(e){
    document.getElementById('status').innerText='Connection offline';
  }
}
function snapshot(){
  window.open('/capture','_blank');
}
setInterval(refreshStatus,1500);
refreshStatus();
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void initializeServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/health", HTTP_GET, handleHealth);
  server.on("/stream", HTTP_GET, handleStream);
  server.begin();

  Serial.println("[Server] HTTP Server started successfully!");
  Serial.printf("[Server] Device Dashboard: http://%s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[Server] Video Stream:     http://%s/stream\n", WiFi.localIP().toString().c_str());
  Serial.printf("[Server] Snapshot View:    http://%s/capture\n", WiFi.localIP().toString().c_str());
}

// ============================================================
// 12. WEIGHT PROCESSING & STABILITY LOGIC
// ============================================================
float averageSamples() {
  int n = samplesFilled ? SAMPLE_COUNT : sampleIndex;
  if (n == 0) return 0;
  float sum = 0;
  for (int i = 0; i < n; i++) sum += samples[i];
  return sum / n;
}

bool isWeightStable() {
  if (!samplesFilled) return false;
  float low = samples[0], high = samples[0];
  for (int i = 1; i < SAMPLE_COUNT; i++) {
    if (samples[i] < low)  low  = samples[i];
    if (samples[i] > high) high = samples[i];
  }
  return (high - low) <= STABLE_DELTA_GRAMS;
}

void processWeight() {
  if (!scaleReady) return;
  if (millis() - lastSampleMs < SAMPLE_INTERVAL_MS) return;
  lastSampleMs = millis();

  if (!scale.is_ready()) {
    systemStatus = "HX711 ERROR";
    showDisplay(currentWeight, systemStatus);
    return;
  }

  // 2-sample average to eliminate raw ADC flutter
  float weight = scale.get_units(2);
  if (!isfinite(weight)) return;

  samples[sampleIndex++] = weight;
  if (sampleIndex >= SAMPLE_COUNT) {
    sampleIndex = 0;
    samplesFilled = true;
  }

  float avg = averageSamples();
  currentWeight = avg;
  bool stable = isWeightStable();

  if (millis() - lastPrintMs >= 1000) {
    Serial.printf("[Weight] %.2f grams | %s\n", avg, stable ? "STABLE" : "UNSTABLE");
    lastPrintMs = millis();
  }

  // Empty Platform
  if (fabs(avg) <= EMPTY_THRESHOLD_GRAMS) {
    stableSinceMs = 0;
    captureDone = false;
    systemStatus = "EMPTY";
    showDisplay(0, systemStatus);
    return;
  }

  // Stable Object on Scale
  if (stable) {
    if (stableSinceMs == 0) stableSinceMs = millis();
    systemStatus = "STABLE";
    showDisplay(avg, systemStatus);

    if (!captureDone && millis() - stableSinceMs >= STABLE_DURATION_MS) {
      captureDone = true;
      systemStatus = "CAPTURING";
      Serial.printf("[Event] Weight stable at %.2f g! Uploading snapshot...\n", avg);
      showDisplay(avg, systemStatus);

      bool uploaded = uploadImage(avg);
      systemStatus = uploaded ? "SAVED" : "UPLOAD ERR";
      showDisplay(avg, systemStatus);
    }
  } else {
    stableSinceMs = 0;
    systemStatus = "UNSTABLE";
    showDisplay(avg, systemStatus);
  }
}

// ============================================================
// 13. SERIAL COMMANDS (Tare, Raw, Health, Calibrate)
// ============================================================
void processSerialCommands() {
  if (!Serial.available()) return;
  String command = Serial.readStringUntil('\n');
  command.trim();

  if (command == "t") {
    if (scaleReady) {
      scale.tare(20);
      sampleIndex = 0;
      samplesFilled = false;
      stableSinceMs = 0;
      captureDone = false;
      Serial.println("[TARE] Completed successfully! (Zeroed)");
    }
  } else if (command == "r") {
    if (scaleReady) {
      Serial.print("[Sensor] Raw ADC Value: ");
      Serial.println(scale.read());
    }
  } else if (command == "h") {
    Serial.println("===== SYSTEM HEALTH =====");
    Serial.printf("WiFi:   %s (%s)\n", (WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED"), WiFi.localIP().toString().c_str());
    Serial.printf("Camera: %s\n", (cameraReady ? "READY" : "ERROR"));
    Serial.printf("HX711:  %s\n", (scaleReady ? "READY" : "ERROR"));
    Serial.printf("Weight: %.2f g\n", currentWeight);
    Serial.printf("Calibration Factor: %.2f\n", CALIBRATION_FACTOR);
    Serial.println("=========================");
  } else if (command.startsWith("c ")) {
    float knownWeight = command.substring(2).toFloat();
    if (scaleReady && knownWeight > 0) {
      float raw = scale.get_value(20);
      if (fabs(raw) > 1) {
        CALIBRATION_FACTOR = raw / knownWeight;
        scale.set_scale(CALIBRATION_FACTOR);
        Serial.printf("[CALIBRATION] New factor set: %.4f\n", CALIBRATION_FACTOR);
        Serial.println("Save this value in 'float CALIBRATION_FACTOR' in code!");
      } else {
        Serial.println("[CALIBRATION] Invalid reading. Check sensor connection.");
      }
    } else {
      Serial.println("[CALIBRATION] Usage: c <known_grams> (e.g., 'c 500')");
    }
  }
}

// ============================================================
// 14. SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n\n====================================");
  Serial.println(" SMART WEIGHING SYSTEM");
  Serial.println(" ESP32-S3 SINGLE BOARD ALL-IN-ONE");
  Serial.println("====================================");

  // 1. OLED
  Serial.printf("[1/4] Setting I2C on SDA=GPIO%d, SCL=GPIO%d ...\n", OLED_SDA, OLED_SCL);
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setTimeOut(1000);
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    oledAvailable = true;
    showDisplay(0, "STARTING");
    Serial.println("[OLED] Display ready (0x3C)");
  } else {
    oledAvailable = false;
    Serial.println("[OLED] Display not detected (Check wiring)");
  }

  // 2. HX711
  Serial.printf("[2/4] Setting HX711 on DOUT=GPIO%d, SCK=GPIO%d ...\n", HX711_DOUT, HX711_SCK);
  scale.begin(HX711_DOUT, HX711_SCK);
  delay(300);
  if (scale.wait_ready_timeout(3000)) {
    scaleReady = true;
    scale.set_scale(CALIBRATION_FACTOR);
    scale.tare(20);
    Serial.println("[HX711] Ready! Tare completed.");
  } else {
    scaleReady = false;
    Serial.println("[HX711] WARNING: Sensor not detected! Check DOUT(47) & SCK(48).");
  }

  // 3. Camera
  Serial.println("[3/4] Initializing Camera Module...");
  cameraReady = initializeCamera();
  if (!cameraReady) {
    Serial.println("[Camera] WARNING: Camera initialization failed!");
  }

  // 4. Wi-Fi & Web Server
  Serial.println("[4/4] Connecting to Wi-Fi...");
  if (connectWiFi()) {
    cameraIP = WiFi.localIP().toString();
    initializeServer();
  } else {
    Serial.println("[WiFi] Offline mode. Web server disabled.");
  }

  Serial.println("\n====================================");
  Serial.println(" >>> INITIALIZATION COMPLETED <<<");
  Serial.println("====================================");
  Serial.println("Available Serial Commands:");
  Serial.println("  t       -> Tare (zero the scale)");
  Serial.println("  r       -> Print raw ADC value");
  Serial.println("  h       -> Show system health");
  Serial.println("  c <wt>  -> Calibrate (e.g., 'c 500')\n");
}

// ============================================================
// 15. MAIN LOOP
// ============================================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnect = 0;
    if (millis() - lastReconnect > 10000) {
      connectWiFi();
      lastReconnect = millis();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  processSerialCommands();
  processWeight();
}
