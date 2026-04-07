#pragma once
#include <stdint.h>

// Enter deep sleep for the given number of seconds.
// This function never returns — the device resets on wakeup.
// Caller must flush any pending MQTT/log output before calling.
//
// ESP8266: requires GPIO16 connected to RST for timer wakeup.
// ESP32:   uses esp_sleep timer wakeup.
void enterDeepSleep(uint32_t seconds);
