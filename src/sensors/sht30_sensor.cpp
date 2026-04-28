#include "sht30_sensor.h"
#include "../logger.h"
#include <Wire.h>
#include <Adafruit_SHT31.h>

static Adafruit_SHT31 sht31;

bool Sht30Sensor::begin(int i2c_sda, int i2c_scl, int, int, int) {
    _ready = false;
    Wire.begin(i2c_sda, i2c_scl);
    if (!sht31.begin(_addr)) {
        logMessageFmt("warn", "SHT30/31 not found at 0x%X", _addr);
        return false;
    }
    _ready = true;
    logMessageFmt("info", "SHT30/31 OK (0x%X)", _addr);
    return true;
}

bool Sht30Sensor::isReady() { return _ready; }

bool Sht30Sensor::read(SensorReading& r) {
    if (!_ready) return false;

    float temp = sht31.readTemperature();
    float hum  = sht31.readHumidity();

    if (isnan(temp) || isnan(hum)) {
        logMessageFmt("warn", "SHT30 read failed");
        return false;
    }

    _lastTemp = temp;
    _lastHum  = hum;
    r.msgType = msgType();
    snprintf(r.data, sizeof(r.data),
             "{\"Temp\":%.2f,\"Hum\":%.1f}",
             temp, hum);
    return true;
}
