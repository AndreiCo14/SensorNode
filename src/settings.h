#pragma once

// Default WiFi credentials — overridden at runtime by /wifi.json
#ifndef DEFAULT_WIFI_SSID
#  define DEFAULT_WIFI_SSID     ""
#endif
#ifndef DEFAULT_WIFI_PASS
#  define DEFAULT_WIFI_PASS     ""
#endif

// Default MQTT settings — overridden at runtime by /mqtt.json
#ifndef DEFAULT_MQTT_BROKER
#  define DEFAULT_MQTT_BROKER   "mq.airmq.cc"
#endif
#ifndef DEFAULT_MQTT_PORT
#  define DEFAULT_MQTT_PORT     18883
#endif
#ifndef DEFAULT_MQTT_PREFIX
#  define DEFAULT_MQTT_PREFIX   "Sensors/"
#endif
#ifndef DEFAULT_MQTT_TLS
#  define DEFAULT_MQTT_TLS      false
#endif

// MQTT topic suffixes
#define TOPIC_DATA_SUFFIX              "/sensordata"
#define TOPIC_TELE_SUFFIX              "/telemetry"
#define TOPIC_START_SUFFIX             "/Start"
#define TOPIC_CMD_SUFFIX               "/cmd"
#define TOPIC_STATUS_SUFFIX            "/Status"
#define TOPIC_PROVISION_SUFFIX         "/provision"
#define TOPIC_PROVISION_BACKUP_SUFFIX  "/provision/backup"

// Firmware build tag (overridden in platformio.ini per environment)
#ifndef FW_BUILD
#  define FW_BUILD  "dev"
#endif

// OTA version check URL (overridden in platformio.ini per environment)
#ifndef OTA_VERSION_URL
#  define OTA_VERSION_URL   ""
#endif
