#include "logger.h"
#include "config.h"
#include "queues.h"
#include "system_state.h"
#include <ArduinoJson.h>
#include <WebSocketsServer.h>

static WebSocketsServer wsServer(81);
static bool wsStarted = false;
static bool debugLogEnabled = false;
static bool maintenanceModeEnabled = false;
static bool s_wsEnabled = true;

void setDebugLog(bool en) { debugLogEnabled = en; }
bool getDebugLog()        { return debugLogEnabled; }
void setMaintenanceMode(bool en) { maintenanceModeEnabled = en; }
bool getMaintenanceMode()        { return maintenanceModeEnabled; }
void loggerSetWsEnabled(bool en) { s_wsEnabled = en; }

static LogEntry ringBuffer[LOG_RING_SIZE];
static size_t   ringIndex = 0;
static SemaphoreHandle_t ringMutex = NULL;

void logMessage(const char* message, const char* level) {
    if (!logQueue) return;
    if (strcmp(level, "debug") == 0 && !debugLogEnabled) return;

    LogEntry entry;
    entry.timestamp = millis();
    strncpy(entry.message, message, LOG_MSG_MAX_LEN - 1);
    entry.message[LOG_MSG_MAX_LEN - 1] = '\0';
    strncpy(entry.level, level, sizeof(entry.level) - 1);
    entry.level[sizeof(entry.level) - 1] = '\0';

    xQueueSend(logQueue, &entry, 0);
    Serial.printf("[%s] %s\r\n", level, message);
}

void logMessage(const String& message, const char* level) {
    logMessage(message.c_str(), level);
}

static String serializeEntry(const LogEntry& entry) {
    JsonDocument doc;
    doc["t"] = entry.timestamp;
    doc["m"] = entry.message;
    doc["l"] = entry.level;
    String out;
    serializeJson(doc, out);
    return out;
}

static void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_CONNECTED) {
        if (ringMutex && xSemaphoreTake(ringMutex, pdMS_TO_TICKS(100))) {
            for (size_t i = 0; i < LOG_RING_SIZE; i++) {
                size_t idx = (ringIndex + i) % LOG_RING_SIZE;
                if (ringBuffer[idx].timestamp > 0) {
                    String msg = serializeEntry(ringBuffer[idx]);
                    wsServer.sendTXT(num, msg);
                }
            }
            xSemaphoreGive(ringMutex);
        }
    }
}

void loggerTask(void* pvParameters) {
    ringMutex = xSemaphoreCreateMutex();
    if (s_wsEnabled) {
        wsServer.begin();
        wsServer.onEvent(onWsEvent);
        wsStarted = true;
    }

    LogEntry entry;
    for (;;) {
        if (wsStarted) wsServer.loop();
        if (xQueueReceive(logQueue, &entry, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(50))) {
                ringBuffer[ringIndex] = entry;
                ringIndex = (ringIndex + 1) % LOG_RING_SIZE;
                xSemaphoreGive(ringMutex);
            }
            if (wsStarted) {
                String msg = serializeEntry(entry);
                wsServer.broadcastTXT(msg);
            }
        }
    }
}

#ifdef ESP8266
void loggerInit() {
    ringMutex = xSemaphoreCreateMutex();
    if (s_wsEnabled) {
        wsServer.begin();
        wsServer.onEvent(onWsEvent);
        wsStarted = true;
    }
}

void loggerProcess() {
    if (wsStarted) wsServer.loop();
    LogEntry entry;
    if (xQueueReceive(logQueue, &entry, 0) == pdTRUE) {
        if (xSemaphoreTake(ringMutex, 0)) {
            ringBuffer[ringIndex] = entry;
            ringIndex = (ringIndex + 1) % LOG_RING_SIZE;
            xSemaphoreGive(ringMutex);
        }
        if (wsStarted) {
            String msg = serializeEntry(entry);
            wsServer.broadcastTXT(msg);
        }
    }
}
#endif
