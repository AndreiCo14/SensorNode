#pragma once

#include <Arduino.h>

void logMessage(const char* level, const char* message);
void logMessageFmt(const char* level, const char* format, ...);
void setDebugLog(bool en);
bool getDebugLog();
void setMaintenanceMode(bool en);
bool getMaintenanceMode();
void setDeepSleepMode(bool en);
bool getDeepSleepMode();
void setIgnoreCmdMode(bool en);
bool getIgnoreCmdMode();
void setNarodmonMode(bool en);
bool getNarodmonMode();
void loggerSetWsEnabled(bool en);
void loggerTask(void* pvParameters);
void broadcast_maintenance(bool maintenance);
void broadcast_deepSleep(bool deepSleep);
void broadcast_ignoreCmd(bool ignoreCmd);
void broadcast_narodmon(bool narodmon);
void broadcastTeleInterval(uint16_t teleIntervalM);
void broadcastOnTime(uint16_t onTimeSec);
void broadcastFsList(const String& json);
void broadcastFsContent(const String& content);

#ifdef ESP8266
void loggerInit();
void loggerProcess();
#endif
