#include "bme280_sensor.h"
#include "../logger.h"
#include <Wire.h>
#include <SparkFunBME280.h>

static BME280 bme;

bool Bme280Sensor::begin(int i2c_sda, int i2c_scl, int, int, int) {
    _ready = false;
#ifdef ESP8266
    Wire.begin(i2c_sda, i2c_scl);
#else
    Wire.begin(i2c_sda, i2c_scl);
#endif
    bme.setI2CAddress(_addr);
    if (!bme.beginI2C(Wire)) {
        logMessage("BME280 not found at 0x" + String(_addr, HEX), "warn");
        return false;
    }
    _ready = true;
    logMessage("BME280 OK (0x" + String(_addr, HEX) + ")", "info");
    return true;
}

bool Bme280Sensor::isReady() { return _ready; }

bool Bme280Sensor::read(SensorReading& r) {
    if (!_ready) return false;

    float temp  = bme.readTempC();
    float hum   = bme.readFloatHumidity();
    float press = bme.readFloatPressure() / 100.0f;  // Pa → hPa

    r.msgType = msgType();
    snprintf(r.data, sizeof(r.data),
             "{\"temp\":%.2f,\"hum\":%.1f,\"press\":%.2f}",
             temp, hum, press);
    return true;
}
