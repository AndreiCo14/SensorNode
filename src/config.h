#pragma once

#include "board.h"

// ─── Queue sizes ──────────────────────────────────────────────────────────────
#define SENSOR_QUEUE_SIZE       10
#define CMD_QUEUE_SIZE          5
#define LOG_QUEUE_SIZE          16

// ─── Logging ──────────────────────────────────────────────────────────────────
#ifdef ESP8266
#define LOG_RING_SIZE           12   // reduced to save ~6 KB BSS on ESP8266
#else
#define LOG_RING_SIZE           50
#endif
#define LOG_MSG_MAX_LEN         160

// ─── Uplink / telemetry ───────────────────────────────────────────────────────
#define DEFAULT_SAMPLE_NUM          1
#define DEFAULT_TELE_INTERVAL_M     30
#define AP_WINDOW_MS                180000UL
#define WIFI_PRIMARY_TIMEOUT_MS     30000UL

// ─── Filesystem paths ─────────────────────────────────────────────────────────
#define WIFI_CONF_PATH      "/wifi.json"
#define MQTT_CONF_PATH      "/mqtt.json"
#define HW_CONF_PATH        "/hwconfig.json"
#define SENSOR_SETUP_PATH   "/sensorsetup.json"

// ─── Time ─────────────────────────────────────────────────────────────────────
#define MYTZ    "CET-1CEST,M3.5.0,M10.5.0/3"

// ─── Task stacks & priorities ─────────────────────────────────────────────────
#ifdef ESP8266
#  define TASK_STACK_SENSOR   2048
#  define TASK_STACK_UPLINK   3072
#  define TASK_STACK_WEB      3072
#  define TASK_STACK_LOGGER   2048
#else
#  define TASK_STACK_SENSOR   4096
#  define TASK_STACK_UPLINK   8192
#  define TASK_STACK_WEB      8192
#  define TASK_STACK_LOGGER   4096
#endif

#define TASK_PRIO_SENSOR    4
#define TASK_PRIO_UPLINK    3
#define TASK_PRIO_WEB       2
#define TASK_PRIO_LOGGER    1

// ─── SensorReading data buffer ────────────────────────────────────────────────
#define SENSOR_DATA_MAX     128
