#include "led.h"
#include <Adafruit_NeoPixel.h>

static Adafruit_NeoPixel* pixel = nullptr;
static LedState  curState       = LED_OFF;
static uint32_t  lastUpdate     = 0;
static uint32_t  stateEnterTime = 0;
static uint8_t   phase          = 0;

void ledInit(int8_t pin) {
    if (pin < 0) return;
    pixel = new Adafruit_NeoPixel(1, (uint8_t)pin, NEO_GRB + NEO_KHZ800);
    pixel->begin();
    pixel->show();
    curState = LED_OFF;
}

void ledSetState(LedState state) {
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
                curState = LED_OFF;
            } else {
                setColor(12, 10, 0);
            }
            lastUpdate = now;
            break;

        case LED_ERROR:
            // Red solid
            setColor(20, 0, 0);
            lastUpdate = now;
            break;
    }
}
