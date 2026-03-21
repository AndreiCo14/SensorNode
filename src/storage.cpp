#include "storage.h"
#include "config.h"
#include "settings.h"
#include "board.h"
#include "logger.h"
#include <LittleFS.h>
#include "esp_littlefs.h"
#include <ArduinoJson.h>

JsonDocument sensorSetupData;
SemaphoreHandle_t sensorSetupMutex = NULL;
bool lfsReady = false;

// ─── Init ─────────────────────────────────────────────────────────────────────

bool storageInit() {
    sensorSetupMutex = xSemaphoreCreateMutex();

#ifdef ESP8266
    bool ok = LittleFS.begin();
    if (!ok) {
        logMessage("LittleFS mount failed — formatting", "warn");
        LittleFS.format();
        ok = LittleFS.begin();
    }
#else
    // Try known partition labels in order: our preferred label first,
    // then fallbacks for common third-party firmware layouts (ESPEasy etc.)
    //
    // After a failed begin(), _started stays false so LittleFS.end() is a
    // no-op — the VFS stays partially registered in ESP-IDF. Call
    // esp_vfs_littlefs_unregister() directly before each attempt to guarantee
    // a clean slate. Then use begin(formatOnFail=true) for mount+format in one
    // shot; formatOnFail calls disableCore0WDT() which may log a warning when
    // called from setup() but the return value is ignored — format proceeds.
    static const char* const labels[] = {"storage", "spiffs", "littlefs", "ffat", nullptr};
    bool ok = false;
    const char* usedLabel = nullptr;
    for (int i = 0; labels[i] && !ok; i++) {
        esp_vfs_littlefs_unregister(labels[i]); // force-clear any stale VFS registration
        ok = LittleFS.begin(true, "/littlefs", 10, labels[i]);
        if (ok) usedLabel = labels[i];
    }
    if (ok && usedLabel && strcmp(usedLabel, "storage") != 0)
        logMessage(String("LittleFS mounted on partition '") + usedLabel + "' (migrated)", "warn");
#endif
    if (!ok) {
        logMessage("LittleFS unavailable — no compatible partition found", "error");
        return false;
    }

    File root = LittleFS.open("/", "r");
    File entry = root.openNextFile();
    while (entry) {
        logMessage(String("FS: /") + entry.name() + " (" + entry.size() + " bytes)", "info");
        entry = root.openNextFile();
    }
    root.close();

    lfsReady = true;
    logMessage("LittleFS mounted", "info");
    return true;
}

// ─── Generic helpers ──────────────────────────────────────────────────────────

static bool readJson(const char* path, JsonDocument& doc) {
    if (!LittleFS.exists(path)) return false;
    File f = LittleFS.open(path, "r");
    if (!f || f.size() == 0) { f.close(); return false; }
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    return !err;
}

static bool writeJson(const char* path, const JsonDocument& doc) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    bool ok = (serializeJson(doc, f) > 0);
    f.close();
    return ok;
}

// ─── WiFi credentials ─────────────────────────────────────────────────────────

bool loadWifiCreds(char* ssid, size_t ssidLen, char* pass, size_t passLen,
                   char* ssid2, size_t ssid2Len, char* pass2, size_t pass2Len) {
    JsonDocument doc;
    if (!readJson(WIFI_CONF_PATH, doc)) return false;
    const char* s  = doc["ssid"]  | "";
    const char* p  = doc["pass"]  | "";
    const char* s2 = doc["ssid2"] | "";
    const char* p2 = doc["pass2"] | "";
    strncpy(ssid,  s,  ssidLen  - 1);  ssid[ssidLen-1]   = '\0';
    strncpy(pass,  p,  passLen  - 1);  pass[passLen-1]   = '\0';
    strncpy(ssid2, s2, ssid2Len - 1);  ssid2[ssid2Len-1] = '\0';
    strncpy(pass2, p2, pass2Len - 1);  pass2[pass2Len-1] = '\0';
    return strlen(ssid) > 0;
}

bool saveWifiCreds(const char* ssid, const char* pass,
                   const char* ssid2, const char* pass2) {
    JsonDocument doc;
    doc["ssid"]  = ssid  ? ssid  : "";
    doc["pass"]  = pass  ? pass  : "";
    doc["ssid2"] = ssid2 ? ssid2 : "";
    doc["pass2"] = pass2 ? pass2 : "";
    return writeJson(WIFI_CONF_PATH, doc);
}

// ─── MQTT config ──────────────────────────────────────────────────────────────

bool loadMqttConfig(MqttConfig& cfg) {
    JsonDocument doc;
    if (!readJson(MQTT_CONF_PATH, doc)) {
        // Return defaults
        strncpy(cfg.broker, DEFAULT_MQTT_BROKER, sizeof(cfg.broker) - 1);
        cfg.port            = DEFAULT_MQTT_PORT;
        strncpy(cfg.prefix, DEFAULT_MQTT_PREFIX, sizeof(cfg.prefix) - 1);
        cfg.tls             = DEFAULT_MQTT_TLS;
        cfg.reconnIntervalS = 5;
        return false;
    }
    strncpy(cfg.broker, doc["broker"] | DEFAULT_MQTT_BROKER, sizeof(cfg.broker) - 1);
    cfg.port = doc["port"] | DEFAULT_MQTT_PORT;
    strncpy(cfg.prefix, doc["prefix"] | DEFAULT_MQTT_PREFIX, sizeof(cfg.prefix) - 1);
    cfg.tls             = doc["tls"]             | DEFAULT_MQTT_TLS;
    cfg.reconnIntervalS = doc["reconnIntervalS"] | (uint16_t)5;
    cfg.broker[sizeof(cfg.broker)-1] = '\0';
    cfg.prefix[sizeof(cfg.prefix)-1] = '\0';
    return true;
}

bool saveMqttConfig(const MqttConfig& cfg) {
    JsonDocument doc;
    doc["broker"]         = cfg.broker;
    doc["port"]           = cfg.port;
    doc["prefix"]         = cfg.prefix;
    doc["tls"]            = cfg.tls;
    doc["reconnIntervalS"] = cfg.reconnIntervalS;
    return writeJson(MQTT_CONF_PATH, doc);
}

// ─── Hardware config ──────────────────────────────────────────────────────────

bool loadHwConfig(HwConfig& cfg) {
    // Apply board defaults first
    cfg.i2c_sda      = DEFAULT_I2C_SDA;
    cfg.i2c_scl      = DEFAULT_I2C_SCL;
    cfg.uart_rx      = DEFAULT_UART_RX;
    cfg.uart_tx      = DEFAULT_UART_TX;
    cfg.onewire      = DEFAULT_ONEWIRE;
    cfg.led_pin      = DEFAULT_LED_PIN;
    cfg.pin5v        = DEFAULT_5V_PIN;
    cfg.intervalSec  = 60;
    cfg.teleIntervalM = DEFAULT_TELE_INTERVAL_M;
    cfg.sampleNum    = DEFAULT_SAMPLE_NUM;
    cfg.onTime       = 30;

    JsonDocument doc;
    if (!readJson(HW_CONF_PATH, doc)) return false;

    if (!doc["i2c_sda"].isNull())       cfg.i2c_sda      = doc["i2c_sda"].as<int8_t>();
    if (!doc["i2c_scl"].isNull())       cfg.i2c_scl      = doc["i2c_scl"].as<int8_t>();
    if (!doc["uart_rx"].isNull())       cfg.uart_rx      = doc["uart_rx"].as<int8_t>();
    if (!doc["uart_tx"].isNull())       cfg.uart_tx      = doc["uart_tx"].as<int8_t>();
    if (!doc["onewire"].isNull())       cfg.onewire      = doc["onewire"].as<int8_t>();
    if (!doc["led_pin"].isNull())       cfg.led_pin      = doc["led_pin"].as<int8_t>();
    if (!doc["5v_pin"].isNull())        cfg.pin5v        = doc["5v_pin"].as<int8_t>();
    if (!doc["interval"].isNull())      cfg.intervalSec  = doc["interval"].as<uint16_t>();
    if (!doc["teleIntervalM"].isNull()) cfg.teleIntervalM = doc["teleIntervalM"].as<uint16_t>();
    if (!doc["sampleNum"].isNull())     cfg.sampleNum    = doc["sampleNum"].as<int8_t>();
    if (!doc["onTime"].isNull()) {
        cfg.onTime = doc["onTime"].as<uint16_t>();
        if (cfg.onTime < 30) cfg.onTime = 30;
    }
    return true;
}

bool saveHwConfig(const HwConfig& cfg) {
    JsonDocument doc;
    doc["i2c_sda"]       = cfg.i2c_sda;
    doc["i2c_scl"]       = cfg.i2c_scl;
    doc["uart_rx"]       = cfg.uart_rx;
    doc["uart_tx"]       = cfg.uart_tx;
    doc["onewire"]       = cfg.onewire;
    doc["led_pin"]       = cfg.led_pin;
    doc["5v_pin"]        = cfg.pin5v;
    doc["interval"]      = cfg.intervalSec;
    doc["teleIntervalM"] = cfg.teleIntervalM;
    doc["sampleNum"]     = cfg.sampleNum;
    doc["onTime"]        = cfg.onTime;
    return writeJson(HW_CONF_PATH, doc);
}

// ─── Sensor setup ─────────────────────────────────────────────────────────────

bool loadSensorSetup() {
    if (!LittleFS.exists(SENSOR_SETUP_PATH)) {
        logMessage("sensorsetup.json not found — using defaults (all disabled)", "info");
        return false;
    }
    File f = LittleFS.open(SENSOR_SETUP_PATH, "r");
    if (!f || f.size() == 0) { f.close(); return false; }
    bool ok = false;
    if (xSemaphoreTake(sensorSetupMutex, pdMS_TO_TICKS(1000))) {
        DeserializationError err = deserializeJson(sensorSetupData, f);
        xSemaphoreGive(sensorSetupMutex);
        ok = !err;
    }
    f.close();
    if (ok) logMessage("sensorsetup loaded", "info");
    return ok;
}

bool saveSensorSetup() {
    File f = LittleFS.open(SENSOR_SETUP_PATH, "w");
    if (!f) return false;
    bool ok = false;
    if (xSemaphoreTake(sensorSetupMutex, pdMS_TO_TICKS(1000))) {
        ok = (serializeJson(sensorSetupData, f) > 0);
        xSemaphoreGive(sensorSetupMutex);
    }
    f.close();
    logMessage(ok ? "sensorsetup saved" : "sensorsetup write failed", ok ? "info" : "error");
    return ok;
}

