# Supported Sensors

This document describes the sensors supported by SensorNode, how to enable them, and what data they publish.

---

## Configuration

Sensor configuration is stored in two files on the device:

### `/sensorsetup.json`
A JSON array listing the sensors to use. Each entry must have at least `type` and `enabled`.

```json
[
  {"type": "bme280", "enabled": true},
  {"type": "sht30",  "enabled": true, "switched": true},
  {"type": "geiger", "enabled": true, "pin": 5}
]
```

| Field | Type | Description |
|---|---|---|
| `type` | string | Sensor type string (see each sensor below) |
| `enabled` | bool | Must be `true` to initialise |
| `addr` | int | I2C address override (decimal). Optional — uses sensor default if omitted |
| `switched` | bool | Set to `true` if the sensor is powered from the switched 5V rail (`5v_pin`). The sensor will be re-initialized automatically each time the rail powers on. Default: `false` (constant power assumed) |

### `/hwconfig.json`
Hardware and timing settings. Pin fields are optional — each firmware build has board-specific defaults compiled in. Only set a pin here if you need to override the default.

**ESP32-C3 defaults:** SDA=6, SCL=7, UART RX=20, TX=21, 5V=9
**ESP8266 defaults:** SDA=4 (D2), SCL=5 (D1), UART RX=3, TX=1, 5V=15

```json
{
  "i2c_sda": 6,
  "i2c_scl": 7,
  "interval": 60,
  "teleIntervalM": 30,
  "sampleNum": 1,
  "onTime": 30,
  "deepSleep": false
}
```

| Field | Description | Default |
|---|---|---|
| `i2c_sda` / `i2c_scl` | I2C pins (shared by all I2C sensors) | board default |
| `uart_rx` / `uart_tx` | UART pins (PMS7003) | board default |
| `onewire` | 1-Wire pin (DS18B20) | board default |
| `5v_pin` | GPIO controlling an external 5V power rail; switched off between reads | disabled |
| `gpio<N>` | Controlled GPIO; value is `follow`, `invert`, `on`, or `off` | — |
| `interval` | How often to read sensors, in seconds | 60 |
| `teleIntervalM` | How often to send telemetry, in minutes | 30 |
| `sampleNum` | Number of readings to collect before publishing | 1 |
| `onTime` | Warm-up time before a read, in seconds (minimum 30) | 30 |
| `deepSleep` | Sleep between read cycles to save power | false |

---

## DS18B20
**Measures:** Temperature | **Interface:** 1-Wire

Waterproof temperature probe. The 1-Wire pin is set in `hwconfig.json` → `onewire`. Multiple devices on the same bus are supported but only the first one is read.

**Config:**
```json
{"type": "ds18b20", "enabled": true}
```

**Published values:**
| Field | Unit |
|---|---|
| `Temp` | °C |

---

## BME280
**Measures:** Temperature, Humidity, Pressure | **Interface:** I2C

All-in-one environmental sensor. Default I2C address is `0x76`; use `addr` (decimal) to override.

**Config:**
```json
{"type": "bme280", "enabled": true}
{"type": "bme280", "enabled": true, "addr": 119}
```

**Published values:**
| Field | Unit |
|---|---|
| `Temp` | °C |
| `Hum` | %RH |
| `Press` | hPa |

---

## SHT30 / SHT31
**Measures:** Temperature, Humidity | **Interface:** I2C

Sensirion humidity and temperature sensor. Default address `0x44`; alternate `0x45` (= 69 decimal) when the ADDR pin is high.

**Config:**
```json
{"type": "sht30", "enabled": true}
{"type": "sht30", "enabled": true, "addr": 69}
```

**Published values:**
| Field | Unit |
|---|---|
| `Temp` | °C |
| `Hum` | %RH |

---

## SHT4x
**Measures:** Temperature, Humidity | **Interface:** I2C

High-accuracy Sensirion sensor (SHT40/41/43/45). Fixed address `0x44`.

**Config:**
```json
{"type": "sht4x", "enabled": true}
```

**Published values:**
| Field | Unit |
|---|---|
| `Temp` | °C |
| `Hum` | %RH |

---

## HTU21D
**Measures:** Temperature, Humidity | **Interface:** I2C

Fixed address `0x40`; cannot be changed.

**Config:**
```json
{"type": "htu21d", "enabled": true}
```

**Published values:**
| Field | Unit |
|---|---|
| `Temp` | °C |
| `Hum` | %RH |

---

## BMP280
**Measures:** Temperature, Pressure | **Interface:** I2C

Pressure and temperature only — no humidity. Default address `0x76`.

**Config:**
```json
{"type": "bmp280", "enabled": true}
{"type": "bmp280", "enabled": true, "addr": 119}
```

**Published values:**
| Field | Unit |
|---|---|
| `Temp` | °C |
| `Press` | hPa |

---

## BMP580
**Measures:** Temperature, Pressure | **Interface:** I2C

High-performance barometric sensor. Default address `0x46`.

**Config:**
```json
{"type": "bmp580", "enabled": true}
{"type": "bmp580", "enabled": true, "addr": 71}
```

**Published values:**
| Field | Unit |
|---|---|
| `Temp` | °C |
| `Press` | hPa |

---

## XDB401
**Measures:** Pressure, Temperature | **Interface:** I2C

Industrial liquid/gas pressure sensor. Default address `0x7F`, full-scale range 0–0.5 MPa. Use `fullscale_pa` to match your sensor's rated range.

**Config:**
```json
{"type": "xdb401", "enabled": true}
{"type": "xdb401", "enabled": true, "fullscale_pa": 1000000}
```

| Extra field | Description | Default |
|---|---|---|
| `fullscale_pa` | Sensor full-scale range in Pa | 500000 (0.5 MPa) |

**Published values:**
| Field | Unit |
|---|---|
| `Temp` | °C |
| `Press` | hPa |

---

## SGP4x (SGP40 / SGP41)
**Measures:** VOC index, NOx index (SGP41 only) | **Interface:** I2C

Air quality sensor. Outputs a VOC index (1–500 scale; 100 = baseline). The SGP41 variant also outputs a NOx index. The device variant is detected automatically. If a temperature/humidity sensor is active at the same time, it is used for compensation automatically.

Fixed address `0x59`.

**Config:**
```json
{"type": "sgp4x", "enabled": true}
```

**Published values:**
| Field | Unit | Notes |
|---|---|---|
| `VOC` | index (1–500) | SGP40 and SGP41 |
| `NOx` | index (1–500) | SGP41 only |

---

## SCD4x
**Measures:** CO₂, Temperature, Humidity | **Interface:** I2C

Photoacoustic CO₂ sensor (SCD40/41). Fixed address `0x62`.

**Config:**
```json
{"type": "scd4x", "enabled": true}
```

**Published values:**
| Field | Unit |
|---|---|
| `CO2` | ppm |
| `Temp` | °C |
| `Hum` | %RH |

---

## SEN5x (SEN54 / SEN55)
**Measures:** Particulate matter, Temperature, Humidity, VOC, NOx (SEN55 only) | **Interface:** I2C

Sensirion all-in-one air quality module. The SEN54 / SEN55 variant is detected automatically. Fixed address `0x69`.

**Config:**
```json
{"type": "sen5x", "enabled": true}
```

**Published values:**
| Field | Unit | Notes |
|---|---|---|
| `PMS1` | µg/m³ | PM1.0 |
| `PMS25` | µg/m³ | PM2.5 |
| `PMS10` | µg/m³ | PM10 |
| `Temp` | °C | |
| `Hum` | %RH | |
| `VOC` | index (1–500) | |
| `NOx` | index (1–500) | SEN55 only |

---

## PMS7003
**Measures:** Particulate matter | **Interface:** UART

Plantower laser particle counter. Requires a warm-up period before each measurement. The sensor is powered on shortly before the read window and off again after; warm-up duration is set by `onTime` in `hwconfig.json` (minimum 30 s). The 5V supply pin (`5v_pin`) must be configured if the sensor is powered via the controlled rail.

UART pins are set in `hwconfig.json` (`uart_rx`, `uart_tx`).

**Config:**
```json
{"type": "pms7003", "enabled": true}
```

**Published values:**
| Field | Unit | Notes |
|---|---|---|
| `PMS1` | µg/m³ | PM1.0, averaged over warm-up window |
| `PMS25` | µg/m³ | PM2.5 |
| `PMS10` | µg/m³ | PM10 |

---

## Geiger Counter
**Measures:** Radiation | **Interface:** GPIO

Counts ionising radiation pulses and reports counts per minute (CPM). The GPIO pin must be specified in the sensor config. The minimum measurement window is 60 seconds; with a longer `interval`, CPM is normalised over the actual elapsed time.

**Config:**
```json
{"type": "geiger", "enabled": true, "pin": 5}
```

| Extra field | Description |
|---|---|
| `pin` | GPIO pin connected to the Geiger tube pulse output (required) |

**Published values:**
| Field | Unit |
|---|---|
| `Count` | CPM (counts per minute) |
