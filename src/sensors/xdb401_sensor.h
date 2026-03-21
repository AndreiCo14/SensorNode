#pragma once
#include "sensor_base.h"

// XDB401 — I2C digital pressure + temperature sensor
// Default I2C address: 0x7F, full-scale 500000 Pa (0.5 MPa).
// Override via JSON config: {"type":"xdb401","enabled":true,"fullscale_pa":1000000}
class Xdb401Sensor : public SensorBase {
public:
    bool begin(int i2c_sda, int i2c_scl, int uart_rx, int uart_tx, int onewire_pin) override;
    bool isReady()  override;
    bool read(SensorReading& r) override;
    const char* type()    const override { return "xdb401"; }
    uint8_t     msgType() const override { return 235; }
    uint8_t     i2cAddr() const override { return _addr; }
    void        setAddr(uint8_t a) override { _addr = a; }
    void        setFullscalePa(float pa)   { _fullscale_pa = pa; }
private:
    bool    _ready        = false;
    uint8_t _addr         = 0x7F;
    float   _fullscale_pa = 500000.0f;  // 0.5 MPa default
};
