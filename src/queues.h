#pragma once

// FreeRTOS include paths differ between platforms
#ifdef ESP8266
#  include "compat_esp8266.h"
#else
#  include <freertos/FreeRTOS.h>
#  include <freertos/queue.h>
#  include <freertos/task.h>
#endif
#include "config.h"

// ─── Sensor reading — produced by sensorTask, consumed by uplinkTask ──────────
struct SensorReading {
    uint32_t deviceId;              // chipId
    uint32_t time;                  // epoch seconds (filled by uplinkTask)
    uint16_t count;                 // reading sequence number
    uint16_t mV;                    // VCC in mV (0 if unknown)
    int8_t   rssi;                  // 0 for local sensors
    uint8_t  msgType;               // 202=BME280, 235=SHT30, 201=DS18B20, ...
    bool     immTX;                 // flush batch immediately
    char     data[SENSOR_DATA_MAX]; // JSON: {"temp":21.5,"hum":65.0,"press":1013.2}
};

// ─── MQTT command — produced by webTask/MQTT callback, consumed by uplinkTask ─
struct MqttCommand {
    char payload[512];
};

// ─── Log entry — produced by any task, consumed by loggerTask ────────────────
struct LogEntry {
    uint32_t timestamp;
    char     message[LOG_MSG_MAX_LEN];
    char     level[8];
};

// ─── Queue handles ────────────────────────────────────────────────────────────
extern QueueHandle_t sensorQueue;
extern QueueHandle_t cmdQueue;
extern QueueHandle_t logQueue;

// ─── Task handles ─────────────────────────────────────────────────────────────
extern TaskHandle_t sensorTaskHandle;
extern TaskHandle_t uplinkTaskHandle;
extern TaskHandle_t webTaskHandle;
extern TaskHandle_t loggerTaskHandle;
