#include "led.h"
#include <Adafruit_NeoPixel.h>

static Adafruit_NeoPixel* pixel = nullptr;
static LedState curState = LED_OFF;
static uint32_t lastUpdate = 0;
static uint8_t  phase = 0;

void ledInit(int8_t pin) {
    if (pin < 0) return;
    pixel = new Adafruit_NeoPixel(1, (uint8_t)pin, NEO_GRB + NEO_KHZ800);
    pixel->begin();
    pixel->show();
    curState = LED_BOOT;
}

void ledSetState(LedState state) {
    curState = state;
    lastUpdate = 0;  // force immediate update
}

static void setColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!pixel) return;
    pixel->setPixelColor(0, pixel->Color(r, g, b));
    pixel->show();
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

        case LED_BOOT:
            if (dt > 60) {
                phase = (phase + 1) & 0xFF;
                uint8_t b = (phase < 128) ? phase * 2 : (255 - phase) * 2;
                setColor(0, 0, b / 4);
                lastUpdate = now;
            }
            break;

        case LED_CONNECTING:
            if (dt > 100) {
                phase ^= 0xFF;
                setColor(0, phase ? 8 : 0, phase ? 8 : 0);
                lastUpdate = now;
            }
            break;

        case LED_CONNECTED:
            setColor(0, 8, 0);
            lastUpdate = now;
            break;

        case LED_MQTT_OK:
            if (dt < 200) {
                setColor(0, 20, 0);
            } else {
                setColor(0, 8, 0);
                curState = LED_CONNECTED;
            }
            break;

        case LED_ERROR:
            if (dt > 500) {
                phase ^= 0xFF;
                setColor(phase ? 20 : 0, 0, 0);
                lastUpdate = now;
            }
            break;
    }
}
