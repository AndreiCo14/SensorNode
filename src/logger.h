#pragma once

#include <Arduino.h>

void logMessage(const char* message, const char* level = "info");
void logMessage(const String& message, const char* level = "info");
void setDebugLog(bool en);
bool getDebugLog();
void setMaintenanceMode(bool en);
bool getMaintenanceMode();
void loggerSetWsEnabled(bool en);
void loggerTask(void* pvParameters);
void broadcastButtonState(bool maintenance, bool deepSleep);

#ifdef ESP8266
void loggerInit();
void loggerProcess();
#endif
