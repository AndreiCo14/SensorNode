#pragma once

#include "sensor_base.h"
#include "../storage.h"

#define MAX_SENSORS 8

// sensorTask — FreeRTOS task entry point
void sensorTask(void* pvParameters);

// Called from main after storage init to build the sensor list
void sensorsInit();

// Free existing sensor instances, re-run sensorsInit(), and re-enable
// measurement cycles. Safe to call at runtime after sensor config changes.
void sensorsReinit();

// Enable measurement cycles; call after MQTT startup handshake completes
void sensorsEnable();

// Like sensorsEnable() but schedules the first read at (onTime + 5) seconds
// from now, regardless of teleIntervalM. Used in deep sleep mode so the device
// reads once as soon as the PMS7003 is warm, then hands off to the sleep cycle.
void sensorsEnableDeepSleep();

// Number of sensors that successfully initialized
uint8_t sensorsActiveCount();

// Fill doc with last known values per sensor type: {"bme280":{"temp":21.5,...},...}
void sensorGetLastValues(JsonDocument& out);

#ifdef ESP8266
void sensorProcess();
#endif
