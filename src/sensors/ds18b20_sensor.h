#pragma once
#include "sensor_base.h"

class Ds18b20Sensor : public SensorBase {
public:
    bool begin(int i2c_sda, int i2c_scl, int uart_rx, int uart_tx, int onewire_pin) override;
    bool isReady() override;
    bool read(SensorReading& r) override;
    const char* type() const override { return "ds18b20"; }
    uint8_t msgType() const override { return 201; }
private:
    bool _ready = false;
    int  _pin   = -1;
};
