#pragma once
#include "sensor_base.h"

class Htu21dSensor : public SensorBase {
public:
    bool begin(int i2c_sda, int i2c_scl, int uart_rx, int uart_tx, int onewire_pin) override;
    bool isReady()  override;
    bool read(SensorReading& r) override;
    const char* type()       const override { return "htu21d"; }
    uint8_t     msgType()    const override { return 235; }
    uint8_t     i2cAddr()    const override { return 0x40; }
    float       lastTemp()   const override { return _lastTemp; }
    float       lastHum()    const override { return _lastHum; }
    bool        providesTH() const override { return true; }
private:
    bool  _ready    = false;
    float _lastTemp = 25.0f;
    float _lastHum  = 50.0f;
};
