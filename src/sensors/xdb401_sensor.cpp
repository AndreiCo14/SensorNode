#include "xdb401_sensor.h"
#include "../logger.h"
#include <Wire.h>
#include <Arduino.h>

static bool readReg(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

static bool measure(uint8_t addr, float fullscale_pa, float& press_hpa, float& temp_c) {
    // Trigger measurement
    Wire.beginTransmission(addr);
    Wire.write(0x30);
    Wire.write(0x0A);
    if (Wire.endTransmission() != 0) return false;
    delay(50);

    // Read pressure (3 bytes, signed 24-bit)
    uint8_t pb[3];
    if (!readReg(addr, 0x06, pb, 3)) return false;
    uint32_t raw_p = ((uint32_t)pb[0] << 16) | ((uint32_t)pb[1] << 8) | pb[2];
    float p_norm = (raw_p > 8388608UL)
                 ? ((float)(int32_t)(raw_p - 16777216UL)) / 8388608.0f
                 : (float)raw_p / 8388608.0f;
    press_hpa = p_norm * (fullscale_pa / 100.0f);  // Pa → hPa

    // Read temperature (2 bytes, signed 16-bit)
    uint8_t tb[2];
    if (!readReg(addr, 0x09, tb, 2)) return false;
    uint16_t raw_t = ((uint16_t)tb[0] << 8) | tb[1];
    temp_c = (raw_t > 32768U)
           ? ((float)(int16_t)(raw_t - 65536U)) / 256.0f
           : (float)raw_t / 256.0f;

    return true;
}

bool Xdb401Sensor::begin(int, int, int, int, int) {
    _ready = false;
    float p, t;
    if (!measure(_addr, _fullscale_pa, p, t)) {
        logMessageFmt("warn", "XDB401 not found at 0x%X", _addr);
        return false;
    }
    _ready = true;
    logMessageFmt("info", "XDB401 OK (0x%X)", _addr);
    return true;
}

bool Xdb401Sensor::isReady() { return _ready; }

bool Xdb401Sensor::read(SensorReading& r) {
    if (!_ready) return false;

    float press_hpa, temp_c;
    if (!measure(_addr, _fullscale_pa, press_hpa, temp_c)) {
        logMessageFmt("warn", "XDB401 read failed");
        return false;
    }

    r.msgType = msgType();
    snprintf(r.data, sizeof(r.data),
             "{\"Temp\":%.2f,\"Press\":%.2f}", temp_c, press_hpa);
    return true;
}
