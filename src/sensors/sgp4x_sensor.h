#pragma once
#include "sensor_base.h"
#include <SensirionI2CSgp41.h>
#include <VOCGasIndexAlgorithm.h>
#include <NOxGasIndexAlgorithm.h>

class Sgp4xSensor : public SensorBase {
public:
    bool begin(int i2c_sda, int i2c_scl, int uart_rx, int uart_tx, int onewire_pin) override;
    bool isReady()  override;
    bool read(SensorReading& r) override;
    const char* type()    const override { return "sgp4x"; }
    uint8_t     msgType() const override { return 235; }
    uint8_t     i2cAddr() const override { return 0x59; }

    // Provide a T/H source for compensation (wired by sensor_manager after init)
    void setCompensationSource(SensorBase* src) { _thSrc = src; }

private:
    bool _ready = false;

    enum SgpMode { MODE_UNKNOWN, MODE_SGP40, MODE_SGP41 };
    SgpMode _mode = MODE_UNKNOWN;

    SensirionI2CSgp41    _sgp;
    VOCGasIndexAlgorithm _vocAlgo;
    NOxGasIndexAlgorithm _noxAlgo;
    SensorBase*          _thSrc = nullptr;

    // Convert °C / %RH to Sensirion compensation ticks
    static uint16_t toTempTicks(float t) {
        return (uint16_t)((t + 45.0f) * 65535.0f / 175.0f);
    }
    static uint16_t toRhTicks(float rh) {
        return (uint16_t)(rh * 65535.0f / 100.0f);
    }
};
