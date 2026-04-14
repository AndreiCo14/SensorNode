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
    uint16_t reconnIntervalS;   // seconds between reconnect attempts (default 5)
};

struct HwConfig {
    int8_t   i2c_sda;
    int8_t   i2c_scl;
    int8_t   uart_rx;
    int8_t   uart_tx;
    int8_t   onewire;
    int8_t   led_pin;
    int8_t   pin5v;
    // Controlled GPIOs: key in JSON is "gpio<pin>", value is mode string.
    // gpio_pin[i] == -1 marks an unused slot.
    static const uint8_t GPIO_CTRL_MAX = 8;
    int8_t gpio_pin[GPIO_CTRL_MAX];
    char   gpio_mode[GPIO_CTRL_MAX][8];  // "follow", "invert", "on", "off"
    uint8_t gpio_count;
    uint16_t intervalSec;
    // Operational params — persisted so they survive reboots
    uint16_t teleIntervalM;
    int8_t   sampleNum;
    uint16_t onTime;
    bool     deepSleep;
    // Set to true after a provisioning config has been successfully applied
    bool     provisioned;
};

// ─── Globals ──────────────────────────────────────────────────────────────────
extern JsonDocument sensorSetupData;    // /sensorsetup.json (enable/disable)
extern SemaphoreHandle_t sensorSetupMutex;
extern bool lfsReady;

// ─── Init ─────────────────────────────────────────────────────────────────────
bool storageInit();

// ─── WiFi ─────────────────────────────────────────────────────────────────────
bool loadWifiCreds(char* ssid, size_t ssidLen, char* pass, size_t passLen,
                   char* ssid2, size_t ssid2Len, char* pass2, size_t pass2Len);
bool saveWifiCreds(const char* ssid, const char* pass,
                   const char* ssid2, const char* pass2);
void clearWifiCreds();

// ─── MQTT config ──────────────────────────────────────────────────────────────
bool loadMqttConfig(MqttConfig& cfg);
bool saveMqttConfig(const MqttConfig& cfg);

// ─── Hardware config ──────────────────────────────────────────────────────────
bool loadHwConfig(HwConfig& cfg);
bool saveHwConfig(const HwConfig& cfg);

// ─── Sensor setup (enable/disable per type) ───────────────────────────────────
bool loadSensorSetup();
bool saveSensorSetup();

// ─── Feature flags ────────────────────────────────────────────────────────────
#define FEATURES_CONF_PATH  "/features.json"

struct FeatureFlags {
    bool web;    // HTTP server + web UI (port 80)
    bool wsLog;  // WebSocket log server (port 81)
};

bool loadFeatures(FeatureFlags& f);
bool saveFeatures(const FeatureFlags& f);

