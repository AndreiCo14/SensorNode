#pragma once

#ifdef ESP8266
#  include "compat_esp8266.h"
#else
#  include <freertos/FreeRTOS.h>
#  include <freertos/semphr.h>
#endif
#include <stdint.h>

struct SystemState {
    // Uptime
    uint32_t startTime;         // millis()/1000 at boot
    int32_t  lastTeleSent;

    // Config
    uint16_t teleIntervalM;
    int8_t   sampleNum;
    uint16_t onTime;            // PMS7003 warmup/on duration (seconds)

    // Status
    bool     mqttConnected;
    bool     wifiConnected;
    bool     apMode;

    // Sensor info
    uint8_t  sensorsActive;     // count of sensors that initialized OK
    uint32_t readingCount;      // total readings published

    // Chip
    uint32_t chipId;
    char     sysname[17];       // "AirMQ_<chipId>"

    // Boot diagnostics
    uint8_t  resetReason;

    SemaphoreHandle_t mutex;
};

extern SystemState sysState;

#define STATE_LOCK()    xSemaphoreTake(sysState.mutex, portMAX_DELAY)
#define STATE_UNLOCK()  xSemaphoreGive(sysState.mutex)

#define STATE_GET(field) ({ \
    STATE_LOCK(); \
    auto _val = sysState.field; \
    STATE_UNLOCK(); \
    _val; \
})

#define STATE_SET(field, value) ({ \
    STATE_LOCK(); \
    sysState.field = (value); \
    STATE_UNLOCK(); \
})
