#include "sht4x_sensor.h"
#include "../logger.h"
#include <Wire.h>
#include <Adafruit_SHT4x.h>

static Adafruit_SHT4x sht4x;

bool Sht4xSensor::begin(int, int, int, int, int) {
    _ready = false;
    if (!sht4x.begin(&Wire)) {
        logMessageFmt("warn", "SHT4x not found at 0x%X", _addr);
        return false;
    }
    sht4x.setPrecision(SHT4X_HIGH_PRECISION);
    sht4x.setHeater(SHT4X_NO_HEATER);
    _ready = true;
    logMessageFmt("info", "SHT4x OK (0x%X)", _addr);
    return true;
}

bool Sht4xSensor::isReady() { return _ready; }

bool Sht4xSensor::read(SensorReading& r) {
    if (!_ready) return false;

    sensors_event_t humidity, temp;
    if (!sht4x.getEvent(&humidity, &temp)) {
        logMessage("warn", "SHT4x read failed");
        return false;
    }

    _lastTemp = temp.temperature;
    _lastHum  = humidity.relative_humidity;
    r.msgType = msgType();
    snprintf(r.data, sizeof(r.data),
             "{\"Temp\":%.2f,\"Hum\":%.1f}",
             _lastTemp, _lastHum);
    return true;
}
