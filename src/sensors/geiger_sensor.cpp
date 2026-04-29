#include "geiger_sensor.h"
#include "../logger.h"
#include <Arduino.h>

#define GEIGER_ISR_ATTR IRAM_ATTR

static volatile uint32_t s_pulseCount = 0;

static void GEIGER_ISR_ATTR geigerISR() {
    s_pulseCount++;
}

bool GeigerSensor::begin(int, int, int, int, int) {
    _ready = false;
    if (_pin < 0) {
        logMessage("warn", "Geiger: GPIO pin not configured");
        return false;
    }
    s_pulseCount = 0;
    pinMode(_pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(_pin), geigerISR, FALLING);
    _lastReadMs = millis();
    _ready = true;
    logMessageFmt("info", "Geiger counter on GPIO%d", _pin);
    return true;
}

bool GeigerSensor::read(SensorReading& r) {
    if (!_ready) return false;

    uint32_t now     = millis();
    uint32_t elapsed = now - _lastReadMs;
    if (elapsed < 60000UL) return false;  // need at least 1 minute for a valid CPM

    noInterrupts();
    uint32_t count = s_pulseCount;
    s_pulseCount   = 0;
    interrupts();

    _lastReadMs = now;

    float cpm = (float)count / ((float)elapsed / 60000.0f);

    logMessageFmt("debug", "Geiger: count=%d elapsed=%ds CPM=%.1f", count, elapsed / 1000, cpm);

    r.msgType = msgType();
    snprintf(r.data, sizeof(r.data), "{\"Count\":%.1f}", cpm);
    return true;
}
