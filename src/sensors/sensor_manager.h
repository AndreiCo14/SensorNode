#pragma once

#include "sensor_base.h"
#include "../storage.h"

#define MAX_SENSORS 8

// sensorTask — FreeRTOS task entry point
void sensorTask(void* pvParameters);

// Called from main after storage init to build the sensor list
void sensorsInit();

// Number of sensors that successfully initialized
uint8_t sensorsActiveCount();

#ifdef ESP8266
void sensorProcess();
#endif
