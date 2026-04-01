#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "settings.h"
#include "queues.h"
#include "system_state.h"
#include "platform.h"
#include "storage.h"
#include "logger.h"
#include "led.h"
#include "uplink.h"
#include "webserver.h"
#include "sensors/sensor_manager.h"

#ifdef ESP8266
ADC_MODE(ADC_VCC);  // ESP.getVcc() reads the supply rail, not TOUT pin
#else
#  include "esp_system.h"
#endif

// ─── Queue & task handles ─────────────────────────────────────────────────────
QueueHandle_t sensorQueue    = NULL;
QueueHandle_t cmdQueue       = NULL;
QueueHandle_t logQueue       = NULL;

TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t uplinkTaskHandle = NULL;
TaskHandle_t webTaskHandle    = NULL;
TaskHandle_t loggerTaskHandle = NULL;

// ─── System state ─────────────────────────────────────────────────────────────
SystemState sysState = {0};

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n\n=== AirMQ SensorNode ===");

    // ── Reset reason ──
#ifdef ESP8266
    sysState.resetReason = 0;
    Serial.printf("Reset reason: %s\r\n", ESP.getResetReason().c_str());
#else
    {
        esp_reset_reason_t r = esp_reset_reason();
        static const char* const names[] = {
            "Unknown","Poweron","Ext","SW","Panic",
            "IntWDT","TaskWDT","WDT","Deepsleep","Brownout","SDIO"
        };
        const char* name = ((int)r < 11) ? names[(int)r] : "?";
        Serial.printf("Reset reason: %d (%s)\r\n", (int)r, name);
        sysState.resetReason = (uint8_t)r;
    }
#endif

    // ── Mutex ──
    sysState.mutex = xSemaphoreCreateMutex();

    // ── Chip ID ──
    uint32_t chipId = getChipId();
    STATE_LOCK();
    sysState.chipId          = chipId;
    sysState.teleIntervalM   = DEFAULT_TELE_INTERVAL_M;
    sysState.sampleNum       = DEFAULT_SAMPLE_NUM;
    sysState.onTime          = 30;
    sysState.startTime       = millis() / 1000;
    sysState.mqttConnected   = false;
    sysState.wifiConnected   = false;
    sysState.apMode          = false;
    sysState.sensorsActive   = 0;
    sysState.readingCount    = 0;
    snprintf(sysState.sysname, sizeof(sysState.sysname), "AirMQ_%lu",
             (unsigned long)chipId);
    STATE_UNLOCK();
    Serial.printf("Chip ID: %lu  Name: %s\r\n", (unsigned long)chipId, sysState.sysname);

    // ── Queues ──
    sensorQueue = xQueueCreate(SENSOR_QUEUE_SIZE, sizeof(SensorReading));
    cmdQueue    = xQueueCreate(CMD_QUEUE_SIZE,    sizeof(MqttCommand));
    logQueue    = xQueueCreate(LOG_QUEUE_SIZE,    sizeof(LogEntry));
    if (!sensorQueue || !cmdQueue || !logQueue) {
        Serial.println("FATAL: queue creation failed");
        DEVICE_RESTART();
    }

    // ── Storage ──
    if (!storageInit())
        Serial.println("Storage init failed — continuing without config");
    loadSensorSetup();

    // ── Feature flags — must be set before loggerInit()/webInit() ──
    {
        FeatureFlags feat;
        loadFeatures(feat);
        loggerSetWsEnabled(feat.wsLog);
        webSetEnabled(feat.web);
        if (!feat.wsLog) Serial.println("WebSocket log disabled (feature flag)");
        if (!feat.web)   Serial.println("Web server disabled (feature flag)");
    }

    // ── LED + persisted operational params ──
    {
        HwConfig hw;
        loadHwConfig(hw);
        if (hw.pin5v >= 0) { pinMode(hw.pin5v, OUTPUT); digitalWrite(hw.pin5v, HIGH); }
        ledInit(hw.led_pin);
        // Override RAM defaults with persisted values
        STATE_LOCK();
        sysState.teleIntervalM = hw.teleIntervalM;
        sysState.sampleNum     = hw.sampleNum;
        sysState.onTime        = hw.onTime;
        STATE_UNLOCK();
    }

    // ── Init sensors ──
    sensorsInit();

    // ── NTP (best-effort; syncs after WiFi connects) ──
    configTzTime(MYTZ, "time.google.com", "pool.ntp.org");

    // ── Start tasks ──
    Serial.printf("[+%lums] Starting tasks\r\n", millis());

#ifdef ESP8266
    // Cooperative: init each subsystem, then drive from loop()
    loggerInit();
    webInit();
    uplinkInit();  // opens AP + starts STA attempt
    logMessage("Ready — " FW_BUILD, "info");
#else
    CREATE_TASK_CORE(loggerTask, "logger", TASK_STACK_LOGGER,
                     NULL, TASK_PRIO_LOGGER, &loggerTaskHandle, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    CREATE_TASK_CORE(sensorTask, "sensor", TASK_STACK_SENSOR,
                     NULL, TASK_PRIO_SENSOR, &sensorTaskHandle, 0);

    CREATE_TASK_CORE(uplinkTask, "uplink", TASK_STACK_UPLINK,
                     NULL, TASK_PRIO_UPLINK, &uplinkTaskHandle, 1);

    CREATE_TASK_CORE(webTask, "web", TASK_STACK_WEB,
                     NULL, TASK_PRIO_WEB, &webTaskHandle, 1);

    logMessage("All tasks started — " FW_BUILD, "info");
#endif
}

#ifdef ESP8266
void loop() {
    ledUpdate();
    loggerProcess();
    webProcess();
    uplinkProcess();
    sensorProcess();
}
#else
void loop() {
    // All work is done in FreeRTOS tasks.
    vTaskSuspend(NULL);
}
#endif
