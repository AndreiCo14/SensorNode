# Supported Sensors

All sensors implement `SensorBase`. The `sensor_manager` calls `begin()` once at startup and `read()` every `intervalM` minutes. `msgType` is an internal tag used during sensor merging; it is not included in the MQTT payload.

---

## DS18B20
**Type string:** `ds18b20` | **msgType:** 201 | **Interface:** 1-Wire

Configured via `onewire_pin`. Detects device count at init — fails if none found. Reads the first device on the bus (`index 0`).

**JSON output:**
```json
{"Temp": 23.45}
```

**Library:** `DallasTemperature` + `OneWire`

---

## BME280
**Type string:** `bme280` | **msgType:** 202 | **Interface:** I2C | **Default address:** `0x76`

Temperature, humidity, and pressure. Pressure is converted from Pa to hPa. Caches last T/H values for use as compensation source by SGP4x.

**JSON output:**
```json
{"Temp": 23.45, "Hum": 55.1, "Press": 1013.25}
```

**Library:** `SparkFunBME280`

---

## SHT30 / SHT31
**Type string:** `sht30` | **msgType:** 235 | **Interface:** I2C | **Default address:** `0x44`

Temperature and humidity. Returns `false` on NaN (sensor failure).

**JSON output:**
```json
{"Temp": 23.45, "Hum": 55.1}
```

**Library:** `Adafruit_SHT31`

---

## SHT4x
**Type string:** `sht4x` | **msgType:** 235 | **Interface:** I2C | **Default address:** `0x44`

Temperature and humidity. Configured for high precision, heater disabled. Returns `false` on read failure.

**JSON output:**
```json
{"Temp": 23.45, "Hum": 55.1}
```

**Library:** `Adafruit_SHT4x`

---

## HTU21D
**Type string:** `htu21d` | **msgType:** 235 | **Interface:** I2C | **Fixed address:** `0x40`

Temperature and humidity. Returns `false` on I2C timeout or CRC error. Address is fixed; `begin()` ignores pin parameters.

**JSON output:**
```json
{"Temp": 23.45, "Hum": 55.1}
```

**Library:** `SparkFunHTU21D`

---

## BMP280
**Type string:** `bmp280` | **msgType:** 235 | **Interface:** I2C | **Default address:** `0x76`

Temperature and pressure only (no humidity). Pressure converted from Pa to hPa. Uses default `Wire` bus (no explicit `Wire.begin()` — relies on caller or framework init).

**JSON output:**
```json
{"Temp": 23.45, "Press": 1013.25}
```

**Library:** `Adafruit_BMP280`

---

## BMP580
**Type string:** `bmp580` | **msgType:** 235 | **Interface:** I2C | **Default address:** `0x46`

Temperature and pressure. Uses SparkFun BMP581 library (compatible with BMP580). Pressure converted from Pa to hPa.

**JSON output:**
```json
{"Temp": 23.45, "Press": 1013.25}
```

**Library:** `SparkFun_BMP581_Arduino_Library`

---

## XDB401
**Type string:** `xdb401` | **msgType:** 235 | **Interface:** I2C | **Default address:** `0x7F`

Digital pressure + temperature sensor. Fullscale is configurable (default 500000 Pa / 0.5 MPa). Each `read()` triggers a measurement via register `0x30/0x0A`, waits 50 ms, then reads raw 24-bit pressure and 16-bit temperature. Both are decoded as signed values.

**Config options:**
```json
{"type": "xdb401", "enabled": true, "fullscale_pa": 1000000}
```

**JSON output:**
```json
{"Temp": 23.45, "Press": 150.00}
```

**No external library** — raw I2C reads via `Wire`.

---

## SGP4x (SGP40 / SGP41)
**Type string:** `sgp4x` | **msgType:** 235 | **Interface:** I2C | **Fixed address:** `0x59`

VOC index sensor. Auto-detects SGP40 (VOC only) vs SGP41 (VOC + NOx) at first `read()` by attempting `measureRawSignals()`; falls back to SGP40 mode on error. Runs one conditioning step at `begin()`.

Accepts a T/H compensation source (`setCompensationSource()`), wired automatically by `sensor_manager` if a T/H-capable sensor (BME280, SHT30, SHT4x, etc.) is present. Defaults to 25 °C / 50 %RH if no source.

Raw signals are processed through Sensirion VOC/NOx Gas Index Algorithm libraries to produce index values (1–500 scale).

**JSON output (SGP41):**
```json
{"VOC": 100, "NOx": 1}
```

**JSON output (SGP40):**
```json
{"VOC": 100}
```

**Libraries:** `SensirionI2CSgp41`, `Sensirion Gas Index Algorithm`

---

## SCD4x
**Type string:** `scd4x` | **msgType:** 235 | **Interface:** I2C | **Fixed address:** `0x62`

CO₂, temperature, and humidity. Starts periodic measurement at `begin()` (stops any running measurement first). `read()` checks `getDataReadyStatus()` and returns `false` if not ready — no blocking wait.

**JSON output:**
```json
{"CO2": 850, "Temp": 23.45, "Hum": 55.1}
```

**Library:** `SensirionI2cScd4x`

---

## SEN5x (SEN54 / SEN55)
**Type string:** `sen5x` | **msgType:** 211 | **Interface:** I2C | **Fixed address:** `0x69`

Particulate matter, temperature, humidity, and VOC. SEN55 also adds NOx. Auto-detects variant by reading product name at init.

Starts continuous measurement after a 1.2 s post-reset delay. `read()` checks `readDataReady()` — returns `false` if not ready. Caches last T/H for use as compensation source.

**JSON output (SEN55):**
```json
{"PM1": 1.2, "PM25": 2.3, "PM4": 3.1, "PM10": 4.0, "Temp": 23.45, "Hum": 55.1, "VOC": 100.0, "NOx": 1.0}
```

**JSON output (SEN54):**
```json
{"PM1": 1.2, "PM25": 2.3, "PM4": 3.1, "PM10": 4.0, "Temp": 23.45, "Hum": 55.1, "VOC": 100.0}
```

**Library:** `SensirionI2CSen5x`

---

## PMS7003
**Type string:** `pms7003` | **msgType:** 240 | **Interface:** UART0 (9600 baud)

Particulate matter sensor (PM1.0, PM2.5, PM10). Uses a warm-up / state-machine approach:

1. **IDLE** → waits until `onTime` seconds before the next scheduled read, then opens `Serial` at 9600 baud and enters WARMING
2. **WARMING** → parses incoming 32-byte frames (~1/s), stores PM1/2.5/10 values into a 10-entry ring buffer
3. **DATA_READY** → once `onTime` has elapsed, averages the buffered frames and marks data ready for `read()`

`onTime` minimum is 30 s. Averaging is a simple mean over all valid frames received during warm-up (up to 10).

**JSON output:**
```json
{"PMS1": 1.2, "PMS25": 2.3, "PMS10": 4.0}
```

**No external library** — manual UART frame parsing.

---

## Geiger Counter
**Type string:** `geiger` | **msgType:** 210 | **Interface:** GPIO (interrupt)

Counts radiation pulses via a FALLING-edge interrupt on a configured GPIO pin (`INPUT_PULLUP`). `read()` requires at least 60 s elapsed since the last read; before that it returns `false`.

CPM is calculated as:
```
CPM = pulse_count / (elapsed_ms / 60000)
```

The counter is reset to zero after each successful read. With `intervalM = N`, the window is exactly N minutes and CPM = raw count / N.

**JSON output:**
```json
{"Count": 20.0}
```

**No external library** — GPIO interrupt only.
