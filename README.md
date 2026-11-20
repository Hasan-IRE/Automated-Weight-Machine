# Smart Weighing System (Single-Board ESP32-S3-CAM)

Complete All-in-One IoT Smart Weighing Scale with integrated camera snapshot, live streaming, OLED display, and local Node.js web dashboard.

---

## Hardware Configuration (Single Board: ESP32-S3-WROOM-CAM)

### 1. OLED Display (0.96" I2C 128x64)
- **VCC** -> 3.3V
- **GND** -> GND
- **SDA** -> `GPIO 1`
- **SCL** -> `GPIO 2`

### 2. HX711 Weight Sensor Module
- **VCC** -> 3.3V (or 5V)
- **GND** -> GND
- **DOUT / DT** -> `GPIO 47`
- **SCK** -> `GPIO 48`
- Connect 4-wire load cell to E+, E-, A+, A- terminals.

### 3. Integrated Camera (ESP32-S3-WROOM-CAM)
- Uses default generic ESP32-S3 camera pin mapping (XCLK 15, SIOD 4, SIOC 5, etc.)

---

## Arduino IDE Board Settings

- **Board:** `ESP32S3 Dev Module`
- **USB CDC On Boot:** `Enabled`
- **PSRAM:** `OPI PSRAM`
- **Upload Speed:** `921600` or `115200`

---

## Endpoints & URLs

- **Local PC Dashboard:** `http://localhost:3000` (or `http://172.16.114.152:3000`)
- **ESP32 Device Dashboard:** `http://<ESP32_IP>/`
- **ESP32 Live Video Stream:** `http://<ESP32_IP>/stream`
- **ESP32 Snapshot:** `http://<ESP32_IP>/capture`
- **ESP32 JSON Health:** `http://<ESP32_IP>/health`

---

## Serial Monitor Commands (115200 baud)

Type any of the following into the Serial Monitor input and press Enter:
- `t` : **Tare** (zeroes out the current weight on the scale)
- `r` : **Raw** (prints raw 24-bit ADC value from the load cell)
- `h` : **Health** (shows Wi-Fi, camera, sensor, and current weight status)
- `c <grams>` : **Calibrate** (e.g. `c 500` when placing a known 500g weight)
