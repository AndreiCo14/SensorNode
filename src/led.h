#pragma once
#include <stdint.h>

enum LedState {
    LED_OFF = 0,
    LED_AP,          // purple solid      — no WiFi creds, config mode
    LED_CONNECTING,  // blue pulse        — has creds, connecting to WiFi
    LED_CONNECTED,   // green pulse       — WiFi up, MQTT not connected
    LED_MQTT_OK,     // yellow solid 2min — WiFi + MQTT connected, then off
    LED_OTA,         // cyan solid        — OTA update in progress
    LED_ERROR,       // red solid         — error
};

void ledInit(int8_t pin);
void ledSetState(LedState state);
void ledUpdate();   // call periodically from task loop
