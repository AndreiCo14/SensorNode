#include "sensor_manager.h"
#include "bme280_sensor.h"
#include "sht30_sensor.h"
#include "ds18b20_sensor.h"
#include "pms7003_sensor.h"
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

uint8_t sensorsActiveCount() { return activeCount; }

// ─── Find or create a sensor instance of the given type ──────────────────────

static SensorBase* makeSensor(const char* type) {
    if (strcmp(type, "bme280")  == 0) return new Bme280Sensor();
    if (strcmp(type, "sht30")   == 0) return new Sht30Sensor();
    if (strcmp(type, "ds18b20") == 0) return new Ds18b20Sensor();
    if (strcmp(type, "pms7003") == 0) return new Pms7003Sensor();
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

        // Set I2C address if present
        uint8_t addr = entry["addr"] | 0;
        if (addr > 0) {
            if (strcmp(type, "bme280") == 0)
                static_cast<Bme280Sensor*>(s)->setAddr(addr);
            else if (strcmp(type, "sht30") == 0)
                static_cast<Sht30Sensor*>(s)->setAddr(addr);
        }

        bool ok = s->begin(hwCfg.i2c_sda, hwCfg.i2c_scl,
                           hwCfg.uart_rx,  hwCfg.uart_tx,
                           hwCfg.onewire);

        sensors[sensorCount++] = s;
        if (ok) activeCount++;
    }

    xSemaphoreGive(sensorSetupMutex);

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

    if (!hasData) { ledUpdate(); return; }

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

    ledUpdate();
}

static bool tickAllSensors() {
    bool triggered = false;
    for (uint8_t i = 0; i < sensorCount; i++)
        if (sensors[i] && sensors[i]->tick())
            triggered = true;
    return triggered;
}

void sensorTask(void* pvParameters) {
    for (;;) {
        if (s_enabled) {
            bool triggered = tickAllSensors();
            uint32_t now = millis();
            uint16_t intervalM = STATE_GET(teleIntervalM);
            if (intervalM == 0) intervalM = 1;
            uint32_t intervalMs = (uint32_t)intervalM * 60000UL;
            if (triggered || now - s_lastRead >= intervalMs)
                doSensorRead();
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

#ifdef ESP8266
void sensorProcess() {
    if (!s_enabled) return;
    bool triggered = tickAllSensors();
    uint32_t now = millis();
    uint16_t intervalM = STATE_GET(teleIntervalM);
    if (intervalM == 0) intervalM = 1;
    uint32_t intervalMs = (uint32_t)intervalM * 60000UL;
    if (triggered || now - s_lastRead >= intervalMs)
        doSensorRead();
}
#endif
