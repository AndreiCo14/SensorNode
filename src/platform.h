#pragma once
// ─── Platform abstraction: ESP8266 vs ESP32 ──────────────────────────────────

#ifdef ESP8266
#  include "compat_esp8266.h"
#  include <ESP8266WiFi.h>
#  define OTA_ERROR_STRING()  Update.getErrorString()
#  define OTA_BEGIN(size)     Update.begin(size)
#  include <ESP8266WebServer.h>
#  include <WiFiClientSecure.h>    // BearSSL::WiFiClientSecure
#  include <ESP8266httpUpdate.h>
   typedef ESP8266WebServer WebServerT;
#  define DEVICE_RESTART()         ESP.restart()
#  define FREE_HEAP()              ESP.getFreeHeap()
#  define CREATE_TASK_CORE(fn, name, stack, param, prio, handle, core) \
       xTaskCreate(fn, name, stack, param, prio, handle)
   static inline uint32_t getChipId() { return ESP.getChipId(); }
#else
#  include <WiFi.h>
#  include <WiFiClientSecure.h>
#  include <WebServer.h>
#  include <HTTPClient.h>
#  include <Update.h>
#  include "esp_system.h"
   typedef WebServer WebServerT;
#  define OTA_ERROR_STRING()  Update.errorString()
#  define OTA_BEGIN(size)     Update.begin(UPDATE_SIZE_UNKNOWN)
#  define DEVICE_RESTART()         esp_restart()
#  define FREE_HEAP()              esp_get_minimum_free_heap_size()
#  define CREATE_TASK_CORE(fn, name, stack, param, prio, handle, core) \
       xTaskCreatePinnedToCore(fn, name, stack, param, prio, handle, core)
   static inline uint32_t getChipId() {
       uint32_t id = 0;
       for (int i = 0; i < 17; i += 8)
           id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xFF) << i;
       return id;
   }
#endif
