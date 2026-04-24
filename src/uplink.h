#pragma once

void uplinkTask(void* pvParameters);
void sendTelemetry();
bool getDeepSleepMode();

#ifdef ESP8266
void uplinkInit();
void uplinkProcess();
#endif
