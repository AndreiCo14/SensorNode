#pragma once

#include "sensor_base.h"
#include "../storage.h"

#define MAX_SENSORS 8

// sensorTask — FreeRTOS task entry point
void sensorTask(void* pvParameters);

// Called from main after storage init to build the sensor list
void sensorsInit();

// Enable measurement cycles; call after MQTT startup handshake completes
void sensorsEnable();

// Like sensorsEnable() but schedules the first read at (onTime + 5) seconds
// from now, regardless of teleIntervalM. Used in deep sleep mode so the device
// reads once as soon as the PMS7003 is warm, then hands off to the sleep cycle.
void sensorsEnableDeepSleep();

// Number of sensors that successfully initialized
uint8_t sensorsActiveCount();

#ifdef ESP8266
void sensorProcess();
#endif
