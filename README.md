# Automated IoT Weight Machine & Visual Capture Station

An all-in-one industrial IoT weighing station powered by a single **ESP32-S3-WROOM-CAM (N16R8)**. Features integrated load-cell weight measurement (HX711), automated weight stability detection, visual snapshot capture upon stabilization, 0.96" I2C OLED live display, captive-portal Wi-Fi provisioning via **SmartProv**, and a responsive local Node.js web dashboard with Supabase cloud database/storage synchronization.

---

## Key Features

1. **Captive-Portal Wi-Fi Provisioning (SmartProv Integration):**
   - No hardcoded Wi-Fi credentials in source code.
   - Automatically launches an Access Point (`SmartScale-XXXX`) and captive portal (`192.168.4.1`) when unconfigured or connection fails.
   - Live IP and AP status displayed on the OLED screen.
2. **Automated Weighing & Stability Detection:**
   - 24-bit ADC high-precision load cell sensing via HX711 with deadband smoothing.
   - Detects stable load within configurable tolerance ($\pm 3.5\text{g}$) held for 3 seconds.
   - Automatic hardware zero-point tare offset saved to non-volatile flash (NVS).
3. **Automated Visual Capture:**
   - Automatically captures high-resolution camera snapshot when a stable object weight is detected.
   - Asynchronous snapshot handling prevents HTTP server stack overflows.
4. **Local Web Dashboard & Streaming:**
   - Full-width industrial dashboard with live scale reading, real-time tare, and manual photo capture triggers.
   - Dual-mode camera streaming: direct MJPEG stream with backend stream proxy fallback.
5. **Cloud Synchronization (Supabase):**
   - Automatically uploads captured images to Supabase Storage (`captures` bucket).
   - Inserts measurement records (timestamp, weight in grams, weight in kg, snapshot URL, trigger type) into Supabase PostgreSQL.

---

## Hardware Configuration (ESP32-S3-WROOM-CAM)

### 1. 0.96" I2C OLED Display (SSD1306, 128x64)
- **VCC** -> 3.3V
- **GND** -> GND
- **SDA** -> `GPIO 1`
- **SCL** -> `GPIO 2` (I2C Clock)

### 2. HX711 Load Cell Amplifier
- **VCC** -> 3.3V (or 5V)
- **GND** -> GND
- **DOUT / DT** -> `GPIO 47`
- **SCK** -> `GPIO 48`
- Connect 4-wire load cell to `E+`, `E-`, `A+`, `A-` terminals.

### 3. Integrated OV2640 / OV5640 Camera
Standard ESP32-S3 camera pin mapping:
- **XCLK:** `GPIO 15`, **PCLK:** `GPIO 13`, **VSYNC:** `GPIO 6`, **HREF:** `GPIO 7`
- **Data (D0-D7):** `GPIO 11, 9, 8, 10, 12, 18, 17, 16`
- **I2C SCCB:** **SIOD:** `GPIO 4`, **SIOC:** `GPIO 5`

---

## Wi-Fi Setup via Captive Portal (SmartProv)

1. Power on the device. If Wi-Fi is not yet configured, the OLED display will show:
   ```text
   WIFI SETUP MODE
   Connect to Hotspot:
   SmartScale-XXXX
   IP: 192.168.4.1
   ```
2. On your smartphone or laptop, connect to the Wi-Fi network named **`SmartScale-XXXX`**.
3. Open your browser and navigate to `http://192.168.4.1` (or let the captive portal open automatically).
4. Select your home or office Wi-Fi network from the list, enter the password, and click **Save & Connect**.
5. The device will reboot, connect to your network, and display its assigned local IP address on the OLED.

---

## Firmware Setup (Arduino IDE / CLI)

### Dependencies
Install the following libraries via the Arduino Library Manager:
- `SmartProv` (v3.0.2+)
- `HX711` by bogde
- `Adafruit SSD1306` & `Adafruit GFX Library`
- `esp32` board package by Espressif (v3.x+)

### Board Settings
- **Board:** `ESP32S3 Dev Module`
- **USB CDC On Boot:** `Enabled`
- **Flash Size:** `16MB (128Mb)`
- **Partition Scheme:** `16M Flash (3MB APP/9.9MB FATFS)` or `Default 4MB with spiffs`
- **PSRAM:** `OPI PSRAM`
- **Upload Speed:** `921600`

### Compile & Flash via Arduino CLI
```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3 main-esp32/
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3 main-esp32/
```

---

## Backend & Web Dashboard Setup

The Node.js backend handles local dashboard serving, serial port bridging, and Supabase cloud synchronization.

### 1. Installation
```bash
cd backend
npm install
```

### 2. Environment Configuration
Copy `.env.example` to `.env`:
```bash
cp .env.example .env
```
Update `.env` with your Supabase credentials:
```env
PORT=3000
SUPABASE_URL=https://your-project.supabase.co
SUPABASE_SERVICE_ROLE_KEY=your-service-role-key
SUPABASE_ANON_KEY=your-anon-key
```

### 3. Supabase Database Schema
Create the `measurements` table in your Supabase SQL editor:
```sql
create table measurements (
  id uuid primary key default gen_random_uuid(),
  timestamp timestamptz default now(),
  weight_grams numeric not null,
  weight_kg numeric not null,
  image_url text,
  trigger_type text default 'auto'
);

-- Create storage bucket for snapshots
insert into storage.buckets (id, name, public) 
values ('captures', 'captures', true)
on conflict do nothing;
```

### 4. Run Backend Server
```bash
node server.js
```
Open `http://localhost:3000` in any web browser.

---

## Serial Monitor Commands (115200 Baud)

Connect over USB serial and send any of the following commands:
- `t` : **Tare** (zeroes out current scale reading and saves offset to NVS flash).
- `raw` : **Raw ADC** (prints current raw 24-bit reading from HX711).
- `cap` : **Manual Capture** (triggers an immediate camera snapshot and upload).
- `h` : **Health & Status** (prints Wi-Fi connection, scale status, weight, and calibration factor).
- `c <grams>` : **Calibrate** (e.g. `c 500` with a known 500g weight placed on the load cell).
- `reset` : **Factory Reset** (erases stored Wi-Fi credentials and restarts into SmartProv setup mode).

---

## Authors & Contributors

- **Shahriar Alom Masud** — [GitHub: @Masud744](https://github.com/Masud744) | [LinkedIn](https://www.linkedin.com/in/shahriar-alom-masud)
- **Hasan** — [GitHub: @Hasan-IRE](https://github.com/Hasan-IRE)
