# Runtime Feature Toggles

Optional subsystems can be enabled/disabled at runtime and persisted to LittleFS (`/features.json`).
Changes take effect after reboot. Defaults: all features **enabled** (including first boot, no file present).

## Available flags

| Flag | Default | What it controls | Heap saved when off |
|------|---------|-----------------|-------------------|
| `web` | `true` | HTTP server (port 80) + web UI | ~2–4 KB |
| `wsLog` | `true` | WebSocket log stream (port 81) | ~3–5 KB |

On **ESP8266** both flags together free ~6–8 KB — enough to stabilise MQTT connections on a tight heap.
On **ESP32-C3** heap is not a concern (~300 KB free), but disabling the web server after initial config reduces the attack surface.

## How to toggle

### Web UI
Open the **Features** section, check/uncheck the toggles, click **Apply & Reboot**.

### MQTT command
Publish to `<prefix><chipId>/cmd`:

```json
{"feature": "wsLog", "value": false}
{"feature": "web",   "value": false}
```

The device saves the flag and reboots immediately.

### Recovery (web disabled)
If the web UI is disabled and MQTT is unavailable, re-flash via serial or use the **MQTT** command once connectivity is restored:

```json
{"feature": "web", "value": true}
```

## Implementation

| File | Role |
|------|------|
| `src/storage.h/cpp` | `FeatureFlags` struct, `loadFeatures()` / `saveFeatures()` — persists `/features.json` |
| `src/logger.h/cpp` | `loggerSetWsEnabled(bool)` — gates `wsServer.begin()` and `wsServer.loop()` |
| `src/webserver.h/cpp` | `webSetEnabled(bool)` — gates `httpServer.begin()` and `handleClient()`; `GET/POST /api/features` |
| `src/main.cpp` | Loads flags from LittleFS after `storageInit()`, calls setters before `loggerInit()`/`webInit()` |
| `src/uplink.cpp` | Handles `{"feature":…}` MQTT command in `handleCommand()` |

## ESP8266-specific memory reductions

Applied unconditionally on ESP8266 (compile-time `#ifdef`):

| Constant | ESP8266 | ESP32-C3 | BSS saved |
|----------|---------|----------|-----------|
| `LOG_RING_SIZE` | 12 | 50 | ~6.4 KB |
| `MqttCommand.payload` | 256 B | 512 B | ~1.3 KB |
