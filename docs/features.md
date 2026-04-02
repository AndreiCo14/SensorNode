# SensorNode Features & MQTT Command API

## MQTT Command API

Commands are sent as JSON to:

```
Sensors/<chipId>/cmd
```

`<chipId>` is the device's unique chip ID (shown in the web UI and telemetry). The prefix `Sensors/` is the default and can be changed via the web UI.

---

### Configuration commands

These fields can be combined in a single message. Changes are saved to `/hwconfig.json` and take effect immediately.

| Field | Type | Description |
|---|---|---|
| `teleIntervalM` | uint16 | Telemetry publish interval in minutes |
| `sampleNum` | int8 | Number of sensor readings to batch before publishing |
| `onTime` | uint16 | PMS7003 fan warmup time in seconds (minimum 30) |
| `debugLog` | bool | Enable (`true`) or disable (`false`) verbose serial/WebSocket logging |

```json
{"teleIntervalM": 5, "sampleNum": 3}
```

---

### Feature flags

Toggles a persistent feature flag and reboots the device.

```json
{"feature": "web",   "value": false}
{"feature": "wsLog", "value": false}
```

`"value"` defaults to `true` if omitted. See [Feature Toggles](#feature-toggles) below for details.

---

### OTA firmware update

Triggers an immediate OTA update from the given URL, then reboots.

```json
{"ota": "http://example.com/firmware.bin"}
```

---

### Action commands

| Command | Description |
|---|---|
| `reboot` | Restart the device immediately |
| `wifiReset` | Delete saved WiFi credentials (`/wifi.json`) and reboot — device starts in AP mode (`AirMQ-SN-<chipId>` / `configesp`) |
| `wifiOn` | Enable the configuration AP (if not already active) |
| `wifiOff` | Disable the configuration AP |
| `telemetry` | Publish a telemetry message immediately |
| `otaCheck` | Check the OTA version server (`OTA_VERSION_URL`) for a newer firmware |
| `configBackup` | Publish current hardware + sensor configuration to `Sensors/<chipId>/provision/backup` (QoS 1) |
| `configRestore` | Reset `provisioned=false`, resend Start message, re-subscribe to provision topic to re-apply retained config |

---

### Provisioning

On each MQTT connect the device subscribes to `Sensors/<chipId>/provision`. If a **retained** message is present, its JSON payload is applied as configuration immediately (before sensors are enabled). Unknown fields are ignored; all fields are optional.

**Provisioning payload fields:**

| Field | Type | Description |
|---|---|---|
| `teleIntervalM` | uint16 | Telemetry interval in minutes |
| `sampleNum` | int8 | Sensor batch size |
| `onTime` | uint16 | PMS7003 warmup time in seconds (minimum 30) |
| `interval` | uint16 | Sensor read interval in seconds |
| `i2c_sda` | int8 | I2C SDA pin |
| `i2c_scl` | int8 | I2C SCL pin |
| `uart_rx` | int8 | UART RX pin |
| `uart_tx` | int8 | UART TX pin |
| `onewire` | int8 | OneWire pin |
| `led_pin` | int8 | LED pin |
| `5v_pin` | int8 | 5V boost pin |
| `sensors` | array | Active sensor list (enabled sensors only) |
| `mqtt` | object | MQTT config: `broker`, `port`, `prefix`, `tls` |

Pin changes take effect after reboot. Operational params (`teleIntervalM`, `sampleNum`, `onTime`) take effect immediately.

**Publish a provisioning config** (retain=true) to `Sensors/<chipId>/provision`:

```json
{
  "teleIntervalM": 5,
  "sampleNum": 3,
  "sensors": [{"type": "sht30", "enabled": true, "addr": 68}],
  "mqtt": {"broker": "mq.airmq.cc", "port": 18883, "prefix": "Sensors/", "tls": false}
}
```

**`provisioned` flag** — set to `true` after a provisioning config is successfully applied; reported in `Start` and `telemetry` messages. A fresh or factory-reset device reports `false`, signalling the backend to push config.

**Backup** — `{"cmd":"configBackup"}` publishes current active config to `Sensors/<chipId>/provision/backup` (QoS 1, not retained). Includes a `timestamp` (Unix epoch) as a version marker. Only enabled sensors are included.

**Restore flow:**
1. Backend publishes backup payload as retained to `Sensors/<chipId>/provision`
2. Backend sends `{"cmd":"configRestore"}` to the device
3. Device resets `provisioned=false`, resends Start, re-subscribes — broker re-delivers retained config — applied — `provisioned=true`

---

## Feature Toggles

Optional subsystems can be enabled/disabled at runtime and persisted to `/features.json`.
Changes take effect after reboot. Defaults: all features **enabled**.

### Available flags

| Flag | Default | What it controls | Heap saved when off |
|------|---------|-----------------|-------------------|
| `web` | `true` | HTTP server (port 80) + web UI | ~2–4 KB |
| `wsLog` | `true` | WebSocket log stream (port 81) | ~3–5 KB |

On **ESP8266** both flags together free ~6–8 KB — enough to stabilise MQTT connections on a tight heap.
On **ESP32-C3** heap is not a concern (~300 KB free), but disabling the web server after initial config reduces the attack surface.

### How to toggle

**Web UI** — open the Features section, check/uncheck, click **Apply & Reboot**.

**MQTT** — publish to `Sensors/<chipId>/cmd`:

```json
{"feature": "wsLog", "value": false}
{"feature": "web",   "value": false}
```

**Recovery (web disabled)** — re-enable via MQTT once connectivity is restored:

```json
{"feature": "web", "value": true}
```

### Implementation

| File | Role |
|------|------|
| `src/storage.h/cpp` | `FeatureFlags` struct, `loadFeatures()` / `saveFeatures()` — persists `/features.json` |
| `src/logger.h/cpp` | `loggerSetWsEnabled(bool)` — gates `wsServer.begin()` and `wsServer.loop()` |
| `src/webserver.h/cpp` | `webSetEnabled(bool)` — gates `httpServer.begin()` and `handleClient()`; `GET/POST /api/features` |
| `src/main.cpp` | Loads flags after `storageInit()`, calls setters before `loggerInit()`/`webInit()` |
| `src/uplink.cpp` | Handles `{"feature":…}` MQTT command in `handleCommand()` |

---

## ESP8266 Memory Reductions

Applied unconditionally at compile time (`#ifdef`):

| Constant | ESP8266 | ESP32-C3 | BSS saved |
|----------|---------|----------|-----------|
| `LOG_RING_SIZE` | 12 | 50 | ~6.4 KB |
| `MqttCommand.payload` | 256 B | 512 B | ~1.3 KB |
