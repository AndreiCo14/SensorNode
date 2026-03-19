#include "sht30_sensor.h"
#include "../logger.h"
#include <Wire.h>
#include <Adafruit_SHT31.h>

static Adafruit_SHT31 sht31;

bool Sht30Sensor::begin(int i2c_sda, int i2c_scl, int, int, int) {
    _ready = false;
    Wire.begin(i2c_sda, i2c_scl);
    if (!sht31.begin(_addr)) {
        logMessage("SHT30/31 not found at 0x" + String(_addr, HEX), "warn");
        return false;
    }
    _ready = true;
    logMessage("SHT30/31 OK (0x" + String(_addr, HEX) + ")", "info");
    return true;
}

bool Sht30Sensor::isReady() { return _ready; }

bool Sht30Sensor::read(SensorReading& r) {
    if (!_ready) return false;

    float temp = sht31.readTemperature();
    float hum  = sht31.readHumidity();

    if (isnan(temp) || isnan(hum)) {
        logMessage("SHT30 read failed", "warn");
        return false;
    }

    r.msgType = msgType();
    snprintf(r.data, sizeof(r.data),
             "{\"Temp\":%.2f,\"Hum\":%.1f}",
             temp, hum);
    return true;
}
