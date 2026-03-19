#include "ds18b20_sensor.h"
#include "../logger.h"
#include <OneWire.h>
#include <DallasTemperature.h>

static OneWire*          ow  = nullptr;
static DallasTemperature dt;

bool Ds18b20Sensor::begin(int, int, int, int, int onewire_pin) {
    _ready = false;
    if (onewire_pin < 0) {
        logMessage("DS18B20: no 1-Wire pin configured", "warn");
        return false;
    }
    _pin = onewire_pin;
    if (ow) delete ow;
    ow = new OneWire(_pin);
    dt.setOneWire(ow);
    dt.begin();
    if (dt.getDeviceCount() == 0) {
        logMessage("DS18B20: no devices on pin " + String(_pin), "warn");
        return false;
    }
    _ready = true;
    logMessage("DS18B20 OK (" + String(dt.getDeviceCount()) + " device(s) on pin " + String(_pin) + ")", "info");
    return true;
}

bool Ds18b20Sensor::isReady() { return _ready; }

bool Ds18b20Sensor::read(SensorReading& r) {
    if (!_ready) return false;

    dt.requestTemperatures();
    float temp = dt.getTempCByIndex(0);
    if (temp == DEVICE_DISCONNECTED_C) {
        logMessage("DS18B20 disconnected", "warn");
        return false;
    }

    r.msgType = msgType();
    snprintf(r.data, sizeof(r.data), "{\"Temp\":%.2f}", temp);
    return true;
}
