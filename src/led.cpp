#include "led.h"
#include <Adafruit_NeoPixel.h>

static Adafruit_NeoPixel* pixel = nullptr;
static LedState  curState       = LED_OFF;
static uint32_t  lastUpdate     = 0;
static uint32_t  stateEnterTime = 0;
static uint8_t   phase          = 0;
static bool      mqttOkExpired  = false;  // true after 2-min yellow completes

void ledInit(int8_t pin) {
    if (pin < 0) return;
    pixel = new Adafruit_NeoPixel(1, (uint8_t)pin, NEO_GRB + NEO_KHZ800);
    pixel->begin();
    pixel->show();
    curState = LED_OFF;
}

void ledSetState(LedState state) {
    // WiFi lost — allow yellow to show again on next MQTT connect
    if (state == LED_CONNECTING || state == LED_AP) mqttOkExpired = false;
    // Suppress LED_MQTT_OK if 2-min window already completed this session
    if (state == LED_MQTT_OK && mqttOkExpired) return;
    if (state == curState) return;  // don't reset timer if already in this state
    curState       = state;
    lastUpdate     = 0;
    stateEnterTime = millis();
    phase          = 0;
}

static void setColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!pixel) return;
    pixel->setPixelColor(0, pixel->Color(r, g, b));
    pixel->show();
}

// Triangle-wave pulse: returns 0–254 from phase 0–255
static uint8_t pulse(uint8_t ph) {
    return (ph < 128) ? ph * 2 : (255 - ph) * 2;
}

void ledUpdate() {
    if (!pixel) return;
    uint32_t now = millis();
    uint32_t dt  = now - lastUpdate;

    switch (curState) {

        case LED_OFF:
            setColor(0, 0, 0);
            lastUpdate = now;
            break;

        case LED_AP:
            // Purple solid — no WiFi credentials, waiting for config
            setColor(8, 0, 8);
            lastUpdate = now;
            break;

        case LED_CONNECTING:
            // Blue pulse — actively connecting to WiFi
            if (dt > 8) {
                phase++;
                uint8_t b = pulse(phase) / 10 + 3;  // 3–28
                setColor(0, 0, b);
                lastUpdate = now;
            }
            break;

        case LED_CONNECTED:
            // Green pulse — WiFi up, MQTT not connected
            if (dt > 8) {
                phase++;
                uint8_t g = pulse(phase) / 10 + 3;  // 3–28
                setColor(0, g, 0);
                lastUpdate = now;
            }
            break;

        case LED_MQTT_OK:
            // Yellow solid for 2 minutes, then off
            if (now - stateEnterTime > 120000UL) {
                mqttOkExpired = true;
                curState = LED_OFF;
                setColor(0, 0, 0);
            } else {
                setColor(12, 10, 0);
            }
            lastUpdate = now;
            break;

        case LED_OTA:
            // Cyan solid — OTA update in progress
            setColor(0, 12, 12);
            lastUpdate = now;
            break;

        case LED_ERROR:
            // Red solid
            setColor(20, 0, 0);
            lastUpdate = now;
            break;
    }
}
