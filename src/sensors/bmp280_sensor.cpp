#include "bmp280_sensor.h"
#include "../logger.h"
#include <Wire.h>
#include <Adafruit_BMP280.h>

static Adafruit_BMP280 bmp280;

bool Bmp280Sensor::begin(int, int, int, int, int) {
    _ready = false;
    if (!bmp280.begin(_addr)) {
        logMessageFmt("warn", "BMP280 not found at 0x%X", _addr);
        return false;
    }
    _ready = true;
    logMessageFmt("info", "BMP280 OK (0x%X)", _addr);
    return true;
}

bool Bmp280Sensor::isReady() { return _ready; }

bool Bmp280Sensor::read(SensorReading& r) {
    if (!_ready) return false;

    float temp  = bmp280.readTemperature();
    float press = bmp280.readPressure() / 100.0f;  // Pa → hPa

    r.msgType = msgType();
    snprintf(r.data, sizeof(r.data),
             "{\"Temp\":%.2f,\"Press\":%.2f}", temp, press);
    return true;
}
