#pragma once
#include "sensor_base.h"

class Bme280Sensor : public SensorBase {
public:
    bool begin(int i2c_sda, int i2c_scl, int uart_rx, int uart_tx, int onewire_pin) override;
    bool isReady() override;
    bool read(SensorReading& r) override;
    const char* type() const override { return "bme280"; }
    uint8_t msgType() const override { return 202; }
private:
    bool _ready = false;
    uint8_t _addr = 0x76;
public:
    void setAddr(uint8_t addr) { _addr = addr; }
};
