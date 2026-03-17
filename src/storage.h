#pragma once

#ifdef ESP8266
#  include "compat_esp8266.h"
#else
#  include <freertos/FreeRTOS.h>
#  include <freertos/semphr.h>
#endif
#include <ArduinoJson.h>

// ─── Structured config types ──────────────────────────────────────────────────

struct MqttConfig {
    char    broker[64];
    uint16_t port;
    char    prefix[32];
    bool    tls;
};

struct HwConfig {
    int8_t  i2c_sda;
    int8_t  i2c_scl;
    int8_t  uart_rx;
    int8_t  uart_tx;
    int8_t  onewire;
    int8_t  led_pin;
    uint16_t intervalSec;
};

// ─── Globals ──────────────────────────────────────────────────────────────────
extern JsonDocument sensorConfData;     // /sensorconf.json  (label mapping)
extern JsonDocument sensorSetupData;    // /sensorsetup.json (enable/disable)
extern SemaphoreHandle_t sensorConfMutex;
extern SemaphoreHandle_t sensorSetupMutex;
extern bool lfsReady;

// ─── Init ─────────────────────────────────────────────────────────────────────
bool storageInit();

// ─── WiFi ─────────────────────────────────────────────────────────────────────
bool loadWifiCreds(char* ssid, size_t ssidLen, char* pass, size_t passLen,
                   char* ssid2, size_t ssid2Len, char* pass2, size_t pass2Len);
bool saveWifiCreds(const char* ssid, const char* pass,
                   const char* ssid2, const char* pass2);

// ─── MQTT config ──────────────────────────────────────────────────────────────
bool loadMqttConfig(MqttConfig& cfg);
bool saveMqttConfig(const MqttConfig& cfg);

// ─── Hardware config ──────────────────────────────────────────────────────────
bool loadHwConfig(HwConfig& cfg);
bool saveHwConfig(const HwConfig& cfg);

// ─── Sensor setup (enable/disable per type) ───────────────────────────────────
bool loadSensorSetup();
bool saveSensorSetup();

// ─── Sensor label config (display names) ─────────────────────────────────────
bool loadSensorConf();
bool saveSensorConf();
