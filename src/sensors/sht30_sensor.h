#pragma once
#include "sensor_base.h"

class Sht30Sensor : public SensorBase {
public:
    bool begin(int i2c_sda, int i2c_scl, int uart_rx, int uart_tx, int onewire_pin) override;
    bool isReady() override;
    bool read(SensorReading& r) override;
    const char* type() const override { return "sht30"; }
    uint8_t msgType() const override { return 235; }
private:
    bool _ready = false;
    uint8_t _addr = 0x44;
public:
    void setAddr(uint8_t addr) { _addr = addr; }
};
