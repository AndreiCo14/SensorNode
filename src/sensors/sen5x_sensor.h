#pragma once
#include "sensor_base.h"
#include <SensirionI2CSen5x.h>

class Sen5xSensor : public SensorBase {
public:
    bool        begin(int i2c_sda, int i2c_scl,
                      int uart_rx,  int uart_tx,
                      int onewire_pin) override;
    bool        isReady()    override { return _ready; }
    bool        read(SensorReading& r) override;
    const char* type()       const override { return "sen5x"; }
    uint8_t     msgType()    const override { return 211; }
    uint8_t     i2cAddr()    const override { return 0x69; }
    float       lastTemp()   const override { return _lastTemp; }
    float       lastHum()    const override { return _lastHum; }
    bool        providesTH() const override { return true; }

private:
    SensirionI2CSen5x _sen5x;
    bool     _ready    = false;
    bool     _hasNox   = false;   // true if SEN55 (NOx channel available)
    float    _lastTemp = 25.0f;
    float    _lastHum  = 50.0f;
};
