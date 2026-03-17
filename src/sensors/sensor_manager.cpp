#include "sensor_manager.h"
#include "bme280_sensor.h"
#include "sht30_sensor.h"
#include "ds18b20_sensor.h"
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

static void doSensorRead() {
    s_lastRead = millis();
    uint32_t chipId = STATE_GET(chipId);
    for (uint8_t i = 0; i < sensorCount; i++) {
        if (!sensors[i] || !sensors[i]->isReady()) continue;
        SensorReading r = {};
        r.deviceId = chipId;
        r.time     = 0;
        r.count    = ++s_readingSeq;
        r.mV       = 0;
        r.rssi     = 0;
        r.immTX    = false;
        if (sensors[i]->read(r)) {
            if (xQueueSend(sensorQueue, &r, pdMS_TO_TICKS(100)) != pdTRUE)
                logMessage("sensorQueue full — dropping " + String(sensors[i]->type()), "warn");
            else
                logMessage(String(sensors[i]->type()) + ": " + r.data, "debug");
        }
    }
    ledUpdate();
}

void sensorTask(void* pvParameters) {
    for (;;) {
        uint32_t now = millis();
        uint16_t interval = hwCfg.intervalSec > 0 ? hwCfg.intervalSec : 60;
        if (now - s_lastRead >= (uint32_t)interval * 1000 || s_lastRead == 0)
            doSensorRead();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

#ifdef ESP8266
void sensorProcess() {
    uint32_t now = millis();
    uint16_t interval = hwCfg.intervalSec > 0 ? hwCfg.intervalSec : 60;
    if (now - s_lastRead >= (uint32_t)interval * 1000 || s_lastRead == 0)
        doSensorRead();
}
#endif
