#include "narodmon.h"

#include "sensor_manager.h"
#include "bme280_sensor.h"
#include "sht30_sensor.h"
#include "ds18b20_sensor.h"
#include "pms7003_sensor.h"
#include "scd4x_sensor.h"
#include "sgp4x_sensor.h"
#include "sht4x_sensor.h"
#include "bmp280_sensor.h"
#include "bmp580_sensor.h"
#include "htu21d_sensor.h"
#include "xdb401_sensor.h"
#include "geiger_sensor.h"
#include "sen5x_sensor.h"
#include "../logger.h"
#include "../queues.h"
#include "../system_state.h"
#include "../config.h"
#include "../led.h"
#include <ArduinoJson.h>
#include <Wire.h>

static SensorBase* sensors[MAX_SENSORS] = {};
static bool        s_switched[MAX_SENSORS] = {};  // true = powered from switched 5V rail
static uint8_t     sensorCount = 0;
static uint8_t     activeCount = 0;
static HwConfig    hwCfg;
static char        s_lastData[MAX_SENSORS][SENSOR_DATA_MAX];

uint8_t sensorsActiveCount() { return activeCount; }

void sensorGetLastValues(JsonDocument& out) {
    for (uint8_t i = 0; i < sensorCount; i++) {
        if (!sensors[i] || s_lastData[i][0] == '\0') continue;
        JsonDocument inner;
        if (deserializeJson(inner, s_lastData[i]) == DeserializationError::Ok)
            out[sensors[i]->type()] = inner;
    }
}

// ─── Find or create a sensor instance of the given type ──────────────────────

static SensorBase* makeSensor(const char* type) {
    if (strcmp(type, "bme280")  == 0) return new Bme280Sensor();
    if (strcmp(type, "sht30")   == 0) return new Sht30Sensor();
    if (strcmp(type, "ds18b20") == 0) return new Ds18b20Sensor();
    if (strcmp(type, "pms7003") == 0) return new Pms7003Sensor();
    if (strcmp(type, "scd4x")   == 0) return new Scd4xSensor();
    if (strcmp(type, "sgp4x")   == 0) return new Sgp4xSensor();
    if (strcmp(type, "sht4x")   == 0) return new Sht4xSensor();
    if (strcmp(type, "bmp280")  == 0) return new Bmp280Sensor();
    if (strcmp(type, "bmp580")  == 0) return new Bmp580Sensor();
    if (strcmp(type, "htu21d")  == 0) return new Htu21dSensor();
    if (strcmp(type, "xdb401")  == 0) return new Xdb401Sensor();
    if (strcmp(type, "geiger")  == 0) return new GeigerSensor();
    if (strcmp(type, "sen5x")   == 0) return new Sen5xSensor();
    return nullptr;
}

// ─── Build sensor list from sensorsetup.json ─────────────────────────────────

void sensorsInit() {
    loadHwConfig(hwCfg);

    // Build Wire only once; sensors share it
    if (hwCfg.i2c_sda >= 0 && hwCfg.i2c_scl >= 0) {
        Wire.begin(hwCfg.i2c_sda, hwCfg.i2c_scl);
        logMessageFmt("info", "I2C Wire(%d,%d) started", hwCfg.i2c_sda, hwCfg.i2c_scl);
    }

    sensorCount = 0;
    activeCount = 0;

    if (!xSemaphoreTake(sensorSetupMutex, pdMS_TO_TICKS(1000))) {
        logMessage("error", "sensorsInit: mutex timeout");
        return;
    }

    JsonArray arr = sensorSetupData.as<JsonArray>();
    for (JsonObject entry : arr) {
        if (sensorCount >= MAX_SENSORS) break;

        const char* type = entry["type"] | "";
        bool enabled = entry["enabled"] | false;
        if (!enabled || strlen(type) == 0) continue;

        SensorBase* s = makeSensor(type);
        if (!s) {
            logMessageFmt("warn", "Unknown sensor type: %s", type);
            continue;
        }

        // Set I2C address if provided in config
        uint8_t addr = entry["addr"] | 0;
        if (addr > 0) s->setAddr(addr);

        // XDB401: optional full-scale pressure in Pa
        float fullscale_pa = entry["fullscale_pa"] | 0.0f;
        if (fullscale_pa > 0 && strcmp(type, "xdb401") == 0)
            static_cast<Xdb401Sensor*>(s)->setFullscalePa(fullscale_pa);

        // PMS7003: power and aux pin managed by sensor_manager; no per-sensor pin config needed

        // Geiger counter: GPIO pin from sensor config
        if (strcmp(type, "geiger") == 0) {
            int8_t pin = entry["pin"].isNull() ? (int8_t)-1 : entry["pin"].as<int8_t>();
            static_cast<GeigerSensor*>(s)->setPin(pin);
        }

        // Check for I2C address collisions before initialising
        uint8_t sAddr = s->i2cAddr();
        if (sAddr != 0) {
            bool conflict = false;
            for (uint8_t j = 0; j < sensorCount; j++) {
                if (sensors[j] && sensors[j]->i2cAddr() == sAddr) {
                    logMessageFmt("error", "I2C conflict: %s @ 0x%X already used by %s — skipped", type, sAddr, sensors[j]->type());
                    conflict = true;
                    break;
                }
            }
            if (conflict) { delete s; continue; }
        }

        bool ok = s->begin(hwCfg.i2c_sda, hwCfg.i2c_scl,
                           hwCfg.uart_rx,  hwCfg.uart_tx,
                           hwCfg.onewire);

        s_switched[sensorCount] = entry["switched"] | false;
        sensors[sensorCount++] = s;
        if (ok) activeCount++;
    }

    xSemaphoreGive(sensorSetupMutex);

    // Wire up SGP4x compensation source — use the first active T/H sensor
    Sgp4xSensor* sgp4x = nullptr;
    SensorBase*  thSrc  = nullptr;
    for (uint8_t i = 0; i < sensorCount; i++) {
        if (!sensors[i] || !sensors[i]->isReady()) continue;
        if (!sgp4x && strcmp(sensors[i]->type(), "sgp4x") == 0)
            sgp4x = static_cast<Sgp4xSensor*>(sensors[i]);
        else if (!thSrc && sensors[i]->providesTH())
            thSrc = sensors[i];
    }
    if (sgp4x) {
        if (thSrc) {
            sgp4x->setCompensationSource(thSrc);
            logMessageFmt("info", "SGP4x: compensation from %s", thSrc->type());
        } else {
            logMessage("info", "SGP4x: no T/H source — using default compensation (25°C, 50%RH)");
        }
    }

    STATE_SET(sensorsActive, activeCount);
    logMessageFmt("info", "Sensors: %d configured, %d active", sensorCount, activeCount);
}

// ─── Sensor task ──────────────────────────────────────────────────────────────

static uint16_t s_readingSeq  = 0;
static uint32_t s_lastRead    = 0;
static bool     s_enabled     = false;
static bool     s_windowOn        = false;

static void gpiosOn() {
    for (uint8_t i = 0; i < hwCfg.gpio_count; i++) {
        if (hwCfg.gpio_pin[i] < 0) continue;
        const char* m = hwCfg.gpio_mode[i];
        if      (strncmp(m, "follow", 6) == 0) digitalWrite(hwCfg.gpio_pin[i], HIGH);
        else if (strncmp(m, "invert", 6) == 0) digitalWrite(hwCfg.gpio_pin[i], LOW);
        // "on"/"off": fixed level set at boot, don't change
    }
}

static void gpiosOff() {
    for (uint8_t i = 0; i < hwCfg.gpio_count; i++) {
        if (hwCfg.gpio_pin[i] < 0) continue;
        const char* m = hwCfg.gpio_mode[i];
        if      (strncmp(m, "follow", 6) == 0) digitalWrite(hwCfg.gpio_pin[i], LOW);
        else if (strncmp(m, "invert", 6) == 0) digitalWrite(hwCfg.gpio_pin[i], HIGH);
        // "on"/"off": fixed level, don't change
    }
}

void sensorsEnable() {
    if (s_enabled) return;
    s_enabled = true;
    // Backdate s_lastRead so the first read fires after onTime seconds,
    // giving sensors time to warm up before the first measurement is taken.
    uint16_t onTimeSec  = STATE_GET(onTime);
    uint32_t intervalMs = (uint32_t)STATE_GET(teleIntervalM) * 60000UL;
    if (intervalMs == 0) intervalMs = 60000UL;
    uint32_t readDelayMs = (uint32_t)onTimeSec * 1000UL;
    s_lastRead = millis() - intervalMs + readDelayMs;
    logMessageFmt("info", "Sensors enabled — first read in %ds", onTimeSec);
}

void sensorsEnableDeepSleep() {
    if (s_enabled) return;
    s_enabled = true;
    // Schedule first read at (onTime + 5) seconds from now so PMS7003 has
    // exactly onTime seconds to warm up before doSensorRead() fires.
    // We backdate s_lastRead so the interval timer expires at the right moment.
    uint16_t onTimeSec  = STATE_GET(onTime);
    uint32_t intervalMs = (uint32_t)STATE_GET(teleIntervalM) * 60000UL;
    if (intervalMs == 0) intervalMs = 60000UL;
    uint32_t readDelayMs = ((uint32_t)onTimeSec + 5UL) * 1000UL;
    s_lastRead = millis() - intervalMs + readDelayMs;
    logMessageFmt("info", "Sensors enabled (deep sleep — read in %ds)", onTimeSec + 5);
}

void sensorsReinit() {
    s_enabled = false;
    if (s_windowOn) {
        if (hwCfg.pin5v >= 0) digitalWrite(hwCfg.pin5v, LOW);
        gpiosOff();
        s_windowOn = false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));  // let sensorTask finish current tick
    for (uint8_t i = 0; i < sensorCount; i++) {
        delete sensors[i];
        sensors[i] = nullptr;
    }
    memset(s_switched, 0, sizeof(s_switched));
    memset(s_lastData, 0, sizeof(s_lastData));
    sensorsInit();
    s_lastRead = millis();
    s_enabled  = true;
}

static void reinitSwitchedSensors() {
    for (uint8_t i = 0; i < sensorCount; i++) {
        if (sensors[i] && s_switched[i]) {
            logMessageFmt("debug", "%s: reinit after power-on", sensors[i]->type());
            sensors[i]->begin(hwCfg.i2c_sda, hwCfg.i2c_scl,
                              hwCfg.uart_rx,  hwCfg.uart_tx,
                              hwCfg.onewire);
        }
    }
}

static void doSensorRead() {
    s_lastRead = millis();
    uint32_t chipId = STATE_GET(chipId);

    // Merge all ready sensors into one combined JSON object
    char merged[SENSOR_DATA_MAX];
    merged[0] = '{';
    merged[1] = '\0';
    bool    first        = true;
    bool    hasData      = false;
    uint8_t firstMsgType = 0;

    for (uint8_t i = 0; i < sensorCount; i++) {
        if (!sensors[i] || !sensors[i]->isReady()) continue;
        SensorReading r = {};
        r.deviceId = chipId;
        if (!sensors[i]->read(r)) continue;
        strncpy(s_lastData[i], r.data, SENSOR_DATA_MAX - 1); // {"Temp":25.30,"Hum":65.0,"Press":1013.25}
logMessageFmt("1","%s",s_lastData[i]);
        // r.data = {"k":v,...} — strip braces and append inner content
        size_t dlen = strlen(r.data);
        if (dlen < 3) continue;                     // empty object, skip

        const char* inner = r.data + 1;             // skip leading '{'
        size_t      ilen  = dlen - 2;               // content without '{' and '}'
        size_t      mlen  = strlen(merged);
        size_t      avail = sizeof(merged) - mlen - 2; // reserve for '}' + '\0'

        if (!first && avail > 0) {
            merged[mlen++] = ',';
            merged[mlen]   = '\0';
            avail--;
        }
        strncat(merged, inner, (ilen < avail) ? ilen : avail); //Добавление "Temp":25.30,"Hum":65.0,"Press":1013.25

        if (!hasData) firstMsgType = r.msgType;
        first   = false;
        hasData = true;
        logMessageFmt("debug", "%s: %s", sensors[i]->type(), r.data);
    }

    size_t mlen = strlen(merged);
    merged[mlen]     = '}';
    merged[mlen + 1] = '\0'; //Пакет с данными датчиков
logMessageFmt("2","%s",merged);
    if (getNarodmonMode()) {send_to_narodmon(merged);}

    if (!hasData) return;

    SensorReading combined = {};
    combined.deviceId = chipId;
    combined.time     = 0;
    combined.count    = ++s_readingSeq;
    combined.mV       = 0;
    combined.rssi     = 0;
    combined.immTX    = false;
    combined.msgType  = firstMsgType;
    strncpy(combined.data, merged, sizeof(combined.data) - 1);

    if (xQueueSend(sensorQueue, &combined, pdMS_TO_TICKS(100)) != pdTRUE)
        logMessage("warn", "sensorQueue full — dropping reading");
}

static void tickAllSensors(uint32_t nextReadMs) {
    for (uint8_t i = 0; i < sensorCount; i++)
        if (sensors[i]) sensors[i]->tick(nextReadMs);
}

static void processSensorCycle() {
    if (s_enabled) {
        uint32_t elapsed = millis() - s_lastRead;
        uint16_t intervalM = STATE_GET(teleIntervalM);
        if (intervalM == 0) intervalM = 1;
        uint32_t intervalMs = (uint32_t)intervalM * 60000UL;
        tickAllSensors(s_lastRead + intervalMs);
        if (!s_windowOn) {
            uint32_t onTimeMs = STATE_GET(onTime) * 1000UL;
            uint32_t powerOnAt = intervalMs > onTimeMs ? intervalMs - onTimeMs : 0;
            if (elapsed >= powerOnAt) {
                if (hwCfg.pin5v >= 0) {
                    digitalWrite(hwCfg.pin5v, HIGH);
                    reinitSwitchedSensors();
                }
                gpiosOn();
                s_windowOn = true;
            }
        }
        if (elapsed >= intervalMs) {
            doSensorRead();
            if (hwCfg.pin5v >= 0) digitalWrite(hwCfg.pin5v, LOW);
            gpiosOff();
            s_windowOn = false;
        }
    }
}

void sensorTask(void* pvParameters) {
    for (;;) {
        ledUpdate();
        processSensorCycle();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

#ifdef ESP8266
void sensorProcess() {
    processSensorCycle();
}
#endif
