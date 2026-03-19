#pragma once

#include "sensor_base.h"

// ─── PMS7003 particulate matter sensor ───────────────────────────────────────
// ESP8266 only: UART0 (GPIO1 TX, GPIO3 RX), GPIO15 = 5V power enable,
// GPIO12 = inverse of GPIO15.
//
// Power cycle: sensor is powered on for onTime seconds (from SystemState)
// every teleIntervalM minutes.  Incoming frames (~1/s) are stored in a
// ring buffer; the last AVG_N (10) valid readings are averaged and
// published once onTime has elapsed.

class Pms7003Sensor : public SensorBase {
public:
    bool begin(int i2c_sda, int i2c_scl,
               int uart_rx, int uart_tx, int onewire_pin) override;
    bool isReady() override;
    bool read(SensorReading& r) override;
    bool tick() override;

    const char* type()    const override { return "pms7003"; }
    uint8_t     msgType() const override { return 240; }

private:
    enum class State : uint8_t { IDLE, WARMING, DATA_READY };

    State    _state       = State::IDLE;
    uint32_t _powerOnMs   = 0;
    uint32_t _lastCycleMs = 0;

    // Ring buffer of last AVG_N valid frames
    static const uint8_t AVG_N = 10;
    uint16_t _rb1[AVG_N], _rb25[AVG_N], _rb10[AVG_N];
    uint8_t  _rbHead  = 0;
    uint8_t  _rbCount = 0;

    // Averaged result ready for read()
    float _pm1 = 0, _pm25 = 0, _pm10 = 0;

    // Serial frame parse buffer
    uint8_t _buf[32];
    uint8_t _bufPos = 0;

    void powerOn();
    void powerOff();
    void parseSerial();
    bool validateFrame(const uint8_t* buf) const;
};
