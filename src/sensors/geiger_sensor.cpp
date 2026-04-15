#include "geiger_sensor.h"
#include "../logger.h"
#include <Arduino.h>

#ifdef ESP8266
#  define GEIGER_ISR_ATTR ICACHE_RAM_ATTR
#else
#  define GEIGER_ISR_ATTR IRAM_ATTR
#endif

static volatile uint32_t s_pulseCount = 0;

static void GEIGER_ISR_ATTR geigerISR() {
    s_pulseCount++;
}

bool GeigerSensor::begin(int, int, int, int, int) {
    _ready = false;
    if (_pin < 0) {
        logMessage("Geiger: GPIO pin not configured", "warn");
        return false;
    }
    s_pulseCount = 0;
    pinMode(_pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(_pin), geigerISR, FALLING);
    _lastReadMs = millis();
    _ready = true;
    logMessage("Geiger counter on GPIO" + String(_pin), "info");
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

    logMessage("Geiger: count=" + String(count) + " elapsed=" + String(elapsed / 1000) + "s CPM=" + String(cpm, 1), "debug");

    r.msgType = msgType();
    snprintf(r.data, sizeof(r.data), "{\"Count\":%.1f}", cpm);
    return true;
}
