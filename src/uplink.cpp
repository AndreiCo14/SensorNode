#include "uplink.h"
#include "config.h"
#include "settings.h"
#include "logger.h"
#include "storage.h"
#include "system_state.h"
#include "led.h"
#include "platform.h"
#include "queues.h"
#include <ArduinoJson.h>
#include <espMqttClient.h>
#include <DNSServer.h>
#include <time.h>

// ─── MQTT client (TLS or plain, selected at runtime) ─────────────────────────
static espMqttClient*       mqttPlain  = nullptr;
static espMqttClientSecure* mqttSecure = nullptr;
#define MQTT_CALL(method, ...) \
    do { if (mqttSecure) mqttSecure->method(__VA_ARGS__); \
         else if (mqttPlain) mqttPlain->method(__VA_ARGS__); } while(0)
#define MQTT_LOOP() \
    do { if (mqttSecure) mqttSecure->loop(); \
         else if (mqttPlain) mqttPlain->loop(); } while(0)
#define MQTT_CONNECTED() \
    (mqttSecure ? mqttSecure->connected() : (mqttPlain ? mqttPlain->connected() : false))
#define MQTT_PUBLISH(topic, qos, retain, payload) \
    do { if (mqttSecure) mqttSecure->publish(topic, qos, retain, payload); \
         else if (mqttPlain) mqttPlain->publish(topic, qos, retain, payload); } while(0)
#define MQTT_SUBSCRIBE(topic, qos) \
    do { if (mqttSecure) mqttSecure->subscribe(topic, qos); \
         else if (mqttPlain) mqttPlain->subscribe(topic, qos); } while(0)
#define MQTT_CONNECT() \
    do { if (mqttSecure) mqttSecure->connect(); \
         else if (mqttPlain) mqttPlain->connect(); } while(0)

static DNSServer dnsServer;
static char      g_apSsid[24];
static bool      apRequested = false;
static MqttConfig mqttCfgData;

#ifdef ESP8266
// ── ESP8266 cooperative uplink state machine ──────────────────────────────────
enum UplinkState { US_AP_WINDOW, US_AP_ONLY, US_CONNECTED };
static UplinkState s_uplinkState   = US_AP_WINDOW;
static uint32_t    s_apWindowStart = 0;
static bool        s_triedSecondary = false;
static char        s_ssid[33], s_pass[65], s_ssid2[33], s_pass2[65];
static SensorReading s_batch[10];
static uint8_t     s_batchCount = 0;
static uint32_t    s_lastTele   = 0;
#endif

// ─── Topic builder ────────────────────────────────────────────────────────────

static void buildTopic(char* buf, size_t bufLen, const char* suffix) {
    snprintf(buf, bufLen, "%s%lu%s",
             mqttCfgData.prefix,
             (unsigned long)STATE_GET(chipId),
             suffix);
}

// ─── Epoch time ───────────────────────────────────────────────────────────────

static uint32_t getEpochTime() {
    time_t now = time(NULL);
    if (now > 1000000000UL) return (uint32_t)now;
    return millis() / 1000;
}

// ─── Publish sensor batch ─────────────────────────────────────────────────────

static void publishSensorData(const SensorReading* batch, uint8_t count) {
    if (!MQTT_CONNECTED() || count == 0) return;

    // Build JSON array merging base fields + sensor-specific data fields
    String payload = "[";
    for (uint8_t i = 0; i < count; i++) {
        if (i > 0) payload += ',';
        // Splice: strip trailing } from base, then append data fields
        char base[128];
        snprintf(base, sizeof(base),
                 "{\"deviceId\":%lu,\"time\":%lu,\"count\":%u,\"mV\":%u,\"rssi\":%d,\"msgType\":%u,",
                 (unsigned long)batch[i].deviceId,
                 (unsigned long)batch[i].time,
                 batch[i].count,
                 batch[i].mV,
                 batch[i].rssi,
                 batch[i].msgType);
        payload += base;
        // batch[i].data is like {"temp":21.5,...} — strip leading {
        const char* d = batch[i].data;
        if (d[0] == '{') d++;  // skip opening brace; already have comma separator above
        payload += d;
    }
    payload += ']';

    char topic[64];
    buildTopic(topic, sizeof(topic), TOPIC_DATA_SUFFIX);
    MQTT_PUBLISH(topic, 0, false, payload.c_str());
    logMessage("Published " + String(count) + " readings → " + topic, "info");

    if (mqttSecure || mqttPlain) ledSetState(LED_MQTT_OK);
}

// ─── Publish telemetry ────────────────────────────────────────────────────────

void sendTelemetry() {
    if (!MQTT_CONNECTED()) return;

    STATE_LOCK();
    uint32_t chipId    = sysState.chipId;
    uint32_t startTime = sysState.startTime;
    uint32_t readings  = sysState.readingCount;
    uint8_t  active    = sysState.sensorsActive;
    uint8_t  rstReason = sysState.resetReason;
    uint16_t teleInt   = sysState.teleIntervalM;
    STATE_UNLOCK();

    uint32_t now = millis() / 1000;

    JsonDocument doc;
    doc["id"]          = chipId;
    doc["time"]        = getEpochTime();
    doc["build"]       = FW_BUILD;
    doc["uptime"]      = (now - startTime) / 60;
    doc["readingCount"] = readings;
    doc["sensorsActive"] = active;
    doc["freeHeap"]    = (uint32_t)FREE_HEAP();
    doc["wifiRSSI"]    = WiFi.RSSI();
    doc["rstReason"]   = rstReason;
    doc["curInterval"] = teleInt;

    String payload;
    serializeJson(doc, payload);

    char topic[64];
    buildTopic(topic, sizeof(topic), TOPIC_TELE_SUFFIX);
    MQTT_PUBLISH(topic, 0, false, payload.c_str());
    logMessage(String("MQTT → ") + topic, "info");

    STATE_SET(lastTeleSent, (int32_t)now);
}

// ─── Publish start message ────────────────────────────────────────────────────

static void publishStart() {
    JsonDocument doc;
    doc["id"]     = STATE_GET(chipId);
    doc["time"]   = getEpochTime();
    doc["build"]  = FW_BUILD;
    doc["ip"]     = WiFi.localIP().toString();

    String payload;
    serializeJson(doc, payload);

    char topic[64];
    buildTopic(topic, sizeof(topic), TOPIC_START_SUFFIX);
    MQTT_PUBLISH(topic, 0, false, payload.c_str());
    logMessage(String("MQTT → ") + topic, "info");
}

// ─── OTA via HTTP/HTTPS ───────────────────────────────────────────────────────

#ifdef ESP8266
static int doHttpOta(const char* url) {
    logMessage(String("OTA: fetching ") + url, "info");
    ESPhttpUpdate.setLedPin(-1);
    ESPhttpUpdate.rebootOnUpdate(false);
    bool isHttps = (strncmp(url, "https", 5) == 0);
    HTTPUpdateResult res;
    if (isHttps) {
        BearSSL::WiFiClientSecure client;
        client.setInsecure();
        res = ESPhttpUpdate.update(client, url);
    } else {
        WiFiClient client;
        res = ESPhttpUpdate.update(client, url);
    }
    if (res == HTTP_UPDATE_OK) {
        logMessage("OTA complete — rebooting", "info");
        return 0;
    }
    logMessage("OTA failed: " + String(ESPhttpUpdate.getLastErrorString()), "error");
    return 1;
}
#else
static int doHttpOta(const char* url) {
    logMessage(String("OTA: fetching ") + url, "info");
    bool isHttps = (strncmp(url, "https", 5) == 0);

    WiFiClientSecure secureClient;
    WiFiClient       plainClient;

    HTTPClient http;
    if (isHttps) {
        secureClient.setInsecure();
        http.begin(secureClient, url);
    } else {
        http.begin(plainClient, url);
    }
    http.setTimeout(30000);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        logMessage("OTA HTTP error: " + String(code), "error");
        http.end();
        return 1;
    }

    int totalSize = http.getSize();
    if (!Update.begin(totalSize > 0 ? (size_t)totalSize : UPDATE_SIZE_UNKNOWN)) {
        logMessage("OTA begin failed: " + String(Update.errorString()), "error");
        http.end();
        return 2;
    }
    Update.writeStream(*http.getStreamPtr());
    if (!Update.end(true)) {
        logMessage("OTA failed: " + String(Update.errorString()), "error");
        http.end();
        return 3;
    }
    logMessage("OTA complete", "info");
    http.end();
    return 0;
}
#endif

static void doOtaCheck() {
    if (strlen(OTA_VERSION_URL) == 0) {
        logMessage("OTA_VERSION_URL not set", "warn");
        return;
    }
    logMessage("OTA check: " + String(OTA_VERSION_URL), "info");

    bool isHttps = (strncmp(OTA_VERSION_URL, "https", 5) == 0);
    WiFiClientSecure secureClient;
    WiFiClient       plainClient;
    HTTPClient http;
#ifdef ESP8266
    if (isHttps) { secureClient.setInsecure(); http.begin(secureClient, OTA_VERSION_URL); }
    else          { http.begin(plainClient, OTA_VERSION_URL); }
#else
    if (isHttps) { secureClient.setInsecure(); http.begin(secureClient, OTA_VERSION_URL); }
    else          { http.begin(plainClient, OTA_VERSION_URL); }
#endif
    http.setTimeout(10000);
    int code = http.GET();
    if (code != HTTP_CODE_OK) { logMessage("OTA check failed: " + String(code), "error"); http.end(); return; }

    JsonDocument doc;
    if (deserializeJson(doc, http.getString())) { http.end(); logMessage("OTA version JSON invalid", "error"); return; }
    http.end();

    const char* remoteBuild = doc["build"];
    const char* binUrl      = doc["url"];
    if (!remoteBuild || !binUrl) { logMessage("OTA version JSON missing fields", "error"); return; }
    if (strcmp(remoteBuild, FW_BUILD) == 0) { logMessage(String("OTA: up to date (") + FW_BUILD + ")", "info"); return; }

    logMessage(String("OTA: new build ") + remoteBuild + " → updating", "info");
    int result = doHttpOta(binUrl);
    if (result == 0) { vTaskDelay(pdMS_TO_TICKS(200)); DEVICE_RESTART(); }
}

// ─── MQTT command handler ─────────────────────────────────────────────────────

static void handleCommand(const char* payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        logMessage("Command JSON parse failed", "error");
        return;
    }

    if (!doc["teleIntervalM"].isNull()) {
        uint16_t v = doc["teleIntervalM"];
        STATE_SET(teleIntervalM, v);
        logMessage("teleIntervalM → " + String(v), "info");
    }

    if (!doc["sampleNum"].isNull()) {
        int8_t v = doc["sampleNum"];
        STATE_SET(sampleNum, v);
        logMessage("sampleNum → " + String(v), "info");
    }

    if (!doc["sensorConf"].isNull()) {
        if (xSemaphoreTake(sensorConfMutex, pdMS_TO_TICKS(1000))) {
            sensorConfData = doc["sensorConf"];
            xSemaphoreGive(sensorConfMutex);
        }
        saveSensorConf();
        logMessage("sensorConf updated", "info");
    }

    if (!doc["debugLog"].isNull()) {
        bool en = doc["debugLog"].as<bool>();
        setDebugLog(en);
        logMessage(String("debugLog → ") + (en ? "on" : "off"), "info");
    }

    if (doc["ota"].is<const char*>()) {
        int res = doHttpOta(doc["ota"].as<const char*>());
        if (res == 0) { vTaskDelay(pdMS_TO_TICKS(200)); DEVICE_RESTART(); }
    }

    if (doc["cmd"].is<const char*>()) {
        const char* cmd = doc["cmd"];
        if (strcmp(cmd, "reboot")    == 0) { logMessage("Rebooting", "warn"); vTaskDelay(pdMS_TO_TICKS(500)); DEVICE_RESTART(); }
        if (strcmp(cmd, "telemetry") == 0) sendTelemetry();
        if (strcmp(cmd, "otaCheck")  == 0) doOtaCheck();
        if (strcmp(cmd, "wifiOn")    == 0) apRequested = true;
        if (strcmp(cmd, "wifiOff")   == 0) {
            if (STATE_GET(apMode)) {
                dnsServer.stop();
                WiFi.softAPdisconnect(true);
                WiFi.mode(WIFI_STA);
                STATE_SET(apMode, false);
                logMessage("AP closed", "info");
            }
        }
    }
}

// ─── MQTT callbacks ───────────────────────────────────────────────────────────

static void onMqttConnect(bool) {
    STATE_SET(mqttConnected, true);
    logMessage("MQTT connected", "info");
    ledSetState(LED_MQTT_OK);

    char cmdTopic[64];
    snprintf(cmdTopic, sizeof(cmdTopic), "%s%lu%s",
             mqttCfgData.prefix,
             (unsigned long)STATE_GET(chipId),
             TOPIC_CMD_SUFFIX);
    MQTT_SUBSCRIBE(cmdTopic, 0);
    logMessage("Subscribed: " + String(cmdTopic), "info");
    publishStart();
}

static void onMqttDisconnect(espMqttClientTypes::DisconnectReason reason) {
    STATE_SET(mqttConnected, false);
    ledSetState(LED_CONNECTING);
    logMessage("MQTT disconnected: " + String((int)reason), "warn");
}

static void onMqttMessage(const espMqttClientTypes::MessageProperties&,
                           const char*, const uint8_t* payload,
                           size_t len, size_t index, size_t total) {
    if (index == 0 && len == total) {
        MqttCommand cmd;
        size_t copyLen = len < sizeof(cmd.payload) - 1 ? len : sizeof(cmd.payload) - 1;
        memcpy(cmd.payload, payload, copyLen);
        cmd.payload[copyLen] = '\0';
        xQueueSend(cmdQueue, &cmd, 0);
    }
}

// ─── MQTT init ────────────────────────────────────────────────────────────────

static void initMqtt() {
    STATE_LOCK();
    char sysname[17];
    strncpy(sysname, sysState.sysname, sizeof(sysname));
    STATE_UNLOCK();

    if (mqttCfgData.tls) {
        if (!mqttSecure) mqttSecure = new espMqttClientSecure();
        mqttSecure->setInsecure();
        mqttSecure->onConnect(onMqttConnect);
        mqttSecure->onDisconnect(onMqttDisconnect);
        mqttSecure->onMessage(onMqttMessage);
        mqttSecure->setServer(mqttCfgData.broker, mqttCfgData.port);
        mqttSecure->setClientId(sysname);
        mqttSecure->setKeepAlive(60);
        mqttSecure->setCleanSession(true);
        mqttSecure->connect();
    } else {
        if (!mqttPlain) mqttPlain = new espMqttClient();
        mqttPlain->onConnect(onMqttConnect);
        mqttPlain->onDisconnect(onMqttDisconnect);
        mqttPlain->onMessage(onMqttMessage);
        mqttPlain->setServer(mqttCfgData.broker, mqttCfgData.port);
        mqttPlain->setClientId(sysname);
        mqttPlain->setKeepAlive(60);
        mqttPlain->setCleanSession(true);
        mqttPlain->connect();
    }
    logMessage(String("MQTT connecting to ") + mqttCfgData.broker + ":" +
               String(mqttCfgData.port) + (mqttCfgData.tls ? " (TLS)" : ""), "info");
}

// ─── WiFi AP window ───────────────────────────────────────────────────────────

static bool wifiApWindow(const char* ssid1, const char* pass1,
                         const char* ssid2, const char* pass2) {
    bool hasPrimary   = (strlen(ssid1) > 0);
    bool hasSecondary = (strlen(ssid2) > 0);
    bool hasSta       = hasPrimary || hasSecondary;

    WiFi.mode(hasSta ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(g_apSsid);
    dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));

    STATE_SET(apMode, true);
    STATE_SET(wifiConnected, true);

    logMessage(String("AP: ") + g_apSsid + " @ 192.168.4.1 (3 min)", "warn");
    ledSetState(LED_CONNECTING);

    if (hasPrimary) {
        WiFi.setHostname(g_apSsid);
        WiFi.begin(ssid1, pass1);
        logMessage(String("STA → ") + ssid1, "info");
    } else if (hasSecondary) {
        WiFi.setHostname(g_apSsid);
        WiFi.begin(ssid2, pass2);
        logMessage(String("STA → ") + ssid2, "info");
    }

    uint32_t t0       = millis();
    uint32_t staStart = millis();
    bool triedSecondary = !hasSecondary || !hasPrimary;

    while (millis() - t0 < AP_WINDOW_MS) {
        dnsServer.processNextRequest();

        MqttCommand cmd;
        if (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE)
            handleCommand(cmd.payload);

        if (hasSta && WiFi.status() == WL_CONNECTED) {
            logMessage("WiFi STA: " + WiFi.localIP().toString(), "info");
            STATE_SET(wifiConnected, true);
            return true;
        }

        if (!triedSecondary && millis() - staStart > WIFI_PRIMARY_TIMEOUT_MS) {
            triedSecondary = true;
            WiFi.disconnect(false);
            WiFi.begin(ssid2, pass2);
            logMessage(String("STA fallback → ") + ssid2, "info");
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    logMessage("AP window expired — no STA", "warn");
    return false;
}

// ─── Uplink task ──────────────────────────────────────────────────────────────

void uplinkTask(void* pvParameters) {
    loadMqttConfig(mqttCfgData);

    snprintf(g_apSsid, sizeof(g_apSsid), "SNode-%lu",
             (unsigned long)STATE_GET(chipId));

    char wifiSsid[33] = {}, wifiPass[65] = {};
    char wifiSsid2[33] = {}, wifiPass2[65] = {};
    loadWifiCreds(wifiSsid, sizeof(wifiSsid), wifiPass, sizeof(wifiPass),
                  wifiSsid2, sizeof(wifiSsid2), wifiPass2, sizeof(wifiPass2));

    bool staConnected = wifiApWindow(wifiSsid, wifiPass, wifiSsid2, wifiPass2);

    if (!staConnected) {
        logMessage("No STA — AP remains up", "warn");
        for (;;) {
            dnsServer.processNextRequest();
            MqttCommand cmd;
            if (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE) handleCommand(cmd.payload);
            SensorReading r;
            while (xQueueReceive(sensorQueue, &r, 0) == pdTRUE) {}
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        return;
    }

    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    STATE_SET(apMode, false);
    logMessage("AP closed, STA mode", "info");
    ledSetState(LED_CONNECTED);

    configTzTime(MYTZ, "time.google.com", "pool.ntp.org");

    initMqtt();

    SensorReading batch[10];
    uint8_t  batchCount = 0;
    uint32_t lastTele   = 0;

    for (;;) {
        MQTT_LOOP();

        SensorReading reading;
        if (xQueueReceive(sensorQueue, &reading, 0) == pdTRUE) {
            reading.time = getEpochTime();
            if (batchCount < 10)
                batch[batchCount++] = reading;
            else {
                logMessage("Batch overflow — dropping oldest", "warn");
                memmove(batch, batch + 1, sizeof(SensorReading) * 9);
                batch[9] = reading;
            }
        }

        MqttCommand cmd;
        if (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE)
            handleCommand(cmd.payload);

        int8_t sn = STATE_GET(sampleNum);
        if (batchCount >= sn && batchCount > 0) {
            publishSensorData(batch, batchCount);
            STATE_LOCK();
            sysState.readingCount += batchCount;
            STATE_UNLOCK();
            sendTelemetry();
            batchCount = 0;
            lastTele = millis() / 1000;
        }

        uint32_t now     = millis() / 1000;
        uint16_t teleInt = STATE_GET(teleIntervalM);
        if (now - lastTele > (uint32_t)teleInt * 60) {
            sendTelemetry();
            lastTele = now;
        }

        if (apRequested) {
            apRequested = false;
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP(g_apSsid);
            dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
            STATE_SET(apMode, true);
            logMessage(String("AP re-enabled: ") + g_apSsid, "info");
        }

        if (WiFi.status() != WL_CONNECTED) {
            STATE_SET(wifiConnected, false);
            ledSetState(LED_CONNECTING);
            logMessage("WiFi lost — reconnecting", "warn");
            WiFi.begin(wifiSsid, wifiPass);
        } else {
            STATE_SET(wifiConnected, true);
        }

        if (!MQTT_CONNECTED() && WiFi.status() == WL_CONNECTED) {
            logMessage("MQTT reconnecting...", "warn");
            MQTT_CONNECT();
        }

        STATE_SET(mqttConnected, MQTT_CONNECTED());

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

#ifdef ESP8266
// ─── ESP8266 cooperative uplink ───────────────────────────────────────────────

void uplinkInit() {
    loadMqttConfig(mqttCfgData);
    snprintf(g_apSsid, sizeof(g_apSsid), "SNode-%lu",
             (unsigned long)STATE_GET(chipId));
    loadWifiCreds(s_ssid, sizeof(s_ssid), s_pass, sizeof(s_pass),
                  s_ssid2, sizeof(s_ssid2), s_pass2, sizeof(s_pass2));

    bool hasPrimary   = (strlen(s_ssid) > 0);
    bool hasSecondary = (strlen(s_ssid2) > 0);
    bool hasSta       = hasPrimary || hasSecondary;

    WiFi.mode(hasSta ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(g_apSsid);
    dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
    STATE_SET(apMode, true);

    if (hasPrimary) {
        WiFi.setHostname(g_apSsid);
        WiFi.begin(s_ssid, s_pass);
        logMessage(String("STA → ") + s_ssid, "info");
    } else if (hasSecondary) {
        WiFi.setHostname(g_apSsid);
        WiFi.begin(s_ssid2, s_pass2);
        logMessage(String("STA → ") + s_ssid2, "info");
    }

    s_apWindowStart  = millis();
    s_triedSecondary = !hasSecondary || !hasPrimary;
    s_uplinkState    = US_AP_WINDOW;
    ledSetState(LED_CONNECTING);
    logMessage(String("AP: ") + g_apSsid + " @ 192.168.4.1 (" +
               String(AP_WINDOW_MS / 1000) + "s window)", "warn");
}

void uplinkProcess() {
    switch (s_uplinkState) {

    case US_AP_WINDOW:
        dnsServer.processNextRequest();
        {
            MqttCommand cmd;
            if (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE)
                handleCommand(cmd.payload);
        }
        if (WiFi.status() == WL_CONNECTED) {
            logMessage("WiFi STA: " + WiFi.localIP().toString(), "info");
            dnsServer.stop();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            STATE_SET(apMode, false);
            STATE_SET(wifiConnected, true);
            configTzTime(MYTZ, "time.google.com", "pool.ntp.org");
            initMqtt();
            s_uplinkState = US_CONNECTED;
            ledSetState(LED_CONNECTED);
            break;
        }
        if (!s_triedSecondary &&
            millis() - s_apWindowStart > WIFI_PRIMARY_TIMEOUT_MS) {
            s_triedSecondary = true;
            WiFi.disconnect(false);
            WiFi.begin(s_ssid2, s_pass2);
            logMessage(String("STA fallback → ") + s_ssid2, "info");
        }
        if (millis() - s_apWindowStart > AP_WINDOW_MS) {
            logMessage("AP window expired — no STA", "warn");
            s_uplinkState = US_AP_ONLY;
        }
        break;

    case US_AP_ONLY:
        dnsServer.processNextRequest();
        {
            MqttCommand cmd;
            if (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE)
                handleCommand(cmd.payload);
            SensorReading r;
            while (xQueueReceive(sensorQueue, &r, 0) == pdTRUE) {}
        }
        break;

    case US_CONNECTED:
        MQTT_LOOP();
        {
            SensorReading reading;
            if (xQueueReceive(sensorQueue, &reading, 0) == pdTRUE) {
                reading.time = getEpochTime();
                if (s_batchCount < 10)
                    s_batch[s_batchCount++] = reading;
                else {
                    memmove(s_batch, s_batch + 1, sizeof(SensorReading) * 9);
                    s_batch[9] = reading;
                }
            }
            MqttCommand cmd;
            if (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE)
                handleCommand(cmd.payload);

            int8_t sn = STATE_GET(sampleNum);
            if (s_batchCount >= sn && s_batchCount > 0) {
                publishSensorData(s_batch, s_batchCount);
                STATE_LOCK();
                sysState.readingCount += s_batchCount;
                STATE_UNLOCK();
                sendTelemetry();
                s_batchCount = 0;
                s_lastTele   = millis() / 1000;
            }

            uint32_t now     = millis() / 1000;
            uint16_t teleInt = STATE_GET(teleIntervalM);
            if (now - s_lastTele > (uint32_t)teleInt * 60) {
                sendTelemetry();
                s_lastTele = now;
            }

            if (apRequested) {
                apRequested = false;
                WiFi.mode(WIFI_AP_STA);
                WiFi.softAP(g_apSsid);
                dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
                STATE_SET(apMode, true);
                logMessage(String("AP re-enabled: ") + g_apSsid, "info");
            }

            if (WiFi.status() != WL_CONNECTED) {
                STATE_SET(wifiConnected, false);
                ledSetState(LED_CONNECTING);
                logMessage("WiFi lost — reconnecting", "warn");
                WiFi.begin(s_ssid, s_pass);
            } else {
                STATE_SET(wifiConnected, true);
            }

            if (!MQTT_CONNECTED() && WiFi.status() == WL_CONNECTED)
                MQTT_CONNECT();

            STATE_SET(mqttConnected, MQTT_CONNECTED());
        }
        break;
    }
}
#endif
