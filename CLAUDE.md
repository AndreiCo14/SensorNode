# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## Project Overview

**SensorNode** is ESP8266/ESP32-C3 firmware for an AirMQ sensor node. It reads local sensors (BME280, SHT30, SHT4x, SCD4x, DS18B20, HTU21D, BMP280/581, SGP41, PMS7003, XDB401 pressure …) and publishes batched readings to an MQTT broker. Configuration and OTA updates are served via a built-in web UI.

## Build System

PlatformIO. Two primary environments:

| Environment | MCU | Notes |
|---|---|---|
| `esp8266` | ESP8266 (D1 Mini) | Cooperative multitasking, tight heap (~80 KB) |
| `esp32c3` | ESP32-C3 | FreeRTOS tasks, ample heap (~300 KB) |

```bash
pio run -e esp8266
pio run -e esp32c3
pio run -t upload -e esp8266
pio run -t monitor -e esp8266   # 115200 baud
```

There are no automated tests — validation is done via serial monitor, web UI, and MQTT inspection.

## Architecture

### Task Model

**ESP32-C3** — four FreeRTOS tasks pinned to specific cores:

| Task | Core | Priority | Source | Role |
|------|------|----------|--------|------|
| `loggerTask` | 0 | 1 | `logger.cpp` | Drain logQueue, broadcast via WebSocket (port 81) |
| `sensorTask` | 0 | 4 | `sensors/sensor_manager.cpp` | Poll all enabled sensors, push to sensorQueue |
| `uplinkTask` | 1 | 3 | `uplink.cpp` | WiFi/MQTT, batch & publish sensor data, handle commands |
| `webTask` | 1 | 2 | `webserver.cpp` | HTTP API + web UI (port 80), OTA upload |

**ESP8266** — cooperative single-threaded loop:
```cpp
void loop() { ledUpdate(); loggerProcess(); webProcess(); uplinkProcess(); sensorProcess(); }
```
Each subsystem exposes a `*Init()` called in `setup()` and a `*Process()` called each loop iteration.

### IPC (defined in `src/queues.h`)
- `sensorQueue` (10 entries): `SensorReading` → uplinkTask
- `cmdQueue` (5 entries): `MqttCommand` → uplinkTask
- `logQueue` (16 entries): `LogEntry` → loggerTask

`MqttCommand.payload` is 256 B on ESP8266, 512 B on ESP32-C3.

### MQTT Data Flow
uplinkTask publishes to `<prefix><chipId>/`:
- `sensordat` — batched sensor readings (JSON array, flushes at `sampleNum` or on `immTX`)
- `telemet` — telemetry (uptime, RSSI, VCC, build, config)
- `Start` — startup announcement

Subscribes to `<prefix><chipId>/cmd` for configuration commands.

## Key Configuration Files

| File | Purpose |
|------|---------|
| `src/config.h` | Compile-time constants: queue sizes, ring buffer sizes, task stacks/priorities, default params |
| `src/settings.h` | Default WiFi SSID/password, MQTT broker URL, `FW_BUILD` fallback |
| `platformio.ini` | PlatformIO environments, dependencies, build flags |
| `src/boards/` | Per-board pin defaults (`board.h` selects one at compile time) |

Runtime config is persisted to LittleFS:

| File | Content |
|------|---------|
| `/wifi.json` | SSID / password (primary + backup) |
| `/mqtt.json` | Broker, port, prefix, TLS flag |
| `/hwconfig.json` | Pin assignments, sensor interval, telemetry interval, sample count |
| `/sensorsetup.json` | Per-sensor-type enable/disable + I2C address |
| `/features.json` | Runtime feature flags (see `docs/features.md`) |

## ESP8266 Memory Notes

ESP8266 has ~80 KB heap; at MQTT connect time free heap is typically ~16 KB — barely enough for `espMqttClient`'s internal buffers. Key mitigations:

- `LOG_RING_SIZE` is **12** on ESP8266 (vs 50 on ESP32-C3) — saves ~6.4 KB BSS
- `MqttCommand.payload` is **256 B** on ESP8266 (vs 512 B) — saves ~1.3 KB queue
- `-DEMC_MIN_FREE_MEMORY=8192` in `platformio.ini` — lowers espMqttClient's subscribe threshold
- Feature flags allow disabling the web server and WebSocket log at runtime to free ~6–8 KB heap (see `docs/features.md`)
- OTA is forced to HTTP (not HTTPS) on ESP8266 to avoid BearSSL memory pressure
- `loggerProcess()` must **not** be called inside the OTA progress callback — causes ~80% OTA failure rate

## Web UI

`data/index.html` is the source. `src/index_html.h` is a PROGMEM string copy that must be kept in sync:
```bash
python3 scripts/gen_index_html.py   # if the script exists, else update manually
```
When modifying the web UI, update **both** files.

## FW_BUILD Versioning

`scripts/version.py` generates `FW_BUILD` as `YYYYMMDDNN` at compile time.
Counter increments only when `OTA_RELEASE=1` is set (i.e. from `ota-upload.sh`).
Plain `pio run` reuses the last counter value — no increment.

## OTA Architecture

- Version JSON per-env: `version-esp8266.json`, `version-esp32c3.json` on OTA server
- Binary URL uses HTTP — ESP8266 can't handle HTTPS for large downloads
- OTA triggered via MQTT `{"ota":"<url>"}` or web UI upload

## MQTT Command API

Key commands sent to `<prefix><chipId>/cmd`:

| Command | Effect |
|---------|--------|
| `{"teleIntervalM": 30}` | Telemetry interval in minutes |
| `{"sampleNum": 5}` | Readings to batch before publishing |
| `{"debugLog": true}` | Enable verbose serial/WebSocket logging |
| `{"feature": "web", "value": false}` | Disable web server (saves heap, requires reboot) |
| `{"feature": "wsLog", "value": false}` | Disable WebSocket log (saves heap, requires reboot) |
| `{"ota": "<url>"}` | Trigger OTA firmware update |
| `{"cmd": "reboot"}` | Reboot |
| `{"cmd": "telemetry"}` | Send telemetry immediately |
| `{"cmd": "otaCheck"}` | Check OTA server for update |
| `{"cmd": "wifiOn"}` | Open AP window |
