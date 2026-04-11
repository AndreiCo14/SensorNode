# WiFi Reconnect State Machine

## Overview

After a successful initial connection the firmware monitors WiFi continuously and attempts to recover automatically when the connection is lost — regardless of cause (temporary signal loss, L2 MAC blacklist, wrong credentials after a password change, AP disappearing entirely, etc.).

The strategy is a three-phase escalation. Each phase is time-boxed; failure moves to the next phase. Recovery from any phase is transparent: the AP is closed, NTP is re-synced, and MQTT reconnects automatically.

---

## Phases

```
WiFi lost
    │
    ▼  0 – 30 s
┌─────────────────────────────┐
│  Phase 1 — Primary only     │  WiFi.begin(primary) every 10 s
└─────────────────────────────┘
    │ still disconnected at 30 s
    ▼  30 – 60 s
┌─────────────────────────────┐
│  Phase 2 — Secondary only   │  WiFi.begin(secondary) every 10 s
│  (skipped if not configured)│
└─────────────────────────────┘
    │ still disconnected at 60 s
    ▼  60 s +
┌─────────────────────────────┐
│  Phase 3 — AP raised        │  AP "AirMQ-SN-<id>" comes up
│  + alternate retries        │  WiFi.begin(primary/secondary) alternating every 60 s
└─────────────────────────────┘
    │ WiFi connects (any phase)
    ▼
┌─────────────────────────────┐
│  Recovered                  │  AP closed (if it was raised)
│                             │  NTP re-synced
│                             │  MQTT reconnects automatically
└─────────────────────────────┘
```

### Phase 1 — Primary only (0–30 s)

`WiFi.begin(primary)` is called every **10 s**. This covers transient losses: brief signal dropout, router restart, or a MAC blacklist entry that is quickly removed.

### Phase 2 — Secondary only (30–60 s)

If a secondary SSID is configured and the primary has not responded in 30 s, the firmware switches to `WiFi.begin(secondary)` every **10 s**. This handles the primary network being permanently gone (AP failure, credential change, etc.) while a backup network is available.

If no secondary is configured, phase 2 is skipped and the firmware moves straight to phase 3 at 30 s.

### Phase 3 — AP raised + background retries (60 s+)

When both networks have failed for 60 s, the config portal AP (`AirMQ-SN-<chipId>`, no password, 192.168.4.1) is raised so the device can be reconfigured. The firmware continues probing both networks: **primary and secondary alternate** every **60 s**, so whichever network recovers first will be picked up without requiring a reboot or manual intervention.

When either network connects:
- The AP is closed immediately
- The device switches to STA-only mode
- NTP is re-synced
- MQTT reconnects on the next cycle

---

## Boot-time behavior

During boot the firmware runs the same primary→secondary probe inside a 3-minute AP window (`AP_WINDOW_MS = 180 s`). If neither network connects within that window, the device enters phase 3 directly (the AP is already up, both networks have already been tried). From that point the same alternating retry logic runs indefinitely until a network responds.

---

## Timing constants

Defined in [src/config.h](../src/config.h):

| Constant | Default | Meaning |
|---|---|---|
| `WIFI_RECONN_ATTEMPT_MS` | 10 000 ms | Interval between `WiFi.begin()` calls in phases 1 and 2 |
| `WIFI_RECONN_PRIMARY_MS` | 30 000 ms | Duration of phase 1 (primary-only window) |
| `WIFI_RECONN_SECONDARY_MS` | 30 000 ms | Duration of phase 2 (secondary-only window) |
| `WIFI_RECONN_AP_RETRY_MS` | 60 000 ms | Interval between retries in phase 3 |
| `AP_WINDOW_MS` | 180 000 ms | Boot-time AP window before entering phase 3 |

---

## LED indicators

| Color | State |
|---|---|
| Blue pulse | Connecting / reconnecting (phases 1 and 2) |
| Purple solid | AP only — no credentials configured |
| Purple solid | Phase 3 — AP raised, retrying in background |
| Green pulse | WiFi connected, MQTT not yet connected |
| Yellow solid (2 min) | MQTT connected (clears after 2 minutes) |
| Off | Normal operation after MQTT-OK window |

> In phase 3 the LED is purple (AP state) even though retries are running in the background.

---

## Implementation

All reconnect logic lives in `src/uplink.cpp`:

- **`wifiReconnectTick(ssid1, pass1, ssid2, pass2)`** — called every loop iteration while `WiFi.status() != WL_CONNECTED`. Manages phase transitions, AP lifecycle, and rate-limited `WiFi.begin()` calls.
- **`wifiReconnected()`** — called once on the connected edge. Closes the AP if it was raised by the reconnect logic, resets state variables, updates the LED.

The same two functions are used by both the ESP32 FreeRTOS task (`uplinkTask`) and the ESP8266 cooperative state machine (`uplinkProcess` / `US_CONNECTED`, `US_AP_ONLY`).
