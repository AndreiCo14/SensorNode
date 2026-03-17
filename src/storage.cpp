#include "storage.h"
#include "config.h"
#include "settings.h"
#include "board.h"
#include "logger.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

JsonDocument sensorConfData;
JsonDocument sensorSetupData;
SemaphoreHandle_t sensorConfMutex  = NULL;
SemaphoreHandle_t sensorSetupMutex = NULL;
bool lfsReady = false;

// ─── Init ─────────────────────────────────────────────────────────────────────

bool storageInit() {
    sensorConfMutex  = xSemaphoreCreateMutex();
    sensorSetupMutex = xSemaphoreCreateMutex();

#ifdef ESP8266
    bool ok = LittleFS.begin();
#else
    bool ok = LittleFS.begin(true, "/littlefs", 10, "storage");
#endif
    if (!ok) {
        logMessage("LittleFS mount failed", "error");
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
        cfg.port = DEFAULT_MQTT_PORT;
        strncpy(cfg.prefix, DEFAULT_MQTT_PREFIX, sizeof(cfg.prefix) - 1);
        cfg.tls  = DEFAULT_MQTT_TLS;
        return false;
    }
    strncpy(cfg.broker, doc["broker"] | DEFAULT_MQTT_BROKER, sizeof(cfg.broker) - 1);
    cfg.port = doc["port"] | DEFAULT_MQTT_PORT;
    strncpy(cfg.prefix, doc["prefix"] | DEFAULT_MQTT_PREFIX, sizeof(cfg.prefix) - 1);
    cfg.tls  = doc["tls"]  | DEFAULT_MQTT_TLS;
    cfg.broker[sizeof(cfg.broker)-1] = '\0';
    cfg.prefix[sizeof(cfg.prefix)-1] = '\0';
    return true;
}

bool saveMqttConfig(const MqttConfig& cfg) {
    JsonDocument doc;
    doc["broker"] = cfg.broker;
    doc["port"]   = cfg.port;
    doc["prefix"] = cfg.prefix;
    doc["tls"]    = cfg.tls;
    return writeJson(MQTT_CONF_PATH, doc);
}

// ─── Hardware config ──────────────────────────────────────────────────────────

bool loadHwConfig(HwConfig& cfg) {
    // Apply board defaults first
    cfg.i2c_sda     = DEFAULT_I2C_SDA;
    cfg.i2c_scl     = DEFAULT_I2C_SCL;
    cfg.uart_rx     = DEFAULT_UART_RX;
    cfg.uart_tx     = DEFAULT_UART_TX;
    cfg.onewire     = DEFAULT_ONEWIRE;
    cfg.led_pin     = DEFAULT_LED_PIN;
    cfg.intervalSec = 60;

    JsonDocument doc;
    if (!readJson(HW_CONF_PATH, doc)) return false;

    if (!doc["i2c_sda"].isNull())   cfg.i2c_sda     = doc["i2c_sda"].as<int8_t>();
    if (!doc["i2c_scl"].isNull())   cfg.i2c_scl     = doc["i2c_scl"].as<int8_t>();
    if (!doc["uart_rx"].isNull())   cfg.uart_rx     = doc["uart_rx"].as<int8_t>();
    if (!doc["uart_tx"].isNull())   cfg.uart_tx     = doc["uart_tx"].as<int8_t>();
    if (!doc["onewire"].isNull())   cfg.onewire     = doc["onewire"].as<int8_t>();
    if (!doc["led_pin"].isNull())   cfg.led_pin     = doc["led_pin"].as<int8_t>();
    if (!doc["interval"].isNull())  cfg.intervalSec = doc["interval"].as<uint16_t>();
    return true;
}

bool saveHwConfig(const HwConfig& cfg) {
    JsonDocument doc;
    doc["i2c_sda"]  = cfg.i2c_sda;
    doc["i2c_scl"]  = cfg.i2c_scl;
    doc["uart_rx"]  = cfg.uart_rx;
    doc["uart_tx"]  = cfg.uart_tx;
    doc["onewire"]  = cfg.onewire;
    doc["led_pin"]  = cfg.led_pin;
    doc["interval"] = cfg.intervalSec;
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

// ─── Sensor label config ──────────────────────────────────────────────────────

bool loadSensorConf() {
    if (!LittleFS.exists(SENSORCONF_PATH)) return false;
    File f = LittleFS.open(SENSORCONF_PATH, "r");
    if (!f || f.size() == 0) { f.close(); return false; }
    bool ok = false;
    if (xSemaphoreTake(sensorConfMutex, pdMS_TO_TICKS(1000))) {
        DeserializationError err = deserializeJson(sensorConfData, f);
        xSemaphoreGive(sensorConfMutex);
        ok = !err;
    }
    f.close();
    if (ok) logMessage("sensorconf loaded", "info");
    return ok;
}

bool saveSensorConf() {
    File f = LittleFS.open(SENSORCONF_PATH, "w");
    if (!f) return false;
    bool ok = false;
    if (xSemaphoreTake(sensorConfMutex, pdMS_TO_TICKS(1000))) {
        ok = (serializeJson(sensorConfData, f) > 0);
        xSemaphoreGive(sensorConfMutex);
    }
    f.close();
    logMessage(ok ? "sensorconf saved" : "sensorconf write failed", ok ? "info" : "error");
    return ok;
}
