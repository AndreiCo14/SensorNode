#pragma once

void webSetEnabled(bool en);
void webTask(void* pvParameters);

#ifdef ESP8266
void webInit();
void webProcess();
#endif
