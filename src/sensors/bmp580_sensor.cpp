#include "bmp580_sensor.h"
#include "../logger.h"
#include <Wire.h>
#include <SparkFun_BMP581_Arduino_Library.h>

static BMP581 bmp581;

bool Bmp580Sensor::begin(int, int, int, int, int) {
    _ready = false;
    if (bmp581.beginI2C(_addr, Wire) != BMP5_OK) {
        logMessage("BMP580 not found at 0x" + String(_addr, HEX), "warn");
        return false;
    }
    _ready = true;
    logMessage("BMP580 OK (0x" + String(_addr, HEX) + ")", "info");
    return true;
}

bool Bmp580Sensor::isReady() { return _ready; }

bool Bmp580Sensor::read(SensorReading& r) {
    if (!_ready) return false;

    bmp5_sensor_data data = {0, 0};
    if (bmp581.getSensorData(&data) != BMP5_OK) {
        logMessage("BMP580 read failed", "warn");
        return false;
    }

    r.msgType = msgType();
    snprintf(r.data, sizeof(r.data),
             "{\"Temp\":%.2f,\"Press\":%.2f}",
             data.temperature, data.pressure / 100.0f);  // Pa → hPa
    return true;
}
