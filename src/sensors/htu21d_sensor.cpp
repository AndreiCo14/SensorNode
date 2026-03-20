#include "htu21d_sensor.h"
#include "../logger.h"
#include <Wire.h>
#include <SparkFunHTU21D.h>

static HTU21D htu21d;

bool Htu21dSensor::begin(int, int, int, int, int) {
    _ready = false;
    htu21d.begin();
    _ready = true;
    logMessage("HTU21D OK (0x40)", "info");
    return true;
}

bool Htu21dSensor::isReady() { return _ready; }

bool Htu21dSensor::read(SensorReading& r) {
    if (!_ready) return false;

    float temp = htu21d.readTemperature();
    float hum  = htu21d.readHumidity();

    if (temp == ERROR_I2C_TIMEOUT || hum == ERROR_I2C_TIMEOUT ||
        temp == ERROR_BAD_CRC     || hum == ERROR_BAD_CRC) {
        logMessage("HTU21D read error", "warn");
        return false;
    }

    _lastTemp = temp;
    _lastHum  = hum;
    r.msgType = msgType();
    snprintf(r.data, sizeof(r.data),
             "{\"Temp\":%.2f,\"Hum\":%.1f}", temp, hum);
    return true;
}
