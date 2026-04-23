#pragma once

#include <Arduino.h>

void logMessage(const char* message, const char* level = "info");
void logMessage(const String& message, const char* level = "info");
void setDebugLog(bool en);
bool getDebugLog();
void setMaintenanceMode(bool en);
bool getMaintenanceMode();
void setDeepSleepMode(bool en);
bool getDeepSleepMode();
void setIgnoreCmdMode(bool en);
bool getIgnoreCmdMode();
void loggerSetWsEnabled(bool en);
void loggerTask(void* pvParameters);
void broadcast_maintenance(bool maintenance);
void broadcast_deepSleep(bool deepSleep);
void broadcast_ignoreCmd(bool ignoreCmd);
void broadcastTeleInterval(uint16_t teleIntervalM);

#ifdef ESP8266
void loggerInit();
void loggerProcess();
#endif
