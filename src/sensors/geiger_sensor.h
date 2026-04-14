#pragma once
#include "sensor_base.h"

class GeigerSensor : public SensorBase {
public:
    bool        begin(int i2c_sda, int i2c_scl,
                      int uart_rx,  int uart_tx,
                      int onewire_pin) override;
    bool        isReady() override { return _ready; }
    bool        read(SensorReading& r) override;
    const char* type()    const override { return "geiger"; }
    uint8_t     msgType() const override { return 210; }
    void        setPin(int8_t pin) { _pin = pin; }

private:
    bool     _ready      = false;
    int8_t   _pin        = -1;
    uint32_t _lastReadMs = 0;
};
