#pragma once

void uplinkTask(void* pvParameters);
void sendTelemetry();

#ifdef ESP8266
void uplinkInit();
void uplinkProcess();
#endif
