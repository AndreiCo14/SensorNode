#include "deep_sleep.h"
#include "logger.h"
#include "queues.h"
#include "platform.h"
#include <Arduino.h>
#ifdef ESP8266
#  include <ESP8266WiFi.h>
#else
#  include <WiFi.h>
#  include "esp_sleep.h"
#endif

void enterDeepSleep(uint32_t seconds) {
    logMessage("Deep sleep: " + String(seconds) + "s", "info");

#ifdef ESP8266
    // Flush log queue before blocking
    while (uxQueueMessagesWaiting(logQueue)) loggerProcess();
    delay(100);
#else
    vTaskDelay(pdMS_TO_TICKS(200));
#endif

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

#ifdef ESP8266
    // Requires GPIO16 connected to RST
    ESP.deepSleep((uint64_t)seconds * 1000000ULL);
#else
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    esp_deep_sleep_start();
#endif
    // unreachable
}
