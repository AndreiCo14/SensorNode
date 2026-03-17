#pragma once
#include <stdint.h>

// LED states
enum LedState {
    LED_OFF = 0,
    LED_BOOT,        // blue slow pulse
    LED_CONNECTING,  // cyan fast pulse
    LED_CONNECTED,   // green solid
    LED_MQTT_OK,     // green brief flash
    LED_ERROR,       // red solid
};

// Must be called after ledInit() with a valid pin
void ledInit(int8_t pin);
void ledSetState(LedState state);
void ledUpdate();   // call periodically from task loop
