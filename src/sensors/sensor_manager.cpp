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
#include "../logger.h"
#include "../queues.h"
#include "../system_state.h"
#include "../config.h"
#include "../led.h"
#include <ArduinoJson.h>
#include <Wire.h>

static SensorBase* sensors[MAX_SENSORS] = {};
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
    return nullptr;
}

// ─── Build sensor list from sensorsetup.json ─────────────────────────────────

void sensorsInit() {
    loadHwConfig(hwCfg);

    // Build Wire only once; sensors share it
    if (hwCfg.i2c_sda >= 0 && hwCfg.i2c_scl >= 0) {
        Wire.begin(hwCfg.i2c_sda, hwCfg.i2c_scl);
        logMessage("I2C Wire(" + String(hwCfg.i2c_sda) + "," + String(hwCfg.i2c_scl) + ") started", "info");
    }

    sensorCount = 0;
    activeCount = 0;

    if (!xSemaphoreTake(sensorSetupMutex, pdMS_TO_TICKS(1000))) {
        logMessage("sensorsInit: mutex timeout", "error");
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
            logMessage(String("Unknown sensor type: ") + type, "warn");
            continue;
        }

        // Set I2C address if provided in config
        uint8_t addr = entry["addr"] | 0;
        if (addr > 0) s->setAddr(addr);

        // XDB401: optional full-scale pressure in Pa
        float fullscale_pa = entry["fullscale_pa"] | 0.0f;
        if (fullscale_pa > 0 && strcmp(type, "xdb401") == 0)
            static_cast<Xdb401Sensor*>(s)->setFullscalePa(fullscale_pa);

        // PMS7003: power pin from HwConfig; SET pin optional (default -1 = not connected)
        if (strcmp(type, "pms7003") == 0) {
            int8_t setPin = entry["set_pin"].isNull() ? (int8_t)-1 : entry["set_pin"].as<int8_t>();
            bool   setInv = entry["set_inverted"] | true;
            static_cast<Pms7003Sensor*>(s)->setPins(hwCfg.pin5v, setPin, setInv);
        }

        // Check for I2C address collisions before initialising
        uint8_t sAddr = s->i2cAddr();
        if (sAddr != 0) {
            bool conflict = false;
            for (uint8_t j = 0; j < sensorCount; j++) {
                if (sensors[j] && sensors[j]->i2cAddr() == sAddr) {
                    logMessage(String("I2C conflict: ") + type +
                               " @ 0x" + String(sAddr, HEX) +
                               " already used by " + sensors[j]->type() +
                               " — skipped", "error");
                    conflict = true;
                    break;
                }
            }
            if (conflict) { delete s; continue; }
        }

        bool ok = s->begin(hwCfg.i2c_sda, hwCfg.i2c_scl,
                           hwCfg.uart_rx,  hwCfg.uart_tx,
                           hwCfg.onewire);

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
            logMessage(String("SGP4x: compensation from ") + thSrc->type(), "info");
        } else {
            logMessage("SGP4x: no T/H source — using default compensation (25°C, 50%RH)", "info");
        }
    }

    STATE_SET(sensorsActive, activeCount);
    logMessage("Sensors: " + String(sensorCount) + " configured, " +
               String(activeCount) + " active", "info");
}

// ─── Sensor task ──────────────────────────────────────────────────────────────

static uint16_t s_readingSeq  = 0;
static uint32_t s_lastRead    = 0;
static bool     s_enabled     = false;

void sensorsEnable() {
    if (s_enabled) return;
    s_enabled  = true;
    s_lastRead = millis();  // start interval timer; PMS will trigger first combined read
    logMessage("Sensors enabled — awaiting first cycle", "info");
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
    logMessage("Sensors enabled (deep sleep — read in " + String(onTimeSec + 5) + "s)", "info");
}

void sensorsReinit() {
    s_enabled = false;
    vTaskDelay(pdMS_TO_TICKS(20));  // let sensorTask finish current tick
    for (uint8_t i = 0; i < sensorCount; i++) {
        delete sensors[i];
        sensors[i] = nullptr;
    }
    memset(s_lastData, 0, sizeof(s_lastData));
    sensorsInit();
    s_lastRead = millis();
    s_enabled  = true;
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
        strncpy(s_lastData[i], r.data, SENSOR_DATA_MAX - 1);

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
        strncat(merged, inner, (ilen < avail) ? ilen : avail);

        if (!hasData) firstMsgType = r.msgType;
        first   = false;
        hasData = true;
        logMessage(String(sensors[i]->type()) + ": " + r.data, "debug");
    }

    size_t mlen = strlen(merged);
    merged[mlen]     = '}';
    merged[mlen + 1] = '\0';

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
        logMessage("sensorQueue full — dropping reading", "warn");
}

static void tickAllSensors(uint32_t nextReadMs) {
    for (uint8_t i = 0; i < sensorCount; i++)
        if (sensors[i]) sensors[i]->tick(nextReadMs);
}

void sensorTask(void* pvParameters) {
    for (;;) {
        ledUpdate();
        if (s_enabled) {
            uint32_t now = millis();
            uint16_t intervalM = STATE_GET(teleIntervalM);
            if (intervalM == 0) intervalM = 1;
            uint32_t intervalMs = (uint32_t)intervalM * 60000UL;
            tickAllSensors(s_lastRead + intervalMs);
            if (now - s_lastRead >= intervalMs)
                doSensorRead();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

#ifdef ESP8266
void sensorProcess() {
    if (!s_enabled) return;
    uint32_t now = millis();
    uint16_t intervalM = STATE_GET(teleIntervalM);
    if (intervalM == 0) intervalM = 1;
    uint32_t intervalMs = (uint32_t)intervalM * 60000UL;
    tickAllSensors(s_lastRead + intervalMs);
    if (now - s_lastRead >= intervalMs)
        doSensorRead();
}
#endif
