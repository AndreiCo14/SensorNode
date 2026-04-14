#include "pms7003_sensor.h"
#include "../system_state.h"
#include "../logger.h"
#include <Arduino.h>

static const uint16_t PMS_DEFAULT_ONTIME = 30;  // seconds

// ─── Frame parsing ────────────────────────────────────────────────────────────

bool Pms7003Sensor::validateFrame(const uint8_t* buf) const {
    uint16_t sum = 0;
    for (uint8_t i = 0; i < 30; i++) sum += buf[i];
    uint16_t check = ((uint16_t)buf[30] << 8) | buf[31];
    return sum == check;
}

void Pms7003Sensor::parseSerial() {
    while (Serial.available()) {
        uint8_t b = (uint8_t)Serial.read();
        if (_bufPos == 0) {
            if (b != 0x42) continue;
        } else if (_bufPos == 1) {
            if (b != 0x4D) { _bufPos = 0; continue; }
        }
        _buf[_bufPos++] = b;
        if (_bufPos < 32) continue;

        _bufPos = 0;
        if (!validateFrame(_buf)) continue;

        // Store into ring buffer (overwrites oldest when full)
        _rb1[_rbHead]  = ((uint16_t)_buf[4] << 8) | _buf[5];
        _rb25[_rbHead] = ((uint16_t)_buf[6] << 8) | _buf[7];
        _rb10[_rbHead] = ((uint16_t)_buf[8] << 8) | _buf[9];
        _rbHead = (_rbHead + 1) % AVG_N;
        if (_rbCount < AVG_N) _rbCount++;
    }
}

// ─── SensorBase interface ─────────────────────────────────────────────────────

bool Pms7003Sensor::begin(int, int, int, int, int) {
    _state    = State::IDLE;
    _rbHead   = 0;
    _rbCount  = 0;
    return true;
}

bool Pms7003Sensor::isReady() {
    return _state == State::DATA_READY;
}

bool Pms7003Sensor::read(SensorReading& r) {
    if (_state != State::DATA_READY) return false;
    r.msgType = msgType();
    snprintf(r.data, sizeof(r.data),
             "{\"PMS1\":%.1f,\"PMS25\":%.1f,\"PMS10\":%.1f}",
             _pm1, _pm25, _pm10);
    _state = State::IDLE;
    return true;
}

void Pms7003Sensor::tick(uint32_t nextReadMs) {
    switch (_state) {
    case State::IDLE: {
        uint16_t onTimeSec = STATE_GET(onTime);
        if (onTimeSec < PMS_DEFAULT_ONTIME) onTimeSec = PMS_DEFAULT_ONTIME;
        uint32_t now = millis();
        // Start warm-up when there is just enough time to complete before the next read
        if (now + (uint32_t)onTimeSec * 1000UL >= nextReadMs) {
            _rbHead    = 0;
            _rbCount   = 0;
            _bufPos    = 0;
            _powerOnMs = now;
            Serial.begin(9600);
            _state = State::WARMING;
        }
        return;
    }

    case State::WARMING: {
        parseSerial();
        uint16_t onTimeSec = STATE_GET(onTime);
        if (onTimeSec < PMS_DEFAULT_ONTIME) onTimeSec = PMS_DEFAULT_ONTIME;
        if (millis() - _powerOnMs >= (uint32_t)onTimeSec * 1000UL) {
            if (_rbCount > 0) {
                uint32_t s1 = 0, s25 = 0, s10 = 0;
                for (uint8_t i = 0; i < _rbCount; i++) {
                    s1  += _rb1[i];
                    s25 += _rb25[i];
                    s10 += _rb10[i];
                }
                _pm1  = (float)s1  / _rbCount;
                _pm25 = (float)s25 / _rbCount;
                _pm10 = (float)s10 / _rbCount;
                _state = State::DATA_READY;
            } else {
                logMessage("PMS7003: no frames after warmup", "warn");
                _state = State::IDLE;
            }
        }
        return;
    }

    case State::DATA_READY:
        return;
    }
}
