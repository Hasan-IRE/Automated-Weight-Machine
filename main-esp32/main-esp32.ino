#define SP_AP_PREFIX "SmartScale"
#define SP_RESET_PIN 0
#define SP_LED_PIN   255
#include <SmartProv.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <HX711.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include <math.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
const char* SUPABASE_URL  = "https://your-project.supabase.co";
const char* SUPABASE_KEY  = "your-supabase-key";
#endif
String backendUrl         = "http://10.0.21.218:3000/api/upload-image";

SmartProv prov;

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

#define OLED_SDA 1
#define OLED_SCL 2
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3C

#define HX711_DOUT 47
#define HX711_SCK  48

#define SAMPLE_INTERVAL_MS    200
#define STABLE_DURATION_MS    3000
#define STABLE_DELTA_GRAMS    3.5f
#define EMPTY_THRESHOLD_GRAMS 8.0f
#define DEADBAND_GRAMS        0.35f
#define SAMPLE_COUNT          8

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
HX711 scale;
Preferences prefs;
httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

bool oledAvailable = false;
bool scaleReady = false;
bool cameraReady = false;
float calibrationFactor = 420.0f;

float samples[SAMPLE_COUNT] = {0};
int sampleIndex = 0;
bool samplesFilled = false;
float smoothedWeight = 0.0f;
bool smoothedInit = false;

unsigned long lastSampleMs = 0;
unsigned long stableSinceMs = 0;
unsigned long lastPrintMs = 0;
bool captureDone = false;
volatile bool manualTriggerRequested = false;
float currentWeight = 0;
String systemStatus = "STARTING";

void updateDisplay(float grams, const String& status) {
  if (!oledAvailable) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("SMART SCALE");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 18);
  display.print(grams, 1);
  display.println(" g");

  display.setTextSize(1);
  display.setCursor(0, 38);
  display.print("IP: ");
  display.println(prov.isConnected() ? prov.getIP() : "Offline");

  display.setCursor(0, 50);
  display.print("Status: ");
  display.println(status);
  display.display();
}

void showSetupScreen(const String& apName) {
  if (!oledAvailable) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("WIFI SETUP MODE");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(0, 14);
  display.println("Connect to Hotspot:");
  display.setCursor(0, 26);
  display.println(apName);

  display.setCursor(0, 40);
  display.println("IP: 192.168.4.1");

  display.setCursor(0, 52);
  display.println("Configure in browser");
  display.display();
}

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
  config.frame_size   = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count     = 2;
  config.grab_mode    = CAMERA_GRAB_LATEST;
  config.fb_location  = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

  esp_err_t res = esp_camera_init(&config);
  return (res == ESP_OK);
}

bool uploadToSupabase(const String& id, float grams, camera_fb_t* fb, const char* triggerType = "auto") {
  Serial.printf("[Supabase] Starting upload. Heap: %u, PSRAM: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15);

  HTTPClient http;
  String storageUrl = String(SUPABASE_URL) + "/storage/v1/object/weight-images/" + id + ".jpg";
  if (!http.begin(client, storageUrl)) {
    Serial.println("[Supabase] Storage HTTP begin failed");
    return false;
  }

  http.setTimeout(15000);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "image/jpeg");

  int code = http.POST(fb->buf, fb->len);
  if (code < 200 || code >= 300) {
    char errBuf[128] = {0};
    client.lastError(errBuf, sizeof(errBuf));
    Serial.printf("[Supabase] Storage upload failed: %d (%s) SSL: %s\n", code, http.errorToString(code).c_str(), errBuf);
    http.end();
    return false;
  }
  http.end();
  Serial.println("[Supabase] Image uploaded to Storage!");

  String publicUrl = String(SUPABASE_URL) + "/storage/v1/object/public/weight-images/" + id + ".jpg";
  String dbUrl = String(SUPABASE_URL) + "/rest/v1/measurements";
  if (!http.begin(client, dbUrl)) return false;

  http.setTimeout(8000);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=representation");

  char json[320];
  snprintf(json, sizeof(json),
           "{\"id\":\"%s\",\"weight_grams\":%.2f,\"weight_kg\":%.4f,\"image_url\":\"%s\",\"stability_status\":\"stable\",\"capture_status\":\"saved\",\"device_id\":\"smart-scale-01\",\"trigger_type\":\"%s\"}",
           id.c_str(), grams, grams / 1000.0f, publicUrl.c_str(), triggerType);

  int dbCode = http.POST(json);
  http.end();

  if (dbCode >= 200 && dbCode < 300) {
    Serial.printf("[Supabase] DB synced (%s): ID %s, %.2f g\n", triggerType, id.c_str(), grams);
    return true;
  }

  Serial.printf("[Supabase] DB insert failed: %d\n", dbCode);
  return false;
}

bool uploadImage(float grams, const char* triggerType = "auto") {
  if (WiFi.status() != WL_CONNECTED && !prov.isConnected()) return false;
  if (!cameraReady) return false;

  Serial.printf("[Upload] Capturing snapshot (%s) for %.2f g...\n", triggerType, grams);
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[Upload] Failed to grab frame");
    return false;
  }

  String id = String((uint32_t)millis(), HEX) + "-" + String((uint32_t)esp_random(), HEX);
  bool cloudOk = uploadToSupabase(id, grams, fb, triggerType);

  if (backendUrl.length() > 5) {
    WiFiClient client;
    HTTPClient http;
    if (http.begin(client, backendUrl)) {
      http.setTimeout(2000);
      http.addHeader("Content-Type", "image/jpeg");
      http.addHeader("X-Measurement-ID", id);
      http.addHeader("X-Weight-Grams", String(grams, 2));
      http.addHeader("X-Device-ID", "smart-scale-01");
      http.addHeader("X-Trigger-Type", triggerType);
      http.POST(fb->buf, fb->len);
      http.end();
    }
  }

  esp_camera_fb_return(fb);
  return cloudOk;
}

static esp_err_t capture_handler(httpd_req_t *req) {
  if (!cameraReady) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  if (!cameraReady) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char part_buf[64];
  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    esp_err_t res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) {
      size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    }
    esp_camera_fb_return(fb);

    if (res != ESP_OK) break;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return ESP_OK;
}

static esp_err_t options_handler(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
  httpd_resp_set_status(req, "204 No Content");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t health_handler(httpd_req_t *req) {
  char resp[128];
  snprintf(resp, sizeof(resp),
           "{\"status\":\"online\",\"camera\":%s,\"scale\":%s,\"weight\":%.2f,\"calibration\":%.2f}",
           cameraReady ? "true" : "false",
           scaleReady ? "true" : "false",
           currentWeight,
           calibrationFactor);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, resp, strlen(resp));
}

static esp_err_t tare_handler(httpd_req_t *req) {
  if (scaleReady) {
    scale.tare(20);
    prefs.putLong("tare_offset", scale.get_offset());
    sampleIndex = 0;
    samplesFilled = false;
    stableSinceMs = 0;
    captureDone = false;
    smoothedWeight = 0.0f;
    smoothedInit = false;
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
  httpd_resp_set_type(req, "application/json");
  const char* resp = "{\"ok\":true,\"tare\":true}";
  return httpd_resp_send(req, resp, strlen(resp));
}

static esp_err_t calibrate_handler(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
  char query[32];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char val[16];
    if (httpd_query_key_value(query, "grams", val, sizeof(val)) == ESP_OK) {
      float known = atof(val);
      if (known > 0 && scaleReady) {
        float raw = scale.get_value(20);
        if (fabs(raw) > 1.0f) {
          calibrationFactor = raw / known;
          scale.set_scale(calibrationFactor);
          prefs.putFloat("cal_factor", calibrationFactor);

          char resp[64];
          snprintf(resp, sizeof(resp), "{\"ok\":true,\"calibrationFactor\":%.4f}", calibrationFactor);
          httpd_resp_set_type(req, "application/json");
          return httpd_resp_send(req, resp, strlen(resp));
        }
      }
    }
  }
  const char* err = "{\"error\":\"Pass ?grams=weight\"}";
  httpd_resp_set_status(req, "400 Bad Request");
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, err, strlen(err));
}

static esp_err_t trigger_handler(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
  httpd_resp_set_type(req, "application/json");

  manualTriggerRequested = true;
  float wt = currentWeight;
  char resp[96];
  snprintf(resp, sizeof(resp), "{\"ok\":true,\"weight\":%.2f,\"triggerType\":\"manual\"}", wt);
  return httpd_resp_send(req, resp, strlen(resp));
}

static esp_err_t index_handler(httpd_req_t *req) {
  const char* html = "<!DOCTYPE html><html><body style='background:#0f172a;color:#fff;font-family:sans-serif;text-align:center;padding:40px;'>"
                     "<h2>ESP32-S3 Scale Station</h2>"
                     "<p><a style='color:#38bdf8;' href=':81/stream'>Live Stream (Port 81)</a> | <a style='color:#38bdf8;' href='/capture'>Capture</a> | <a style='color:#38bdf8;' href='/health'>Health</a></p>"
                     "</body></html>";
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, html, strlen(html));
}

void startHttpServers() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;
  config.max_open_sockets = 4;
  config.stack_size = 10240;
  config.lru_purge_enable = true;

  httpd_uri_t indexUri     = { "/",         HTTP_GET, index_handler,     NULL };
  httpd_uri_t captureUri   = { "/capture",   HTTP_GET, capture_handler,   NULL };
  httpd_uri_t healthUri    = { "/health",    HTTP_GET, health_handler,    NULL };
  httpd_uri_t tareUri      = { "/tare",      HTTP_GET, tare_handler,      NULL };
  httpd_uri_t calibrateUri = { "/calibrate", HTTP_GET, calibrate_handler, NULL };
  httpd_uri_t triggerUri   = { "/trigger",   HTTP_GET, trigger_handler,   NULL };
  httpd_uri_t optTrigger   = { "/trigger",   HTTP_OPTIONS, options_handler, NULL };
  httpd_uri_t optTare      = { "/tare",      HTTP_OPTIONS, options_handler, NULL };
  httpd_uri_t optHealth    = { "/health",    HTTP_OPTIONS, options_handler, NULL };

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &indexUri);
    httpd_register_uri_handler(camera_httpd, &captureUri);
    httpd_register_uri_handler(camera_httpd, &healthUri);
    httpd_register_uri_handler(camera_httpd, &tareUri);
    httpd_register_uri_handler(camera_httpd, &calibrateUri);
    httpd_register_uri_handler(camera_httpd, &triggerUri);
    httpd_register_uri_handler(camera_httpd, &optTrigger);
    httpd_register_uri_handler(camera_httpd, &optTare);
    httpd_register_uri_handler(camera_httpd, &optHealth);
    Serial.println("Control server started on port 80");
  }

  config.server_port = 81;
  config.ctrl_port = 32769;
  config.max_open_sockets = 2;
  httpd_uri_t streamUri = { "/stream", HTTP_GET, stream_handler, NULL };

  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &streamUri);
    Serial.println("Dedicated stream server started on port 81");
  }
}

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
    systemStatus = "HX711 ERR";
    updateDisplay(currentWeight, systemStatus);
    return;
  }

  float reading = scale.get_units(2);
  if (!isfinite(reading)) return;

  if (!smoothedInit) {
    smoothedWeight = reading;
    smoothedInit = true;
  } else {
    float diff = fabs(reading - smoothedWeight);
    if (diff > DEADBAND_GRAMS) {
      smoothedWeight = 0.65f * reading + 0.35f * smoothedWeight;
    } else {
      smoothedWeight = 0.15f * reading + 0.85f * smoothedWeight;
    }
  }

  samples[sampleIndex++] = smoothedWeight;
  if (sampleIndex >= SAMPLE_COUNT) {
    sampleIndex = 0;
    samplesFilled = true;
  }

  float avg = averageSamples();
  currentWeight = avg;
  bool stable = isWeightStable();

  if (millis() - lastPrintMs >= 1000) {
    Serial.printf("[Weight] %.2f g | %s\n", avg, stable ? "STABLE" : "UNSTABLE");
    lastPrintMs = millis();
  }

  if (fabs(avg) <= EMPTY_THRESHOLD_GRAMS) {
    stableSinceMs = 0;
    captureDone = false;
    systemStatus = "EMPTY";
    updateDisplay(avg, systemStatus);
    return;
  }

  if (stable) {
    if (stableSinceMs == 0) stableSinceMs = millis();
    systemStatus = "STABLE";
    updateDisplay(avg, systemStatus);

    if (!captureDone && millis() - stableSinceMs >= STABLE_DURATION_MS) {
      captureDone = true;
      systemStatus = "CAPTURING";
      updateDisplay(avg, systemStatus);

      bool ok = uploadImage(avg, "auto");
      systemStatus = ok ? "SAVED" : "UPLOAD ERR";
      updateDisplay(avg, systemStatus);
    }
  } else {
    stableSinceMs = 0;
    systemStatus = "UNSTABLE";
    updateDisplay(avg, systemStatus);
  }
}

void processSerialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd == "t") {
    if (scaleReady) {
      scale.tare(20);
      prefs.putLong("tare_offset", scale.get_offset());
      sampleIndex = 0;
      samplesFilled = false;
      stableSinceMs = 0;
      captureDone = false;
      smoothedWeight = 0.0f;
      smoothedInit = false;
      Serial.println("Tare complete. Zero point saved.");
    }
  } else if (cmd == "raw") {
    if (scaleReady) {
      Serial.printf("Raw ADC: %ld\n", scale.read());
    }
  } else if (cmd == "cap") {
    uploadImage(currentWeight, "manual");
  } else if (cmd == "h") {
    Serial.println("--- System Status ---");
    Serial.printf("WiFi: %s (%s)\n", prov.isConnected() ? "Connected" : "Offline", prov.getIP().c_str());
    Serial.printf("Scale: %s, Weight: %.2f g\n", scaleReady ? "Ready" : "Offline", currentWeight);
    Serial.printf("Calibration factor: %.4f\n", calibrationFactor);
    Serial.printf("Supabase URL: %s\n", SUPABASE_URL);
  } else if (cmd.startsWith("c ")) {
    float known = cmd.substring(2).toFloat();
    if (scaleReady && known > 0) {
      float raw = scale.get_value(20);
      if (fabs(raw) > 1.0f) {
        calibrationFactor = raw / known;
        scale.set_scale(calibrationFactor);
        prefs.putFloat("cal_factor", calibrationFactor);
        Serial.printf("Calibrated. Factor: %.4f (saved to NVS)\n", calibrationFactor);
      } else {
        Serial.println("Calibration failed: sensor output too low.");
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1200);

  prefs.begin("weight_scale", false);
  calibrationFactor = prefs.getFloat("cal_factor", 420.0f);
  backendUrl = prefs.getString("backend_url", "http://10.0.21.218:3000/api/upload-image");

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setTimeOut(1000);
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    oledAvailable = true;
    updateDisplay(0, "STARTING");
  }

  scale.begin(HX711_DOUT, HX711_SCK);
  delay(250);
  if (scale.wait_ready_timeout(3000)) {
    scaleReady = true;
    scale.set_scale(calibrationFactor);
    long savedOffset = prefs.getLong("tare_offset", 0);
    if (savedOffset != 0) {
      scale.set_offset(savedOffset);
    } else {
      scale.tare(20);
    }
    Serial.println("HX711 initialized.");
  } else {
    Serial.println("HX711 not detected. Check DOUT/SCK pins.");
  }

  cameraReady = initializeCamera();
  if (!cameraReady) {
    Serial.println("Camera init failed.");
  }

  prov.onSetupMode([]() {
    Serial.printf("[SmartProv] Setup mode active. AP: %s\n", prov.getAPName().c_str());
    showSetupScreen(prov.getAPName());
  });

  prov.onConnected([]() {
    Serial.printf("[SmartProv] Connected! Network: %s | IP: %s\n", prov.getSSID().c_str(), prov.getIP().c_str());
    if (!camera_httpd) {
      startHttpServers();
    }
    updateDisplay(0, "READY");
  });

  prov.enableMDNS("smartscale");
  prov.begin(0, 255);
}

void loop() {
  prov.update();

  if (prov.isSetupMode()) {
    delay(10);
    return;
  }

  if (manualTriggerRequested) {
    manualTriggerRequested = false;
    systemStatus = "CAPTURING";
    updateDisplay(currentWeight, systemStatus);
    bool ok = uploadImage(currentWeight, "manual");
    systemStatus = ok ? "SAVED" : "UPLOAD ERR";
    updateDisplay(currentWeight, systemStatus);
  }

  processSerialCommands();
  processWeight();
}
