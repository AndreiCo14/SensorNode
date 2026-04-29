#include "ds18b20_sensor.h"
#include "../logger.h"
#include <OneWire.h>
#include <DallasTemperature.h>

static OneWire*          ow  = nullptr;
static DallasTemperature dt;

bool Ds18b20Sensor::begin(int, int, int, int, int onewire_pin) {
    _ready = false;
    if (onewire_pin < 0) {
        logMessage("warn", "DS18B20: no 1-Wire pin configured");
        return false;
    }
    _pin = onewire_pin;
    if (ow) delete ow;
    ow = new OneWire(_pin);
    dt.setOneWire(ow);
    dt.begin();
    if (dt.getDeviceCount() == 0) {
        logMessageFmt("warn", "DS18B20: no devices on pin %d", _pin);
        return false;
    }
    _ready = true;
    logMessageFmt("info", "DS18B20 OK (%d device(s) on pin %d)", dt.getDeviceCount(), _pin);
    return true;
}

bool Ds18b20Sensor::isReady() { return _ready; }

bool Ds18b20Sensor::read(SensorReading& r) {
    if (!_ready) return false;

    dt.requestTemperatures();
    float temp = dt.getTempCByIndex(0);
    if (temp == DEVICE_DISCONNECTED_C) {
        logMessage("warn", "DS18B20 disconnected");
        return false;
    }

    r.msgType = msgType();
    snprintf(r.data, sizeof(r.data), "{\"Temp\":%.2f}", temp);
    return true;
}
