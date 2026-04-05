# AirMQ SensorNode — Firmware Documentation

## Table of Contents

1. [Overview](#overview)
2. [Hardware](#hardware)
3. [First-Time Setup](#first-time-setup)
4. [Web Interface](#web-interface)
5. [Supported Sensors](#supported-sensors)
6. [MQTT Integration](#mqtt-integration)
7. [LED Indicator](#led-indicator)
8. [OTA Firmware Updates](#ota-firmware-updates)
9. [Feature Flags](#feature-flags)
10. [Advanced Configuration](#advanced-configuration)
11. [TODO List] (#todo-list)

---

## Overview

AirMQ SensorNode is open-source firmware for Espressif ESP8266 and ESP32 microcontroller families. It reads data from a variety of local sensors and publishes batched readings to an MQTT broker. Configuration is done through a built-in web interface accessible over WiFi.

**Key capabilities:**
- Supports 11 sensor types (temperature, humidity, pressure, CO₂, VOC, particulates, 1-Wire)
- Batches readings and publishes as JSON arrays to the AirMQ backend via MQTT
- Built-in web dashboard for all configuration and OTA firmware updates
- Real-time log stream via WebSocket
- Captive portal for first-time WiFi setup
- Runtime feature configuration and device control 

---

## Hardware

### Supported MCUs

| MCU | Flash | RAM | Notes |
|-----|-------|-----|-------|
| ESP8266 (AirMQ board or any development board) | 4 MB | ~80 KB heap | Cooperative multitasking, tight memory |
| ESP32-C3 | 4 MB | ~300 KB heap | FreeRTOS tasks, USB-CDC serial |
Other Espressif MCUs and devboards may be easily added 

### Default Pin Assignments

Pins can be changed at runtime in **Hardware Setup**. These are the compiled-in defaults.

**ESP8266 ([AirMQ v0.x](https://github.com/AirMQ/AirMQ-sensor/tree/master/Hardware/Mainboard), D1 Mini, etc.)**

| Function | GPIO | D-pin |
|----------|------|-------|
| I2C SDA | 4 | D2 |
| I2C SCL | 5 | D1 |
| OneWire (DS18B20) | — | not set |
| UART RX (PMS7003) | 3 | RX |
| UART TX (PMS7003) | 1 | TX |
| WS2812B LED | — | not set |
| 5 V boost enable | 15 | D8 |

**ESP32-C3 ([AirMQ v2.x](https://github.com/AirMQ/AirMQ-sensor/tree/master/Hardware/Mainboard/2.0.2))**

| Function | GPIO |
|----------|------|
| I2C SDA | 6 |
| I2C SCL | 7 |
| OneWire (DS18B20) | — |
| UART RX (PMS7003) | 20 |
| UART TX (PMS7003) | 21 |
| WS2812B LED | 5 |
| 5 V boost enable | 9 |

---

## First-Time Setup

### Step 1 — Power on

On first boot (no WiFi credentials stored) the device starts in **AP mode** immediately.

### Step 2 — Connect to the config AP

| | |
|--|--|
| **SSID** | `AirMQ-SN-<chipId>` (e.g. `AirMQ-SN-3812940`) |
| **Password** | `configesp` |

Your device should redirect you to the config page automatically (captive portal). If not, open **http://192.168.4.1** in a browser.

The AP stays open for **3 minutes**. After that the device switches to STA-only mode to attempt a stored connection. To reopen the AP, send the `wifiOn` MQTT command or power-cycle the board.

### Step 3 — Configure WiFi

1. Open the **WiFi Setup** section.
2. Enter your network SSID and password. Optionally add a backup network.
3. Use the **Scan** button to pick from discovered networks.
4. Click **Save** then **Reboot** (Commands section).

The device will connect and report its IP address on the serial monitor.

### Step 4 — Configure MQTT

1. Open the **MQTT Config** section.
2. Set **Broker** to your MQTT server hostname or IP.
3. Set **Port** (default: `18883`, plain MQTT — no TLS).
4. Adjust **Topic Prefix** if needed (default: `Sensors/`).
5. Click **Save** then **Reboot**.

After reboot the device connects to MQTT and publishes a startup message to `Sensors/<chipId>/Start`.

### Step 5 — Configure sensors

1. Open the **Sensor Setup** section.
2. Enable the sensor types you have connected.
3. Adjust I2C addresses if your module uses a non-default address.
4. Click **Save** (takes effect after reboot).

### Step 6 — Configure hardware pins (if needed)

The defaults work for standard D1 Mini wiring. If your board is different, open **Hardware Setup** and adjust I2C, UART, OneWire, and LED pin numbers. Use `-1` to disable a pin.

Reboot once after any hardware pin change.

---

## Web Interface

Open `http://<device-ip>/` in a browser. The dashboard auto-refreshes every 15 seconds.

Sections can be **dragged** to reorder and **resized** (drag the `⣿` grip at the right edge of each card). Layout is saved in browser local storage.

### Status

Live device state — read-only.

| Field | Description |
|-------|-------------|
| Chip ID | Unique ID derived from MAC address |
| MCU | ESP8266 or ESP32-C3 |
| Flash | Total flash size |
| IP | Current IP address |
| WiFi | Connection state |
| MQTT | Broker connection state |
| RSSI | WiFi signal strength |
| Uptime | Minutes since last boot |
| Active Sensors | Number of enabled and responding sensors |
| Readings | Total readings published since boot |
| Free Heap | Available RAM (critical on ESP8266; watch for < 16 KB) |
| Sample Num | How many readings are batched per MQTT publish |
| Tele Interval | How often telemetry is sent (minutes) |
| Build | Firmware build tag |
| Time / NTP | Current time and NTP sync status |

### WiFi Setup

Configure STA (station) credentials. Two networks can be stored — the device tries the primary first, then the backup.

The **Scan** button performs a live scan and populates a clickable list of nearby networks. Click an SSID to populate the primary fields.

### MQTT Config

| Field | Default | Notes |
|-------|---------|-------|
| Broker | `mq.airmq.cc` | Hostname or IP |
| Port | `18883` | Plain MQTT |
| Topic Prefix | `Sensors/` | Prepended to all topics |
| TLS / MQTTS | off | Enable for port 8883 |

### Hardware Setup

Adjust GPIO assignments. Set to `-1` to disable a function.

| Field | Default (ESP8266 / ESP32-C3) | Notes |
|-------|------------------------------|-------|
| I2C SDA | 4 / 6 | Shared by all I2C sensors |
| I2C SCL | 5 / 7 | |
| UART RX | 3 / 20 | PMS7003 data |
| UART TX | 1 / 21 | PMS7003 (unused, reserved) |
| 1-Wire pin | -1 | DS18B20 data |
| LED pin | -1 / 5 | WS2812B |

> After changing pins, click **Save** and then **Reboot** from the Commands section.

### Sensor Setup

Each enabled sensor type appears as a card. Check the **enable** checkbox to activate it. I2C sensors also expose an address field (hex); leave at `0` to use the sensor's default address.

Sensors with a shared default address (e.g. BME280 and BMP280 both default to `0x76`) cannot be enabled simultaneously without one being moved to its alternate address — the UI will warn you if there is a conflict.

### Commands

| Button | Action |
|--------|--------|
| **Reboot** | Restart the device |
| **Send Telemetry** | Publish a telemetry packet immediately |
| **Debug Log: OFF/ON** | Toggle verbose debug output in the log stream |
| **Factory Reset** | Erase stored config (keep-WiFi and keep-MQTT checkboxes available) |

### Firmware Update

Two methods:

**Web upload** — Pick a `.bin` file and click **Flash & Reboot**. A progress bar shows transfer progress. The device reboots automatically on success.

**OTA server** — Click **Check OTA Server**. The device fetches the version JSON for its environment, compares with the running build, and downloads automatically if a newer version is available.

### Utils

**I2C Scanner** — scans the I2C bus and lists all detected device addresses. Use this to identify the address of an unknown sensor or verify wiring.

### Features

Runtime toggles that persist across reboots (stored in `/features.json`).

| Toggle | Effect when disabled | Recovery |
|--------|---------------------|----------|
| WebSocket Log (port 81) | Stops log broadcast; saves ~3–5 KB heap | Re-enable via this UI or MQTT |
| Web UI (port 80) | Disables HTTP server entirely; saves ~2–4 KB heap | **MQTT only** — `{"feature":"web","value":true}` then reboot |

> **Warning:** disabling the Web UI cannot be undone from the browser. Only do this if MQTT access is reliable.

### Log Stream

Real-time log output streamed over WebSocket from port 81. Up to 200 entries are kept in the browser. On connect, the last 12 (ESP8266) or 50 (ESP32-C3) entries are replayed from the ring buffer.

| Color | Level |
|-------|-------|
| Blue | info |
| Orange | warn |
| Red | error |
| Gray | debug (only visible when Debug Log is ON) |

---

## Supported Sensors

All I2C sensors share the same bus (SDA/SCL pins configured in Hardware Setup). Multiple sensor types can run simultaneously as long as their I2C addresses do not conflict.

| Type | Sensor | Interface | Measurements | Default addr |
|------|--------|-----------|--------------|--------------|
| `bme280` | Bosch BME280 | I2C | Temperature, Humidity, Pressure | `0x76` |
| `bmp280` | Bosch BMP280 | I2C | Temperature, Pressure | `0x76` |
| `bmp580` | Bosch BMP581 | I2C | Temperature, Pressure (high precision) | `0x46` |
| `sht30` | Sensirion SHT30/31 | I2C | Temperature, Humidity | `0x44` |
| `sht4x` | Sensirion SHT40/41 | I2C | Temperature, Humidity | `0x44` |
| `htu21d` | TE HTU21D | I2C | Temperature, Humidity | `0x40` (fixed) |
| `scd4x` | Sensirion SCD40/41 | I2C | CO₂, Temperature, Humidity | `0x62` (fixed) |
| `sgp4x` | Sensirion SGP40/41 | I2C | VOC index | `0x59` (fixed) |
| `ds18b20` | Dallas DS18B20 | 1-Wire | Temperature | — |
| `pms7003` | Plantower PMS7003 | UART | PM1.0, PM2.5, PM10 | — |
| `xdb401` | XDB401 | I2C | Differential pressure, Temperature | `0x7F` (fixed) |

**Notes:**
- **SGP4x** requires a temperature and humidity source for compensation. The firmware automatically uses the first active T/H sensor (BME280, SHT30, SHT4x, HTU21D, or SCD4x).
- **DS18B20** uses the OneWire pin (Hardware Setup). Multiple DS18B20 sensors on the same bus are read as separate readings.
- **PMS7003** uses UART RX/TX pins. It needs 3.3 V logic and a stable 5 V supply.
- **XDB401**: set `fullscale_pa` in the sensor config JSON to match your sensor's rated range for correct calibration.

---

## MQTT Integration

### Topic structure

All topics follow the pattern: `{prefix}{chipId}/{suffix}`

With the default prefix `Sensors/` and a chip ID of `1234567`:

| Topic | Direction | Content |
|-------|-----------|---------|
| `Sensors/1234567/sensordata` | publish | Batched sensor readings (JSON array) |
| `Sensors/1234567/telemetry` | publish | Device telemetry |
| `Sensors/1234567/Start` | publish | Startup announcement |
| `Sensors/1234567/cmd` | subscribe | Configuration commands |

### Sensor data payload

Published as a JSON array. Each element contains `deviceId`, `time`, and the sensor-specific fields merged at the top level:

```json
[
  {"deviceId": 1234567, "time": 1711900800, "temp": 21.5, "hum": 58.2, "press": 1013.4},
  {"deviceId": 1234567, "time": 1711900802, "temp": 21.6, "hum": 58.1, "press": 1013.5}
]
```

The exact fields inside each object depend on the sensor type — for example a DS18B20 produces `{"temp": 21.5}`, a PMS7003 produces `{"pm1": 3, "pm25": 5, "pm10": 6}`.

The array is flushed when it reaches `sampleNum` readings (default: 1) or when a sensor requests immediate transmission.

### Telemetry payload

```json
{
  "deviceId": 1234567,
  "time": 1711900800,
  "uptime": 142,
  "wifiRSSI": -65,
  "freeHeap": 22400,
  "build": "2026040101",
  "teleInterval": 30,
  "sampleNum": 1
}
```

### Commands

Send a JSON object to `Sensors/<chipId>/cmd`. Multiple keys can be combined in one message.

**Operational settings** (persisted to `/hwconfig.json`):

| Key | Type | Description |
|-----|------|-------------|
| `teleIntervalM` | integer | Telemetry and sensor publish interval in minutes |
| `sampleNum` | integer | Readings to batch before publishing (1 = immediate) |
| `onTime` | integer | Minimum active time in ms (≥ 30) |

**Debug:**

| Key | Type | Description |
|-----|------|-------------|
| `debugLog` | boolean | Enable verbose debug messages in log stream |

**Actions:**

```json
{"cmd": "reboot"}       // restart immediately
{"cmd": "telemetry"}    // publish telemetry now
{"cmd": "otaCheck"}     // check OTA server for update
{"cmd": "wifiOn"}       // open AP window (3 min)
{"cmd": "wifiOff"}      // close AP
```

**OTA trigger:**

```json
{"ota": "http://example.com/firmware.bin"}
```

**Feature toggles** (reboot automatically applied):

```json
{"feature": "wsLog", "value": false}
{"feature": "web",   "value": false}
{"feature": "web",   "value": true}
```

---

## LED Indicator

If a WS2812B LED is connected (LED pin ≠ -1), it shows device state:

| Color | Pattern | State |
|-------|---------|-------|
| Purple | Solid | AP mode — no WiFi credentials, waiting for config |
| Blue | Pulsing | Connecting to WiFi |
| Green | Pulsing | WiFi connected, MQTT not yet connected |
| Yellow | Solid (2 min then off) | MQTT connected and operational |
| Red | Solid | Error |
| Off | — | Normal operation (after 2-min MQTT confirmation period) |

---

## OTA Firmware Updates

### Via web upload

1. Build the firmware binary (`pio run -e esp8266` or `esp32c3`).
2. Open the web UI → **Firmware Update** section.
3. Select the `.bin` file and click **Flash & Reboot**.

### Via OTA server

The device checks a version JSON at the URL configured in `platformio.ini`. If the server's build number is higher than the running firmware, it downloads and applies the update automatically.

Trigger a check from:
- Web UI → **Check OTA Server** button
- MQTT → `{"cmd": "otaCheck"}`

### Via MQTT URL

```json
{"ota": "http://your-server.com/path/to/firmware.bin"}
```

> **Note:** on ESP8266, OTA must use HTTP (not HTTPS). BearSSL cannot hold an open WebSocket and a TLS download connection simultaneously.

---

## Feature Flags

Feature flags let you disable optional subsystems to recover heap memory. This is most useful on ESP8266 where heap is tight (~80 KB total).

Flags are stored in `/features.json` and loaded at boot before any subsystem starts.

| Flag | Default | Heap recovered | Notes |
|------|---------|---------------|-------|
| `wsLog` | enabled | ~3–5 KB | Disables WebSocket log server on port 81 |
| `web` | enabled | ~2–4 KB | Disables HTTP server on port 80 |

**Disabling both together frees ~6–8 KB** — the margin needed for reliable MQTT on ESP8266.

Change from web UI (**Features** section → Apply → Reboot) or via MQTT (see [Commands](#commands)).

> If the web UI is disabled, re-enable it only via MQTT. There is no physical button recovery.

---

## Advanced Configuration

### Compile-time constants (`src/config.h`)

| Constant | Default | Description |
|----------|---------|-------------|
| `DEFAULT_SAMPLE_NUM` | 1 | Readings per MQTT publish |
| `DEFAULT_TELE_INTERVAL_M` | 30 | Telemetry interval (minutes) |
| `DEFAULT_SENSOR_INTERVAL_S` | 60 | Sensor polling interval (seconds) |
| `AP_WINDOW_MS` | 180000 | AP mode duration (ms) |
| `WIFI_PRIMARY_TIMEOUT_MS` | 30000 | STA connect timeout per SSID (ms) |
| `LOG_RING_SIZE` | 12 / 50 | Log history buffer (ESP8266 / ESP32-C3) |

### Compile-time settings (`src/settings.h`)

| Constant | Default | Description |
|----------|---------|-------------|
| `AP_PASSWORD` | `configesp` | WPA2 passphrase for config AP |
| `DEFAULT_MQTT_BROKER` | `mq.airmq.cc` | MQTT broker hostname |
| `DEFAULT_MQTT_PORT` | `18883` | MQTT port |
| `DEFAULT_MQTT_PREFIX` | `Sensors/` | Topic prefix |
| `DEFAULT_MQTT_TLS` | `false` | TLS off by default |

All `settings.h` constants can be overridden via `build_flags` in `platformio.ini` without editing the file.

### Stored config files (LittleFS)

| File | Content |
|------|---------|
| `/wifi.json` | SSID + password (primary and backup) |
| `/mqtt.json` | Broker, port, prefix, TLS flag, reconnect interval |
| `/hwconfig.json` | Pins, telemetry interval, sample count, on-time |
| `/sensorsetup.json` | Per-sensor-type enable/disable + I2C address |
| `/features.json` | Runtime feature flags |

Config files survive OTA updates (they are in the data partition, separate from firmware). Factory Reset from the web UI selectively removes them.

### Re-flashing from scratch

If the device is completely unresponsive or the filesystem is corrupt, flash via serial:

```bash
# ESP8266
pio run -t upload -e esp8266

# ESP32-C3
pio run -t upload -e esp32c3

# Flash filesystem (sensor setup, etc.)
pio run -t uploadfs -e esp32c3
```

After a fresh flash the device boots into AP mode as if first-time setup.

## TODO List

- Additional sensors
- Integration with jther services/APIs  
- Sleep mode for solar-powered devices
