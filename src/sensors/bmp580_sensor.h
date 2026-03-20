#pragma once
#include "sensor_base.h"

class Bmp580Sensor : public SensorBase {
public:
    bool begin(int i2c_sda, int i2c_scl, int uart_rx, int uart_tx, int onewire_pin) override;
    bool isReady()  override;
    bool read(SensorReading& r) override;
    const char* type()    const override { return "bmp580"; }
    uint8_t     msgType() const override { return 235; }
    uint8_t     i2cAddr() const override { return _addr; }
    void        setAddr(uint8_t a) override { _addr = a; }
private:
    bool    _ready = false;
    uint8_t _addr  = 0x46;
};
