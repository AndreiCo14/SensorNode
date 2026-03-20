#include "scd4x_sensor.h"
#include "../logger.h"
#include <Wire.h>
#include <SensirionI2cScd4x.h>

static SensirionI2cScd4x scd4x;

bool Scd4xSensor::begin(int, int, int, int, int) {
    _ready = false;
    scd4x.begin(Wire, 0x62);

    // Stop any running measurement before (re-)init
    scd4x.stopPeriodicMeasurement();
    delay(500);

    uint16_t err = scd4x.startPeriodicMeasurement();
    if (err) {
        logMessage("SCD4x: startPeriodicMeasurement error " + String(err), "warn");
        return false;
    }
    _ready = true;
    logMessage("SCD4x OK (0x62)", "info");
    return true;
}

bool Scd4xSensor::isReady() { return _ready; }

bool Scd4xSensor::read(SensorReading& r) {
    if (!_ready) return false;

    bool dataReady = false;
    uint16_t err = scd4x.getDataReadyStatus(dataReady);
    if (err || !dataReady) return false;

    uint16_t co2 = 0;
    float    temp = 0.0f, hum = 0.0f;
    err = scd4x.readMeasurement(co2, temp, hum);
    if (err) {
        logMessage("SCD4x read error " + String(err), "warn");
        return false;
    }

    _lastTemp = temp;
    _lastHum  = hum;
    r.msgType = msgType();
    snprintf(r.data, sizeof(r.data),
             "{\"CO2\":%u,\"Temp\":%.2f,\"Hum\":%.1f}", co2, temp, hum);
    return true;
}
