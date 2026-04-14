# Deep Sleep Mode

Deep sleep mode is designed for battery- or solar-powered devices. Instead of staying connected continuously, the device wakes up, reads sensors, publishes data, and immediately enters deep sleep until the next measurement cycle.

## Wake–read–sleep cycle

```
Boot → WiFi connect → MQTT connect → startup window
     → read sensors → publish → sleep(teleIntervalM minutes) → [repeat]
```

1. **Boot** — device starts fresh from reset (deep sleep is a full power-off).
2. **Connect** — WiFi and MQTT are established normally.
3. **Startup window** — a brief window during which the device listens for incoming MQTT commands (e.g. configuration changes, OTA, maintenance). The window length is controlled by `STARTUP_CMD_WINDOW_MS` (compile-time constant).
4. **Sensor read** — sensors are enabled. In deep sleep mode `sensorsEnableDeepSleep()` pre-schedules the first read at `onTime + 5` seconds from now so that the PMS7003 has exactly `onTime` seconds of warm-up time before the read fires.
5. **Publish** — once `sampleNum` readings have been collected, sensor data and telemetry are published to MQTT.
6. **Sleep notice** — a status message is published to `Sensors/<chipId>/Status` immediately before sleeping: `{"sleepTime": <seconds>}`.
7. **Sleep** — the MQTT outbox is flushed, then `enterDeepSleep(teleIntervalM × 60)` is called. The device powers off and wakes after the configured interval.

## Hardware requirements

| MCU | Wiring required |
|-----|-----------------|
| ESP8266 | **GPIO16 must be connected to RST.** The timer wakeup works by pulling RST low via GPIO16; without this wire the device will sleep indefinitely. |
| ESP32 / ESP32-C3 | No extra wiring — uses the built-in `esp_sleep` timer wakeup. |

## Configuration

### Enable / disable

Deep sleep is controlled via the MQTT command API. The setting is persisted in `/hwconfig.json` and survives reboots.

```json
{"deepSleep": true}
{"deepSleep": false}
```

### Sleep duration

The device sleeps for exactly `teleIntervalM` minutes (the same interval used for telemetry in normal mode).

```json
{"teleIntervalM": 10}
```

### Sensor warm-up time (`onTime`)

`onTime` (seconds, minimum 30) determines how long the PMS7003 particulate sensor runs before its reading is taken. In deep sleep mode it also sets the delay between boot and the first sensor read, so the total active time per cycle is roughly:

```
active time ≈ boot + WiFi connect + onTime + publish + flush
```

```json
{"onTime": 60}
```

## Maintenance mode

Maintenance mode temporarily pauses the sleep cycle so the device stays online — useful for OTA updates, configuration changes, or debugging.

```json
{"maintenance": true}   // pause sleep — device stays online
{"maintenance": false}  // resume normal sleep cycle
```

Maintenance mode is **not persisted** — it resets on the next boot. If the device reboots while in maintenance mode, it will resume the normal deep sleep cycle.

## Interaction with PMS7003

The PMS7003 requires a warm-up period before its readings are stable. In deep sleep mode, `sensorsEnableDeepSleep()` backdates the internal read timer so the first read is scheduled exactly `onTime + 5` seconds after boot. This gives the fan the full `onTime` to spin up before data is sampled. See [pms7003.md](pms7003.md) for full PMS7003 details.

## Normal mode vs deep sleep mode comparison

| | Normal mode | Deep sleep mode |
|---|---|---|
| Connection | Persistent | Re-established on every cycle |
| Power consumption | High (WiFi always on) | Low (off between cycles) |
| Sensor reads | Continuous, every `teleIntervalM` | One read per wake cycle |
| OTA / config changes | Apply immediately | Apply on next wake (during startup window) |
| Maintenance window | Always available | Must enable maintenance mode first |
