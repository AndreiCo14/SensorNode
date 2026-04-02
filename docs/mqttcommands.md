# MQTT Command API

Commands are sent as JSON to the topic:

```
Sensors/<chipId>/cmd
```

where `<chipId>` is the device's unique chip ID (shown in the web UI and telemetry). The prefix `Sensors/` is the default and can be changed via the web UI.

---

## Configuration commands

These fields can be combined in a single JSON message. Changes are saved to `/hwconfig.json` and take effect immediately.

| Field | Type | Description |
|---|---|---|
| `teleIntervalM` | uint16 | Telemetry publish interval in minutes |
| `sampleNum` | int8 | Number of sensor readings to batch before publishing |
| `onTime` | uint16 | PMS7003 fan warmup time in seconds (minimum 30) |
| `debugLog` | bool | Enable (`true`) or disable (`false`) verbose serial/WebSocket logging |

**Example:**
```json
{"teleIntervalM": 5, "sampleNum": 3}
```

---

## Feature flags

Toggles a persistent feature flag and reboots the device.

```json
{"feature": "web", "value": true}
{"feature": "wsLog", "value": false}
```

| Feature | Default | Description |
|---|---|---|
| `web` | `true` | HTTP server + web UI on port 80 |
| `wsLog` | `true` | WebSocket log server on port 81 |

`"value"` defaults to `true` if omitted.

---

## OTA firmware update

Triggers an immediate OTA update from the given URL, then reboots.

```json
{"ota": "http://example.com/firmware.bin"}
```

---

## Action commands

Sent as `{"cmd": "<action>"}`.

| Command | Description |
|---|---|
| `reboot` | Restart the device immediately |
| `wifiReset` | Delete saved WiFi credentials (`/wifi.json`) and reboot — device starts in AP mode for fresh setup (`AirMQ-SN-<chipId>` / `configesp`) |
| `wifiOn` | Enable the configuration AP (if not already active) |
| `wifiOff` | Disable the configuration AP |
| `telemetry` | Publish a telemetry message immediately |
| `otaCheck` | Check the OTA version server (`OTA_VERSION_URL`) for a newer firmware |
| `configBackup` | Publish current hardware + sensor configuration as JSON to `Sensors/<chipId>/provision/backup` (QoS 1) |
| `configRestore` | Re-subscribe to `Sensors/<chipId>/provision`, causing the broker to re-deliver the retained provisioning config and apply it immediately |

**Examples:**
```json
{"cmd": "reboot"}
{"cmd": "wifiReset"}
{"cmd": "telemetry"}
{"cmd": "configBackup"}
```

---

## Provisioning

On each MQTT connect the device subscribes to:

```
Sensors/<chipId>/provision
```

If a **retained** message is present on this topic, its JSON payload is applied as configuration immediately (before sensors are enabled). Unknown fields are ignored; all fields are optional.

### Provisioning payload fields

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
| `sensors` | array | Sensor enable/disable list (same format as `/sensorsetup.json`) |

Pin changes take effect after reboot. Operational params (`teleIntervalM`, `sampleNum`, `onTime`) take effect immediately.

If no retained message is present within the startup window, the device continues with locally saved or default configuration.

### Publishing a provisioning config

Publish with `retain=true` to `Sensors/<chipId>/provision`:

```json
{
  "teleIntervalM": 5,
  "sampleNum": 3,
  "sensors": [{"type": "bme280", "enabled": true}]
}
```

### Backup

Send `{"cmd": "configBackup"}` to receive the current config at `Sensors/<chipId>/provision/backup` (QoS 1, not retained). The payload includes a `timestamp` field (Unix epoch) that records when the backup was taken and serves as a config version. The `timestamp` field is ignored when the payload is applied as a provisioning config.
