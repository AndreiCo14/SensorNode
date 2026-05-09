#include "uplink.h"
#include "config.h"
#include "settings.h"
#include "logger.h"
#include "storage.h"
#include "system_state.h"
#include "led.h"
#include "platform.h"
#include "queues.h"
#include "sensors/sensor_manager.h"
#include "deep_sleep.h"
#include <ArduinoJson.h>
#include <espMqttClient.h>
#include <DNSServer.h>
#include <time.h>

// After publishStart, wait this long for a broker config command before enabling sensors
#define STARTUP_CMD_WINDOW_MS 5000

// ─── MQTT client (TLS or plain, selected at runtime) ─────────────────────────
static espMqttClient*       mqttPlain  = nullptr;
static espMqttClientSecure* mqttSecure = nullptr;
// Deferred subscription: subscribe() is called from the main loop rather than
// directly inside the CONNACK callback, where the library outbox state may not
// be fully settled yet (causing spurious subscribe() failures).
static bool s_needsSubscribe      = false;
static bool     s_publishStartSent    = false;
static bool     s_subscribeFailLogged = false;  // suppress log spam on repeated failures
static uint32_t s_connectedAtMs      = 0;       // millis() when CONNACK received
static char s_cmdTopic[64]        = {};
static bool s_sensorsStarted      = false;  // sensors have been enabled this boot
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
static uint32_t  s_startupWindowMs = 0;   // set by publishStart(); sensors enabled after window

// ─── Provisioning state ───────────────────────────────────────────────────────
static char   s_provisionTopic[80] = {};
static String s_provisionPayload;
static bool   s_provisionPending = false;

// ─── WiFi reconnect state ─────────────────────────────────────────────────────
static uint32_t s_wifiLostAt       = 0;      // millis() when WiFi was lost
static uint32_t s_lastWifiAttempt  = 0;      // millis() of last WiFi.begin() call
static bool     s_reconnApRaised   = false;  // true if WE raised the AP during reconnect
static bool     s_wifiWasConnected = false;  // tracks edge: connected→disconnected
static bool     s_wifiAltSecondary = false;  // alternates primary/secondary in phase 3

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

static void publishSleepStatus(uint32_t sleepSec) {
    char topic[64];
    char payload[40];
    buildTopic(topic, sizeof(topic), TOPIC_STATUS_SUFFIX);
    snprintf(payload, sizeof(payload), "{\"sleepTime\":%lu}", (unsigned long)sleepSec);
    MQTT_PUBLISH(topic, 0, false, payload);
    logMessageFmt("info", "MQTT → %s %s", topic, payload);
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

    // Build JSON array: deviceId + time + sensor data fields only
    static char payload[2048];  // up to 10 entries × ~170 bytes each
    size_t pos = 0;
    payload[pos++] = '[';
    for (uint8_t i = 0; i < count && pos < sizeof(payload) - 2; i++) {
        if (i > 0 && pos < sizeof(payload) - 1) payload[pos++] = ',';
        char base[64];
        int n = snprintf(base, sizeof(base),
                         "{\"deviceId\":%lu,\"time\":%lu,",
                         (unsigned long)batch[i].deviceId,
                         (unsigned long)batch[i].time);
        if (pos + n < sizeof(payload) - 1) { memcpy(payload + pos, base, n); pos += n; }
        const char* d = batch[i].data;
        if (d[0] == '{') d++;  // skip leading '{'; comma already present above
        size_t dl = strlen(d);
        if (pos + dl < sizeof(payload) - 1) { memcpy(payload + pos, d, dl); pos += dl; }
    }
    if (pos < sizeof(payload) - 1) payload[pos++] = ']';
    payload[pos] = '\0';

    char topic[64];
    buildTopic(topic, sizeof(topic), TOPIC_DATA_SUFFIX);
    MQTT_PUBLISH(topic, 0, false, payload);
    logMessageFmt("info", "Published %d readings -> %s", count, topic);

    if (mqttSecure || mqttPlain) ledSetState(LED_MQTT_OK);
}

// ─── Publish telemetry ────────────────────────────────────────────────────────

void sendTelemetry() {
    if (!MQTT_CONNECTED()) return;

    STATE_LOCK();
    uint32_t startTime = sysState.startTime;
    uint32_t readings  = sysState.readingCount;
    uint16_t teleInt   = sysState.teleIntervalM;
    STATE_UNLOCK();

    uint32_t now = millis() / 1000;

#ifdef BOARD_ESP8266
    float vcc = ESP.getVcc() / 1000.0f;
#else
    float vcc = 0.0f;
#endif

    JsonDocument doc;
    HwConfig hw;
    loadHwConfig(hw);

    doc["Uptime"]      = (now - startTime) / 60;
    doc["RSSI"]        = WiFi.RSSI();
    doc["VCC"]         = vcc;
    doc["freeHeap"]    = (uint32_t)FREE_HEAP();
    doc["curInterval"] = teleInt;
    doc["seq"]         = readings;
    doc["provisioned"] = hw.provisioned;

    static char payload[256];
    serializeJson(doc, payload, sizeof(payload));

    char topic[64];
    buildTopic(topic, sizeof(topic), TOPIC_TELE_SUFFIX);
    MQTT_PUBLISH(topic, 0, false, payload);
    logMessageFmt("info", "MQTT -> %s", topic);

    STATE_SET(lastTeleSent, (int32_t)now);
}

// ─── Publish start message ────────────────────────────────────────────────────

static void publishStart() {
    JsonDocument doc;
    doc["id"]     = STATE_GET(chipId);
    doc["time"]   = getEpochTime();
    doc["build"]  = FW_BUILD;
    doc["ip"]     = WiFi.localIP().toString();
#ifdef ESP8266
    doc["bootReason"]  = ESP.getResetReason();
#else
    {
        static const char* const names[] = {
            "Unknown","Poweron","Ext","SW","Panic",
            "IntWDT","TaskWDT","WDT","Deepsleep","Brownout","SDIO"
        };
        int r = (int)STATE_GET(resetReason);
        doc["bootReason"] = (r < 11) ? names[r] : "Unknown";
    }
#endif

    HwConfig hw;
    loadHwConfig(hw);
    doc["provisioned"] = hw.provisioned;

    static char payload[256];
    serializeJson(doc, payload, sizeof(payload));

    char topic[64];
    buildTopic(topic, sizeof(topic), TOPIC_START_SUFFIX);
    MQTT_PUBLISH(topic, 0, false, payload);
    logMessageFmt("info", "MQTT -> %s", topic);
    if (s_startupWindowMs == 0)
        s_startupWindowMs = millis();  // start the broker-response window
}

// ─── OTA via HTTP/HTTPS ───────────────────────────────────────────────────────

#ifdef ESP8266
// Force HTTP on ESP8266 — BearSSL with constrained buffers while WebSocket is open
// causes OOM crashes during SSL handshake on large binary downloads.
static String forceHttp(const char* url) {
    String s(url);
    if (s.startsWith("https://")) s = "http://" + s.substring(8);
    return s;
}

static int doHttpOta(const char* url) {
    ledSetState(LED_OTA);
    ledUpdate();  // push cyan to hardware immediately before blocking
    String httpUrl = forceHttp(url);
    ESPhttpUpdate.setLedPin(-1);
    ESPhttpUpdate.rebootOnUpdate(false);
    static int s_lastPct;
    ESPhttpUpdate.onProgress([](int cur, int total) {
        if (total <= 0) return;
        int pct = (cur * 100) / total;
        if (pct >= s_lastPct + 10) { s_lastPct = pct; logMessageFmt("otapct", "%d", pct); }
    });
    for (int attempt = 1; attempt <= 2; attempt++) {
        logMessageFmt("info", "OTA: fetching %s%s", httpUrl, (attempt > 1 ? " (retry)" : ""));
        // Flush queued log messages before blocking on update
        while (uxQueueMessagesWaiting(logQueue)) loggerProcess();
        s_lastPct = -1;
        WiFiClient client;
        HTTPUpdateResult res = ESPhttpUpdate.update(client, httpUrl);
        if (res == HTTP_UPDATE_OK) {
            logMessage("info", "OTA complete — rebooting");
            while (uxQueueMessagesWaiting(logQueue)) loggerProcess();
            return 0;
        }
        logMessageFmt("error", "OTA failed: %s", ESPhttpUpdate.getLastErrorString().c_str());
        if (attempt < 2) {
            logMessage("warn", "OTA retry in 5s...");
            delay(5000);
        }
    }
    while (uxQueueMessagesWaiting(logQueue)) loggerProcess();
    return 1;
}
#else
static int doHttpOta(const char* url) {
    ledSetState(LED_OTA);
    ledUpdate();  // push cyan to hardware immediately before blocking
    logMessageFmt("info", "OTA: fetching %s", url);
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
        logMessageFmt("error", "OTA HTTP error: %d", code);
        http.end();
        return 1;
    }

    int totalSize = http.getSize();
    if (!Update.begin(totalSize > 0 ? (size_t)totalSize : UPDATE_SIZE_UNKNOWN)) {
        logMessageFmt("error", "OTA begin failed: %s", Update.errorString());
        http.end();
        return 2;
    }
    static int s_lastPct;
    s_lastPct = -1;
    Update.onProgress([](size_t cur, size_t total) {
        if (total == 0) return;
        int pct = (int)((cur * 100) / total);
        if (pct >= s_lastPct + 10) { s_lastPct = pct; logMessageFmt("otapct", "%d", pct); }
    });
    Update.writeStream(*http.getStreamPtr());
    if (!Update.end(true)) {
        logMessageFmt("error", "OTA failed: %s", Update.errorString());
        http.end();
        return 3;
    }
    logMessage("info", "OTA complete");
    http.end();
    return 0;
}
#endif

static void doOtaCheck() {
#ifdef ESP8266
#define OTA_FLUSH() do { while (uxQueueMessagesWaiting(logQueue)) loggerProcess(); } while(0)
#else
#define OTA_FLUSH() do {} while(0)
#endif

    if (strlen(OTA_VERSION_URL) == 0) {
        logMessage("warn", "OTA_VERSION_URL not set");
        OTA_FLUSH();
        return;
    }
    logMessageFmt("info", "OTA check: %s", OTA_VERSION_URL);
    OTA_FLUSH();  // send before blocking request

#ifdef ESP8266
    // Force HTTP on ESP8266 to avoid BearSSL OOM with open WebSocket
    String versionUrl = forceHttp(OTA_VERSION_URL);
    WiFiClient plainClient8266;
    HTTPClient http;
    http.begin(plainClient8266, versionUrl);
#else
    bool isHttps = (strncmp(OTA_VERSION_URL, "https", 5) == 0);
    WiFiClientSecure secureClient;
    WiFiClient       plainClient;
    HTTPClient http;
    if (isHttps) { secureClient.setInsecure(); http.begin(secureClient, OTA_VERSION_URL); }
    else          { http.begin(plainClient, OTA_VERSION_URL); }
#endif
    http.setTimeout(10000);
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        logMessageFmt("error", "OTA check failed: %d", code);
        http.end();
        OTA_FLUSH();
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, http.getString())) {
        http.end();
        logMessage("error", "OTA version JSON invalid");
        OTA_FLUSH();
        return;
    }
    http.end();

    const char* remoteBuild = doc["build"];
    const char* binUrl      = doc["url"];
    if (!remoteBuild || !binUrl) {
        logMessage("error", "OTA version JSON missing fields");
        OTA_FLUSH();
        return;
    }
    if (strcmp(remoteBuild, FW_BUILD) == 0) {
        logMessageFmt("info", "OTA: up to date (%s)", FW_BUILD);
        OTA_FLUSH();
        return;
    }

    logMessageFmt("info", "OTA: new build %s -> updating", remoteBuild);
    int result = doHttpOta(binUrl);
    if (result == 0) { vTaskDelay(pdMS_TO_TICKS(500)); DEVICE_RESTART(); }

#undef OTA_FLUSH
}

// ─── MQTT command handler ─────────────────────────────────────────────────────

static String exportProvisioningConfig() {
    JsonDocument doc;

    doc["timestamp"]     = getEpochTime();

    // Operational params — use active runtime values, not disk
    doc["teleIntervalM"] = STATE_GET(teleIntervalM);
    doc["sampleNum"]     = STATE_GET(sampleNum);
    doc["onTime"]        = STATE_GET(onTime);

    // Pin assignments and provisioned flag have no runtime state — read from disk
    HwConfig hw;
    loadHwConfig(hw);
    doc["provisioned"] = hw.provisioned;
    doc["interval"]    = hw.intervalSec;
    doc["i2c_sda"]     = hw.i2c_sda;
    doc["i2c_scl"]     = hw.i2c_scl;
    doc["uart_rx"]     = hw.uart_rx;
    doc["uart_tx"]     = hw.uart_tx;
    doc["onewire"]     = hw.onewire;
    doc["led_pin"]     = hw.led_pin;
    doc["5v_pin"]      = hw.pin5v;

    // Sensor setup — only active (enabled) sensors
    if (xSemaphoreTake(sensorSetupMutex, pdMS_TO_TICKS(1000))) {
        JsonArray src = sensorSetupData.as<JsonArray>();
        JsonArray dst = doc["sensors"].to<JsonArray>();
        for (JsonObject s : src)
            if (s["enabled"] | false) dst.add(s);
        xSemaphoreGive(sensorSetupMutex);
    }

    // MQTT config
    JsonObject mqtt  = doc["mqtt"].to<JsonObject>();
    mqtt["broker"]   = mqttCfgData.broker;
    mqtt["port"]     = mqttCfgData.port;
    mqtt["prefix"]   = mqttCfgData.prefix;
    mqtt["tls"]      = mqttCfgData.tls;

    String out;
    serializeJson(doc, out);
    return out;
}

static void handleProvisioningConfig(const char* payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        logMessage("error", "Provision JSON parse failed");
        return;
    }

    HwConfig hw;
    loadHwConfig(hw);
    bool hwChanged = false;

    if (!doc["teleIntervalM"].isNull()) { hw.teleIntervalM = doc["teleIntervalM"].as<uint16_t>(); STATE_SET(teleIntervalM, hw.teleIntervalM); hwChanged = true; }
    if (!doc["sampleNum"].isNull())     { hw.sampleNum     = doc["sampleNum"].as<int8_t>();       STATE_SET(sampleNum, hw.sampleNum);         hwChanged = true; }
    if (!doc["onTime"].isNull()) {
        hw.onTime = doc["onTime"].as<uint16_t>();
        if (hw.onTime < 30) hw.onTime = 30;
        STATE_SET(onTime, hw.onTime);
        broadcastOnTime(hw.onTime);
        hwChanged = true;
    }
    if (!doc["interval"].isNull()) { hw.intervalSec = doc["interval"].as<uint16_t>(); hwChanged = true; }
    if (!doc["i2c_sda"].isNull())  { hw.i2c_sda  = doc["i2c_sda"].as<int8_t>();  hwChanged = true; }
    if (!doc["i2c_scl"].isNull())  { hw.i2c_scl  = doc["i2c_scl"].as<int8_t>();  hwChanged = true; }
    if (!doc["uart_rx"].isNull())  { hw.uart_rx  = doc["uart_rx"].as<int8_t>();   hwChanged = true; }
    if (!doc["uart_tx"].isNull())  { hw.uart_tx  = doc["uart_tx"].as<int8_t>();   hwChanged = true; }
    if (!doc["onewire"].isNull())  { hw.onewire  = doc["onewire"].as<int8_t>();   hwChanged = true; }
    if (!doc["led_pin"].isNull())  { hw.led_pin  = doc["led_pin"].as<int8_t>();   hwChanged = true; ledInit(hw.led_pin); ledSetState(LED_MQTT_OK); }
    if (!doc["5v_pin"].isNull())   { hw.pin5v    = doc["5v_pin"].as<int8_t>();    hwChanged = true; }
    // Parse all gpio# keys from provision payload (any present gpio# replaces the whole set)
    {
        uint8_t n = 0;
        bool found = false;
        for (JsonPair kv : doc.as<JsonObject>()) {
            const char* key = kv.key().c_str();
            if (strncmp(key, "gpio", 4) != 0 || !isdigit((unsigned char)key[4])) continue;
            if (!found) { for (uint8_t i = 0; i < HwConfig::GPIO_CTRL_MAX; i++) hw.gpio_pin[i] = -1; found = true; }
            if (n >= HwConfig::GPIO_CTRL_MAX) break;
            hw.gpio_pin[n] = (int8_t)atoi(key + 4);
            strncpy(hw.gpio_mode[n], kv.value() | "invert", 7);
            hw.gpio_mode[n][7] = '\0';
            n++;
        }
        if (found) { hw.gpio_count = n; hwChanged = true; }
    }

    hw.provisioned = true;
    saveHwConfig(hw);
    if (hwChanged) {
        logMessage("info", "Provision: hwconfig applied:");
        if (!doc["teleIntervalM"].isNull()) logMessageFmt("=", "teleIntervalM=%d", hw.teleIntervalM);
        if (!doc["sampleNum"].isNull())     logMessageFmt("=", "sampleNum=%d", hw.sampleNum);
        if (!doc["onTime"].isNull())        logMessageFmt("=", "onTime=%d", hw.onTime);
        if (!doc["interval"].isNull())      logMessageFmt("=", "interval=%d", hw.intervalSec);
        if (!doc["i2c_sda"].isNull())       logMessageFmt("=", "sda=%d", hw.i2c_sda);
        if (!doc["i2c_scl"].isNull())       logMessageFmt("=", "scl=%d", hw.i2c_scl);
        if (!doc["uart_rx"].isNull())       logMessageFmt("=", "rx=%d", hw.uart_rx);
        if (!doc["uart_tx"].isNull())       logMessageFmt("=", "tx=%d", hw.uart_tx);
        if (!doc["onewire"].isNull())       logMessageFmt("=", "ow=%d", hw.onewire);
        if (!doc["led_pin"].isNull())       logMessageFmt("=", "led=%d", hw.led_pin);
        if (!doc["5v_pin"].isNull())        logMessageFmt("=", "5v=%d", hw.pin5v);
        for (uint8_t i = 0; i < hw.gpio_count; i++)
            if (hw.gpio_pin[i] >= 0) logMessageFmt("=", "gpio%d=%s", hw.gpio_pin[i], hw.gpio_mode[i]);
    } else {
        logMessage("info", "Provision: no hw fields changed, provisioned flag set");
    }

    if (doc["sensors"].is<JsonArray>()) {
        // Migration: old format stored set_pin/set_inverted inside the pms7003 sensor entry.
        // If no gpio# keys were in this payload, promote set_pin as gpio<N>.
        if (hw.gpio_count == 0) {
            for (JsonObject s : doc["sensors"].as<JsonArray>()) {
                if (strcmp(s["type"] | "", "pms7003") == 0 && !s["set_pin"].isNull()) {
                    int8_t pin = s["set_pin"].as<int8_t>();
                    bool inv = s["set_inverted"] | true;
                    if (pin >= 0 && hw.gpio_count < HwConfig::GPIO_CTRL_MAX) {
                        hw.gpio_pin[hw.gpio_count] = pin;
                        strncpy(hw.gpio_mode[hw.gpio_count], inv ? "invert" : "follow", 7);
                        hw.gpio_count++;
                        saveHwConfig(hw);
                        logMessageFmt("info", "Provision: migrated pms7003 set_pin → gpio%d=%s", pin, hw.gpio_mode[hw.gpio_count - 1]);
                    }
                    break;
                }
            }
        }
        if (xSemaphoreTake(sensorSetupMutex, pdMS_TO_TICKS(1000))) {
            sensorSetupData.set(doc["sensors"]);
            xSemaphoreGive(sensorSetupMutex);
        }
        saveSensorSetup();
        sensorsReinit();
        char sLog[256];
        char* p = sLog;
        p += snprintf(p, sizeof(sLog) - (p - sLog), "Provision: sensors —");
        for (JsonObject s : doc["sensors"].as<JsonArray>()) {
            const char* type = s["type"] | "?";
            bool enabled = s["enabled"] | false;
            p += snprintf(p, sizeof(sLog) - (p - sLog), " [%s %s]", type, enabled ? "ON" : "off");
        }
        logMessageFmt("info", "%s", sLog);
    }

    if (doc["mqtt"].is<JsonObject>()) {
        JsonObject mqtt = doc["mqtt"].as<JsonObject>();
        MqttConfig cfg;
        loadMqttConfig(cfg);
        if (!mqtt["broker"].isNull()) strncpy(cfg.broker, mqtt["broker"].as<const char*>(), sizeof(cfg.broker) - 1);
        if (!mqtt["port"].isNull())   cfg.port   = mqtt["port"].as<uint16_t>();
        if (!mqtt["prefix"].isNull()) strncpy(cfg.prefix, mqtt["prefix"].as<const char*>(), sizeof(cfg.prefix) - 1);
        if (!mqtt["tls"].isNull())    cfg.tls    = mqtt["tls"].as<bool>();
        saveMqttConfig(cfg);
        logMessageFmt("info", "Provision: MQTT broker=%s port=%d tls=%s (reconnect to apply)", cfg.broker, cfg.port, cfg.tls ? "yes" : "no");
    }
}

static void handleCommand(const char* payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        logMessage("error", "Command JSON parse failed");
        return;
    }

    if (doc["feature"].is<const char*>()) {
        const char* name = doc["feature"].as<const char*>();
        bool val = doc["value"] | true;
        FeatureFlags f;
        loadFeatures(f);
        if      (strcmp(name, "web")   == 0) f.web   = val;
        else if (strcmp(name, "wsLog") == 0) f.wsLog = val;
        else { logMessageFmt("warn", "Unknown feature: %s", name); return; }
        saveFeatures(f);
        logMessageFmt("info", "feature '%s' -> %s, rebooting", name, val ? "on" : "off");
        vTaskDelay(pdMS_TO_TICKS(500));
        DEVICE_RESTART();
        return;
    }

    bool needsSave = false;

    if (!doc["teleIntervalM"].isNull()) {
        uint16_t v = doc["teleIntervalM"];
        STATE_SET(teleIntervalM, v);
        broadcastTeleInterval(v);
        logMessageFmt("info", "teleIntervalM -> %d", v);
        needsSave = true;
    }

    if (!doc["sampleNum"].isNull()) {
        STATE_SET(sampleNum, (int8_t)doc["sampleNum"]);
        needsSave = true;
    }

    if (!doc["onTime"].isNull()) {
        uint16_t v = doc["onTime"];
        if (v < 30) v = 30;
        STATE_SET(onTime, v);
        logMessageFmt("info", "onTime -> %d", v);
        broadcastOnTime(v);
        needsSave = true;
    }

    if (!doc["deepSleep"].isNull()) {
        bool val = doc["deepSleep"].as<bool>();
        setDeepSleepMode(val);
        logMessageFmt("info", "deepSleep -> %s", (val ? "on" : "off"));
        needsSave = true;
    }

    if (!doc["ignoreCmd"].isNull()) {
        bool val = doc["ignoreCmd"].as<bool>();
        setIgnoreCmdMode(val);
        logMessageFmt("info", "ignoreCmd -> %s", (val ? "on" : "off"));
        needsSave = true;
    }

    if (!doc["narodmon"].isNull()) {
        bool val = doc["narodmon"].as<bool>();
        setNarodmonMode(val);
        logMessageFmt("info", "narodmon -> %s", (val ? "on" : "off"));
        needsSave = true;
    }

    if (needsSave) {
        HwConfig hw;
        loadHwConfig(hw);
        hw.teleIntervalM = STATE_GET(teleIntervalM);
        hw.sampleNum     = STATE_GET(sampleNum);
        hw.onTime        = STATE_GET(onTime);
        hw.deepSleep     = getDeepSleepMode();
        hw.ignoreCmd     = getIgnoreCmdMode();
        hw.narodmon      = getNarodmonMode();
        saveHwConfig(hw);
        logMessage("info", "hwconfig saved");
    }

    if (!doc["debugLog"].isNull()) {
        bool en = doc["debugLog"].as<bool>();
        setDebugLog(en);
        logMessageFmt("info", "debugLog -> %s", (en ? "on" : "off"));
    }

    // maintenance:true  — pause deep sleep cycle, stay online (set in Start reply)
    // maintenance:false — resume deep sleep cycle (sent as subsequent cmd)
    if (!doc["maintenance"].isNull()) {
        bool m = doc["maintenance"].as<bool>();
        setMaintenanceMode(m);
        if (m) {
            logMessage("info", "Maintenance mode — deep sleep paused");
        } else {
            logMessage("info", "Maintenance ended — deep sleep cycle resuming");
        }
    }

    if (doc["ota"].is<const char*>()) {
        int res = doHttpOta(doc["ota"].as<const char*>());
        if (res == 0) { vTaskDelay(pdMS_TO_TICKS(200)); DEVICE_RESTART(); }
    }

    if (doc["cmd"].is<const char*>()) {
        const char* cmd = doc["cmd"];
        if (strcmp(cmd, "reboot")    == 0) { logMessage("warn", "Rebooting"); vTaskDelay(pdMS_TO_TICKS(500)); DEVICE_RESTART(); }
        if (strcmp(cmd, "wifiReset") == 0) { clearWifiCreds(); logMessage("warn", "WiFi credentials cleared — rebooting"); vTaskDelay(pdMS_TO_TICKS(500)); DEVICE_RESTART(); }
        if (strcmp(cmd, "telemetry") == 0) sendTelemetry();
        if (strcmp(cmd, "otaCheck")  == 0) doOtaCheck();
        if (strcmp(cmd, "configRestore") == 0) {
            if (s_provisionTopic[0] && MQTT_CONNECTED()) {
                HwConfig hw;
                loadHwConfig(hw);
                hw.provisioned = false;
                saveHwConfig(hw);
                publishStart();
                MQTT_SUBSCRIBE(s_provisionTopic, 0);
                logMessage("info", "Config restore requested — provisioned=false, awaiting provision payload");
            } else {
                logMessage("warn", "Config restore failed — not connected");
            }
        }
        if (strcmp(cmd, "configBackup") == 0) {
            char backupTopic[80];
            buildTopic(backupTopic, sizeof(backupTopic), TOPIC_PROVISION_BACKUP_SUFFIX);
            String cfgJson = exportProvisioningConfig();
            MQTT_PUBLISH(backupTopic, 1, false, cfgJson.c_str());
            logMessageFmt("info", "Config backup -> %s", backupTopic);
        }
        if (strcmp(cmd, "wifiOn")    == 0) apRequested = true;
        if (strcmp(cmd, "wifiOff")   == 0) {
            if (STATE_GET(apMode)) {
                dnsServer.stop();
                WiFi.softAPdisconnect(true);
                WiFi.mode(WIFI_STA);
                STATE_SET(apMode, false);
                logMessage("info", "AP closed");
            }
        }
    }
}

// ─── MQTT callbacks ───────────────────────────────────────────────────────────

static void onMqttConnect(bool) {
    STATE_SET(mqttConnected, true);
    logMessageFmt("info", "MQTT connected (heap:%d)", FREE_HEAP());
    ledSetState(LED_MQTT_OK);

    snprintf(s_cmdTopic, sizeof(s_cmdTopic), "%s%lu%s",
             mqttCfgData.prefix,
             (unsigned long)STATE_GET(chipId),
             TOPIC_CMD_SUFFIX);
    snprintf(s_provisionTopic, sizeof(s_provisionTopic), "%s%lu%s",
             mqttCfgData.prefix,
             (unsigned long)STATE_GET(chipId),
             TOPIC_PROVISION_SUFFIX);
    s_needsSubscribe      = true;
    s_publishStartSent    = false;
    s_subscribeFailLogged = false;
    s_provisionPending    = false;
    s_connectedAtMs       = millis();
}

static void onMqttDisconnect(espMqttClientTypes::DisconnectReason reason) {
    STATE_SET(mqttConnected, false);
    s_needsSubscribe     = false;
    s_publishStartSent   = false;
    s_subscribeFailLogged = false;
    s_provisionPending   = false;
    ledSetState(LED_CONNECTING);
    logMessageFmt("warn", "MQTT disconnected: %s", espMqttClientTypes::disconnectReasonToString(reason));
}

static void onMqttSubscribe(uint16_t packetId, const espMqttClientTypes::SubscribeReturncode* codes, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (codes[i] == espMqttClientTypes::SubscribeReturncode::FAIL)
            logMessageFmt("error", "Subscribe ACK REJECTED (id:%d)", packetId);
        else
            logMessageFmt("info", "Subscribe ACK ok (id:%d)", packetId);
    }
    if (!s_publishStartSent) {
        publishStart();
        s_publishStartSent = true;
    }
}

static void onMqttMessage(const espMqttClientTypes::MessageProperties&,
                           const char* topic, const uint8_t* payload,
                           size_t len, size_t index, size_t total) {
    if (index != 0 || len != total) return;  // ignore fragmented messages

    if (s_provisionTopic[0] && strcmp(topic, s_provisionTopic) == 0) {
        s_provisionPayload = "";
        s_provisionPayload.concat((const char*)payload, len);
        s_provisionPending = true;
        logMessage("info", "Provision config received");
        return;
    }

    MqttCommand cmd;
    size_t copyLen = len < sizeof(cmd.payload) - 1 ? len : sizeof(cmd.payload) - 1;
    memcpy(cmd.payload, payload, copyLen);
    cmd.payload[copyLen] = '\0';
    logMessageFmt("info", "MQTT rx [%s]: %s", topic, cmd.payload);

    // Ignore external MQTT commands if ignoreCmd mode is enabled or system is not ready yet
    if (getIgnoreCmdMode()) {
        logMessageFmt("info", "MQTT command ignored [%s]: ignoreCmd mode active", topic);
        return;
    }

    xQueueSend(cmdQueue, &cmd, 0);
}

// ─── MQTT init ────────────────────────────────────────────────────────────────

static void initMqtt() {
    STATE_LOCK();
    static char sysname[17];
    strncpy(sysname, sysState.sysname, sizeof(sysname) - 1);
    sysname[sizeof(sysname) - 1] = '\0';
    STATE_UNLOCK();

    if (mqttCfgData.tls) {
        if (!mqttSecure) mqttSecure = new espMqttClientSecure();
        mqttSecure->setInsecure();
        mqttSecure->onConnect(onMqttConnect);
        mqttSecure->onDisconnect(onMqttDisconnect);
        mqttSecure->onSubscribe(onMqttSubscribe);
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
        mqttPlain->onSubscribe(onMqttSubscribe);
        mqttPlain->onMessage(onMqttMessage);
        mqttPlain->setServer(mqttCfgData.broker, mqttCfgData.port);
        mqttPlain->setClientId(sysname);
        mqttPlain->setKeepAlive(60);
        mqttPlain->setCleanSession(true);
        mqttPlain->connect();
    }
    logMessageFmt("info", "MQTT connecting to %s:%d%s", mqttCfgData.broker, mqttCfgData.port, mqttCfgData.tls ? " (TLS)" : "");
}

// ─── WiFi reconnect helpers ───────────────────────────────────────────────────
//
// wifiReconnectTick() implements a three-phase reconnect strategy:
//   Phase 1 (0..PRIMARY_MS):            retry primary only, every ATTEMPT_MS
//   Phase 2 (PRIMARY..PRIMARY+SEC_MS):  retry secondary only (if configured)
//   Phase 3 (PRIMARY+SEC_MS+):          AP raised, alternate primary/secondary every AP_RETRY_MS
//
// Call every loop iteration while WiFi.status() != WL_CONNECTED.
// Call wifiReconnected() once when WiFi.status() == WL_CONNECTED again.

static void wifiReconnectTick(const char* ssid1, const char* pass1,
                               const char* ssid2, const char* pass2) {
    uint32_t now     = millis();
    uint32_t lostFor = now - s_wifiLostAt;
    bool     hasSec  = (ssid2 && strlen(ssid2) > 0);

    // Raise AP when entering phase 3
    if (!s_reconnApRaised &&
            lostFor >= WIFI_RECONN_PRIMARY_MS + WIFI_RECONN_SECONDARY_MS) {
        s_reconnApRaised = true;
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(g_apSsid);
        dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
        STATE_SET(apMode, true);
        ledSetState(LED_AP);
        logMessageFmt("warn", "No network — AP raised: %s", g_apSsid);
    }

    // Rate-limited: phase 3 uses a longer interval to avoid hammering the radio
    uint32_t retryMs = s_reconnApRaised ? WIFI_RECONN_AP_RETRY_MS : WIFI_RECONN_ATTEMPT_MS;
    if (now - s_lastWifiAttempt < retryMs) return;
    s_lastWifiAttempt = now;

    const char* trySsid;
    const char* tryPass;
    if (!s_reconnApRaised) {
        // Phases 1 & 2: straight primary → secondary progression
        if (lostFor < WIFI_RECONN_PRIMARY_MS || !hasSec) {
            trySsid = ssid1; tryPass = pass1;
            logMessageFmt("warn", "WiFi reconnect → %s", ssid1);
        } else {
            trySsid = ssid2; tryPass = pass2;
            logMessageFmt("warn", "WiFi reconnect → %s (secondary)", ssid2);
        }
    } else {
        // Phase 3: alternate primary / secondary every tick
        if (hasSec) s_wifiAltSecondary = !s_wifiAltSecondary;
        if (s_wifiAltSecondary && hasSec) {
            trySsid = ssid2; tryPass = pass2;
            logMessageFmt("warn", "WiFi retry → %s", ssid2);
        } else {
            trySsid = ssid1; tryPass = pass1;
            logMessageFmt("warn", "WiFi retry → %s", ssid1);
        }
    }
    WiFi.disconnect(false);
    WiFi.begin(trySsid, tryPass);
}

// Call once when WiFi transitions to connected. Cleans up any reconnect-raised AP.
static void wifiReconnected() {
    logMessageFmt("info", "WiFi connected: %s", WiFi.localIP().toString().c_str());
    STATE_SET(wifiConnected, true);
    s_wifiWasConnected   = true;
    s_wifiLostAt         = 0;
    s_lastWifiAttempt    = 0;
    s_wifiAltSecondary   = false;
    if (s_reconnApRaised) {
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        STATE_SET(apMode, false);
        s_reconnApRaised = false;
        logMessage("info", "Network restored — AP closed");
    }
    ledSetState(LED_CONNECTED);
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

    logMessageFmt("warn", "AP: %s @ 192.168.4.1 (3 min)", g_apSsid);
    ledSetState(hasSta ? LED_CONNECTING : LED_AP);

    if (hasPrimary) {
        WiFi.setHostname(g_apSsid);
        WiFi.begin(ssid1, pass1);
        logMessageFmt("info", "STA -> %s", ssid1);
    } else if (hasSecondary) {
        WiFi.setHostname(g_apSsid);
        WiFi.begin(ssid2, pass2);
        logMessageFmt("info", "STA -> %s", ssid2);
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
            logMessageFmt("info", "WiFi STA: %s", WiFi.localIP().toString().c_str());
            STATE_SET(wifiConnected, true);
            return true;
        }

        if (!triedSecondary && millis() - staStart > WIFI_PRIMARY_TIMEOUT_MS) {
            triedSecondary = true;
            WiFi.disconnect(false);
            WiFi.begin(ssid2, pass2);
            logMessageFmt("info", "STA fallback -> %s", ssid2);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    logMessage("warn", "AP window expired — no STA");
    return false;
}

// ─── Deferred subscribe helper ────────────────────────────────────────────────

static void tryDeferredSubscribe() {
    if (!s_needsSubscribe || !MQTT_CONNECTED()) return;
    // Wait 500 ms after CONNACK before subscribing — gives the TCP connection
    // time to settle on slow/mobile links before we send the SUBSCRIBE packet.
    if (millis() - s_connectedAtMs < 500) return;
    uint16_t subId = 0;
    if (mqttSecure) subId = mqttSecure->subscribe(s_cmdTopic, 0);
    else if (mqttPlain) subId = mqttPlain->subscribe(s_cmdTopic, 0);
    if (subId) {
        logMessageFmt("info", "Subscribed: %s", s_cmdTopic);
        // Also subscribe to provisioning topic (QoS 0 — retained msg delivery only)
        if (mqttSecure) mqttSecure->subscribe(s_provisionTopic, 0);
        else if (mqttPlain) mqttPlain->subscribe(s_provisionTopic, 0);
        logMessageFmt("info", "Subscribed: %s", s_provisionTopic);
        s_needsSubscribe     = false;
        s_subscribeFailLogged = false;
    } else if (!s_subscribeFailLogged) {
        s_subscribeFailLogged = true;
#ifdef ESP8266
        logMessageFmt("error", "Subscribe FAILED heap: %d maxBlock: %d", FREE_HEAP(), ESP.getMaxFreeBlockSize());
#else
        logMessageFmt("error", "Subscribe FAILED heap: %d", FREE_HEAP());
#endif
    }
    if (!s_publishStartSent) { publishStart(); s_publishStartSent = true; }
}

// ─── Uplink task ──────────────────────────────────────────────────────────────

void uplinkTask(void* pvParameters) {
    loadMqttConfig(mqttCfgData);

    snprintf(g_apSsid, sizeof(g_apSsid), "AirMQ-SN-%lu",
             (unsigned long)STATE_GET(chipId));

    char wifiSsid[33] = {}, wifiPass[65] = {};
    char wifiSsid2[33] = {}, wifiPass2[65] = {};
    loadWifiCreds(wifiSsid, sizeof(wifiSsid), wifiPass, sizeof(wifiPass),
                  wifiSsid2, sizeof(wifiSsid2), wifiPass2, sizeof(wifiPass2));

    bool staConnected = wifiApWindow(wifiSsid, wifiPass, wifiSsid2, wifiPass2);

    if (!staConnected) {
        // wifiApWindow already exhausted primary+secondary; enter phase 3 immediately
        logMessage("warn", "No STA — AP up, retrying networks");
        ledSetState(LED_AP);
        s_reconnApRaised   = true;  // AP already up from wifiApWindow
        s_wifiLostAt       = millis() - (WIFI_RECONN_PRIMARY_MS + WIFI_RECONN_SECONDARY_MS);
        s_lastWifiAttempt  = 0;     // trigger first retry immediately
        s_wifiAltSecondary = false;
        for (;;) {
            dnsServer.processNextRequest();
            MqttCommand cmd;
            if (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE) handleCommand(cmd.payload);
            SensorReading r;
            while (xQueueReceive(sensorQueue, &r, 0) == pdTRUE) {}
            if (WiFi.status() == WL_CONNECTED) {
                wifiReconnected();
                break;
            }
            wifiReconnectTick(wifiSsid, wifiPass, wifiSsid2, wifiPass2);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    // Tear down AP (idempotent — wifiReconnected() may have already done this)
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    STATE_SET(apMode, false);
    logMessage("info", "AP closed, STA mode");
    ledSetState(LED_CONNECTED);

    configTzTime(MYTZ, "time.google.com", "pool.ntp.org");

    initMqtt();

    SensorReading batch[10];
    uint8_t  batchCount = 0;
    uint32_t lastTele   = 0;
    s_wifiWasConnected = true;  // WiFi is up at this point

    for (;;) {
        MQTT_LOOP();

        tryDeferredSubscribe();

        SensorReading reading;
        if (xQueueReceive(sensorQueue, &reading, 0) == pdTRUE) {
            if (!getMaintenanceMode()) {
                reading.time = getEpochTime();
                if (batchCount < 10)
                    batch[batchCount++] = reading;
                else {
                    logMessage("warn", "Batch overflow — dropping oldest");
                    memmove(batch, batch + 1, sizeof(SensorReading) * 9);
                    batch[9] = reading;
                }
            }
        }
        if (s_provisionPending) {
            s_provisionPending = false;
            handleProvisioningConfig(s_provisionPayload.c_str());
        }

        MqttCommand cmd;
        if (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE)
            handleCommand(cmd.payload);

        // Enable sensors after startup command window expires
        if (s_startupWindowMs && !s_sensorsStarted &&
            millis() - s_startupWindowMs >= STARTUP_CMD_WINDOW_MS) {
            s_sensorsStarted = true;
            if (getDeepSleepMode() && !getMaintenanceMode()) {
                logMessageFmt("debug", "Startup window: deep sleep mode — onTime=%ds, interval=%dm", STATE_GET(onTime), STATE_GET(teleIntervalM));
                sensorsEnableDeepSleep();
            } else {
                logMessageFmt("debug", "Startup window: normal mode%s", (getDeepSleepMode() ? " (maintenance override)" : ""));
                sensorsEnable();
            }
        }

        int8_t sn = STATE_GET(sampleNum);
        if (batchCount >= sn && batchCount > 0) {
            publishSensorData(batch, batchCount);
            STATE_LOCK();
            sysState.readingCount += batchCount;
            STATE_UNLOCK();
            sendTelemetry();
            batchCount = 0;
            lastTele = millis() / 1000;

            if (getDeepSleepMode() && !getMaintenanceMode()) {
                uint32_t sleepSec = (uint32_t)STATE_GET(teleIntervalM) * 60UL;
                logMessageFmt("debug", "Deep sleep trigger: flushing MQTT, then sleeping %ds", sleepSec);
                publishSleepStatus(sleepSec);
                // Flush MQTT outbox before sleeping
                for (int i = 0; i < 30; i++) { MQTT_LOOP(); vTaskDelay(pdMS_TO_TICKS(10)); }
                enterDeepSleep(sleepSec);
            }
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
            logMessageFmt("info", "AP re-enabled: %s", g_apSsid);
        }

        if (WiFi.status() != WL_CONNECTED) {
            if (s_wifiWasConnected) {
                s_wifiWasConnected  = false;
                s_wifiLostAt        = millis();
                s_lastWifiAttempt   = 0;
                s_wifiAltSecondary  = false;
                STATE_SET(wifiConnected, false);
                logMessage("warn", "WiFi lost");
            }
            ledSetState(LED_CONNECTING);
            wifiReconnectTick(wifiSsid, wifiPass, wifiSsid2, wifiPass2);
        } else {
            if (!s_wifiWasConnected) {
                wifiReconnected();
                configTzTime(MYTZ, "time.google.com", "pool.ntp.org");
            }
            STATE_SET(wifiConnected, true);
        }

        static uint32_t s_lastMqttAttempt = 0;
        if (!MQTT_CONNECTED() && WiFi.status() == WL_CONNECTED) {
            uint32_t now32 = millis();
            uint32_t intervalMs = (uint32_t)mqttCfgData.reconnIntervalS * 1000;
            if (now32 - s_lastMqttAttempt >= intervalMs) {
                s_lastMqttAttempt = now32;
                logMessage("warn", "MQTT reconnecting...");
                MQTT_CONNECT();
            }
        }

        STATE_SET(mqttConnected, MQTT_CONNECTED());

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

#ifdef ESP8266
// ─── ESP8266 cooperative uplink ───────────────────────────────────────────────

void uplinkInit() {
    loadMqttConfig(mqttCfgData);
    snprintf(g_apSsid, sizeof(g_apSsid), "AirMQ-SN-%lu",
             (unsigned long)STATE_GET(chipId));
    loadWifiCreds(s_ssid, sizeof(s_ssid), s_pass, sizeof(s_pass),
                  s_ssid2, sizeof(s_ssid2), s_pass2, sizeof(s_pass2));

    // If no saved creds (e.g. ESPEasy used SPIFFS which got erased on first
    // LittleFS mount), fall back to SDK-cached credentials — ESP8266 SDK stores
    // the last-used AP in reserved flash sectors that survive OTA and FS reformats.
    if (strlen(s_ssid) == 0) {
        String sdkSsid = WiFi.SSID();
        if (sdkSsid.length() > 0) {
            strncpy(s_ssid, sdkSsid.c_str(), sizeof(s_ssid) - 1);
            strncpy(s_pass, WiFi.psk().c_str(), sizeof(s_pass) - 1);
            logMessageFmt("warn", "No saved WiFi — SDK cache: %s", sdkSsid.c_str());
            if (lfsReady) saveWifiCreds(s_ssid, s_pass, s_ssid2, s_pass2);
        }
    }

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
        logMessageFmt("info", "STA -> %s", s_ssid);
    } else if (hasSecondary) {
        WiFi.setHostname(g_apSsid);
        WiFi.begin(s_ssid2, s_pass2);
        logMessageFmt("info", "STA -> %s", s_ssid2);
    }

    s_apWindowStart  = millis();
    s_triedSecondary = !hasSecondary || !hasPrimary;
    s_uplinkState    = US_AP_WINDOW;
    ledSetState(hasSta ? LED_CONNECTING : LED_AP);
    logMessageFmt("warn", "AP: %s @ 192.168.4.1 (%ds window)", g_apSsid, AP_WINDOW_MS / 1000);
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
            logMessageFmt("info", "WiFi STA: %s", WiFi.localIP().toString().c_str());
            dnsServer.stop();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            STATE_SET(apMode, false);
            STATE_SET(wifiConnected, true);
            s_wifiWasConnected = true;
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
            logMessageFmt("info", "STA fallback -> %s", s_ssid2);
        }
        if (millis() - s_apWindowStart > AP_WINDOW_MS) {
            logMessage("warn", "AP window expired — no STA, retrying networks");
            ledSetState(LED_AP);
            s_uplinkState      = US_AP_ONLY;
            s_reconnApRaised   = true;  // AP already up from uplinkInit
            s_wifiLostAt       = millis() - (WIFI_RECONN_PRIMARY_MS + WIFI_RECONN_SECONDARY_MS);
            s_lastWifiAttempt  = 0;     // trigger first retry immediately
            s_wifiAltSecondary = false;
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
        wifiReconnectTick(s_ssid, s_pass, s_ssid2, s_pass2);
        if (WiFi.status() == WL_CONNECTED) {
            wifiReconnected();  // closes AP, sets LED_CONNECTED
            configTzTime(MYTZ, "time.google.com", "pool.ntp.org");
            initMqtt();
            s_uplinkState = US_CONNECTED;
        }
        break;

    case US_CONNECTED:
        MQTT_LOOP();
        {
            tryDeferredSubscribe();

            SensorReading reading;
            if (xQueueReceive(sensorQueue, &reading, 0) == pdTRUE) {
                if (!getMaintenanceMode()) {
                    reading.time = getEpochTime();
                    if (s_batchCount < 10)
                        s_batch[s_batchCount++] = reading;
                    else {
                        memmove(s_batch, s_batch + 1, sizeof(SensorReading) * 9);
                        s_batch[9] = reading;
                    }
                }
            }
            if (s_provisionPending) {
                s_provisionPending = false;
                handleProvisioningConfig(s_provisionPayload.c_str());
            }

            MqttCommand cmd;
            if (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE)
                handleCommand(cmd.payload);

            // Enable sensors after startup command window expires
            if (s_startupWindowMs && !s_sensorsStarted &&
                millis() - s_startupWindowMs >= STARTUP_CMD_WINDOW_MS) {
                s_sensorsStarted = true;
                if (getDeepSleepMode() && !getMaintenanceMode()) {
                    logMessageFmt("debug", "Startup window: deep sleep mode — onTime=%ds, interval=%dm", STATE_GET(onTime), STATE_GET(teleIntervalM));
                    sensorsEnableDeepSleep();
                } else {
                    logMessageFmt("debug", "Startup window: normal mode%s", (getDeepSleepMode() ? " (maintenance override)" : ""));
                    sensorsEnable();
                }
            }

            int8_t sn = STATE_GET(sampleNum);
            if (s_batchCount >= sn && s_batchCount > 0) {
                publishSensorData(s_batch, s_batchCount);
                STATE_LOCK();
                sysState.readingCount += s_batchCount;
                STATE_UNLOCK();
                sendTelemetry();
                s_batchCount = 0;
                s_lastTele   = millis() / 1000;

                if (getDeepSleepMode() && !getMaintenanceMode()) {
                    uint32_t sleepSec = (uint32_t)STATE_GET(teleIntervalM) * 60UL;
                    logMessageFmt("debug", "Deep sleep trigger: flushing MQTT, then sleeping %ds", sleepSec);
                    publishSleepStatus(sleepSec);
                    MQTT_LOOP();
                    delay(200);
                    enterDeepSleep(sleepSec);
                }
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
                logMessageFmt("info", "AP re-enabled: %s", g_apSsid);
            }

            if (WiFi.status() != WL_CONNECTED) {
                if (s_wifiWasConnected) {
                    s_wifiWasConnected  = false;
                    s_wifiLostAt        = millis();
                    s_lastWifiAttempt   = 0;
                    s_wifiAltSecondary  = false;
                    STATE_SET(wifiConnected, false);
                    logMessage("warn", "WiFi lost");
                }
                ledSetState(LED_CONNECTING);
                wifiReconnectTick(s_ssid, s_pass, s_ssid2, s_pass2);
            } else {
                if (!s_wifiWasConnected) {
                    wifiReconnected();
                    configTzTime(MYTZ, "time.google.com", "pool.ntp.org");
                }
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
