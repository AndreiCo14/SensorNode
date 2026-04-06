# OTA Distribution Channels

## Overview

Two channels are maintained on the OTA server:

| Channel | Server path | Purpose |
|---------|-------------|---------|
| `dev` | `.../SensorNode/dev/` | Development builds — pushed on every upload |
| `release` | `.../SensorNode/release/` | Stable builds — promoted manually |

The channel a device belongs to is fixed at **compile time** via the PlatformIO environment. There is no runtime switching.

---

## PlatformIO Environments

| Environment | MCU | Channel |
|-------------|-----|---------|
| `esp8266` | ESP8266 | dev |
| `esp8266-release` | ESP8266 | release |
| `esp32c3` | ESP32-C3 | dev |
| `esp32c3-release` | ESP32-C3 | release |

Flash a device with the appropriate environment to assign it to a channel:

```bash
pio run -t upload -e esp8266-release   # release channel
pio run -t upload -e esp8266           # dev channel
```

---

## Server Layout

Each channel directory holds:

```
SensorNode/
├── dev/
│   ├── version-esp8266.json          # version manifest
│   ├── version-esp32c3.json
│   ├── firmware-esp8266-latest.bin   # symlink → latest versioned binary
│   ├── firmware-esp32c3-latest.bin
│   ├── firmware-esp8266-2026040601.bin
│   └── firmware-esp32c3-2026040601.bin
└── release/
    ├── version-esp8266.json
    ├── version-esp32c3.json
    ├── firmware-esp8266-latest.bin
    └── ...
```

Version manifest format:

```json
{"build": "2026040601", "env": "esp8266", "url": "http://ota.example.com/.../firmware-esp8266-latest.bin"}
```

> Binary download URLs use HTTP even when the version check uses HTTPS — ESP8266 cannot hold a WebSocket and a TLS download connection open simultaneously.

---

## Building and Uploading

Use `scripts/ota-upload.sh`. The channel is inferred from the environment name:

```bash
# Dev channel
./scripts/ota-upload.sh esp8266
./scripts/ota-upload.sh esp32c3

# Release channel
./scripts/ota-upload.sh esp8266-release
./scripts/ota-upload.sh esp32c3-release

# Release + immediately trigger OTA on a specific device
./scripts/ota-upload.sh esp8266-release 1234567890
```

The script:
1. Sets `OTA_RELEASE=1` and runs `pio run -e <ENV>`, which causes `scripts/version.py` to increment the build counter and compile the new `FW_BUILD` into the binary.
2. Uploads the binary to the server as `firmware-<base_env>-<build>.bin` and updates the `latest` symlink.
3. Writes the updated version JSON.
4. Optionally publishes an MQTT OTA trigger to the specified device.

---

## Server Credentials (`.env`)

Create a `.env` file at the project root (gitignored). The upload script sources it automatically.

```bash
OTA_SSH_USER=<user>
OTA_SSH_ADDR=<server-ip-or-hostname>
OTA_SSH_PORT=<ssh-port>
OTA_SERVER_BASE=/path/on/server/to/OTA/SensorNode   # no trailing slash, no channel suffix
OTA_URL_BASE=https://your-ota-server.com/files/OTA/SensorNode

MQTT_HOST=mq.airmq.cc
MQTT_PORT=18883
```

The script appends `/dev` or `/release` to both `OTA_SERVER_BASE` and `OTA_URL_BASE` based on the environment used.

---

## Version Numbering

Build numbers follow the format `YYYYMMDDNN` (e.g. `2026040601` = first release on 2026-04-06).

The counter is stored in `.build_counter` at the project root and only increments when `OTA_RELEASE=1` is set (i.e. when called via `ota-upload.sh`). Plain `pio run` reuses the last counter value so dev builds don't consume counter slots.

Both dev and release channels share the same counter and binary — a dev build uploaded today gets the same build number whether it ends up in `dev/` or `release/`.
