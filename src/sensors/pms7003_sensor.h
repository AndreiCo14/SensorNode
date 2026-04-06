#pragma once

#include "sensor_base.h"

// ─── PMS7003 particulate matter sensor ───────────────────────────────────────
// UART0 (GPIO1 TX, GPIO3 RX) for frame reception.
// Power is controlled via the board-level 5V pin (HwConfig.pin5v).
// Optionally, the PMS7003 SET pin can be driven via set_pin in sensorsetup.json:
//   set_pin=-1 (default): not connected — SET floats high or is tied, 5V rail
//                         controls the sensor entirely.
//   set_pin=N, set_inverted=true: N drives an inverting FET → SET
//                         (LOW on GPIO → SET=HIGH = run; HIGH → SET=LOW = sleep)
//   set_pin=N, set_inverted=false: N drives SET directly.
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
    void tick(uint32_t nextReadMs) override;

    const char* type()    const override { return "pms7003"; }
    uint8_t     msgType() const override { return 240; }

    // Called by sensor_manager before begin(). pwrPin comes from HwConfig.pin5v;
    // setPin=-1 means SET is not connected (default).
    void setPins(int8_t pwrPin, int8_t setPin, bool setInverted);

private:
    enum class State : uint8_t { IDLE, WARMING, DATA_READY };

    State    _state       = State::IDLE;
    uint32_t _powerOnMs   = 0;

    int8_t   _pwrPin      = -1;   // 5V power enable pin (-1 = not used)
    int8_t   _setPin      = -1;   // SET pin (-1 = not connected)
    bool     _setInverted = true; // true = inverting FET between GPIO and SET

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
