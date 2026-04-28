#include "storage.h"
#include "config.h"
#include "settings.h"
#include "board.h"
#include "logger.h"
#include <LittleFS.h>
#ifdef ESP8266
// SPIFFS global (extern fs::FS SPIFFS) declared in FS.h via LittleFS.h — no extra include needed
#else
#  include "esp_littlefs.h"
#  include "esp_partition.h"
#endif
#include <ArduinoJson.h>

JsonDocument sensorSetupData;
SemaphoreHandle_t sensorSetupMutex = NULL;
bool lfsReady = false;

// ─── Init ─────────────────────────────────────────────────────────────────────

bool storageInit() {
    sensorSetupMutex = xSemaphoreCreateMutex();

#ifdef ESP8266
    // Before touching LittleFS: try mounting as SPIFFS (used by ESPEasy and
    // older firmware). If it succeeds and security.dat is present, extract WiFi
    // credentials now — they'll be saved to our LittleFS config after it's ready.
    // SecurityStruct layout: WifiSSID[32] | WifiKey[64] | WifiSSID2[32] | WifiKey2[64]
    static char spiffsSSID[33]  = {};
    static char spiffsPass[65]  = {};
    static char spiffsSSID2[33] = {};
    static char spiffsPass2[65] = {};
    bool migratedSpiffs = false;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    SPIFFS.setConfig(SPIFFSConfig(false));  // never auto-format — we own this partition
    if (SPIFFS.begin()) {
        const char* secPath = SPIFFS.exists("/security.dat") ? "/security.dat"
                            : SPIFFS.exists("security.dat")  ? "security.dat"
                            : nullptr;
        if (secPath) {
            File f = SPIFFS.open(secPath, "r");
            if (f && f.size() >= 96) {
                f.readBytes(spiffsSSID,  sizeof(spiffsSSID)  - 1);
                f.readBytes(spiffsPass,  sizeof(spiffsPass)  - 1);
                if (f.size() >= 192) {
                    f.readBytes(spiffsSSID2, sizeof(spiffsSSID2) - 1);
                    f.readBytes(spiffsPass2, sizeof(spiffsPass2) - 1);
                }
                migratedSpiffs = strlen(spiffsSSID) > 0;
            }
            if (f) f.close();
        }
        SPIFFS.end();
        if (migratedSpiffs)
            logMessageFmt("warn", "SPIFFS security.dat: migrating SSID=%s", spiffsSSID);
        else
            logMessageFmt("warn", "SPIFFS mounted but no usable security.dat");
    }
#pragma GCC diagnostic pop

    bool ok = LittleFS.begin();
    if (!ok) {
        logMessageFmt("warn", "LittleFS mount failed — formatting");
        LittleFS.format();
        ok = LittleFS.begin();
    }
#else
    // Try known partition labels in order: our preferred label first,
    // then fallbacks for common third-party firmware layouts (ESPEasy etc.)
    //
    // After a failed begin(), esp_vfs_littlefs_unregister() clears stale VFS
    // state (LittleFS.end() is a no-op when _started=false).
    //
    // If a partition exists but contains foreign/corrupt data (e.g. SPIFFS),
    // LittleFS.format() fails because lfs_format() can't overwrite SPIFFS
    // superblocks while the WDT disable is broken in early setup().
    // Fix: erase just the first two 4KB sectors (LittleFS superblocks 0+1)
    // directly via esp_partition_erase_range(), then begin(formatOnFail=true)
    // succeeds on blank flash without needing WDT manipulation.
    static const char* const labels[] = {"storage", "spiffs", "littlefs", "ffat", nullptr};
    bool ok = false;
    const char* usedLabel = nullptr;
    for (int i = 0; labels[i] && !ok; i++) {
        esp_vfs_littlefs_unregister(labels[i]);
        ok = LittleFS.begin(false, "/littlefs", 10, labels[i]);
        if (ok) { usedLabel = labels[i]; break; }

        // Partition found but mount failed — check it actually exists first
        const esp_partition_t* p = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, labels[i]);
        if (!p) continue;

        // Erase superblock sectors so lfs_format() gets clean flash
        logMessageFmt("warn", "Erasing '%s", labels[i] + "' superblocks for reformat");
        esp_partition_erase_range(p, 0, 0x2000); // blocks 0+1 = 8KB
        esp_vfs_littlefs_unregister(labels[i]);
        ok = LittleFS.begin(true, "/littlefs", 10, labels[i]);
        if (ok) {
            usedLabel = labels[i];
            logMessageFmt("warn", "LittleFS reformatted '%s", labels[i] + "' (migration)");
        }
    }
    if (ok && usedLabel && strcmp(usedLabel, "storage") != 0)
        logMessageFmt("warn", "LittleFS mounted on partition '%s", usedLabel + "'");
#endif
    if (!ok) {
        logMessageFmt("error", "LittleFS unavailable — no compatible partition found");
        return false;
    }

#ifdef ESP8266
    if (migratedSpiffs && !LittleFS.exists(WIFI_CONF_PATH))
        saveWifiCreds(spiffsSSID, spiffsPass, spiffsSSID2, spiffsPass2);
#endif

    File root = LittleFS.open("/", "r");
    File entry = root.openNextFile();
    while (entry) {
        logMessageFmt("info", "FS: /%s", entry.name() + " (" + entry.size() + " bytes)");
        entry = root.openNextFile();
    }
    root.close();

    lfsReady = true;
    logMessageFmt("info", "LittleFS mounted");
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
    if (!readJson(WIFI_CONF_PATH, doc)) {
#ifdef ESP8266
        // Try ESPEasy security.dat — survives OTA; binary struct layout:
        //   offset   0: WifiSSID[32]
        //   offset  32: WifiKey[64]
        //   offset  96: WifiSSID2[32]
        //   offset 128: WifiKey2[64]
        if (lfsReady && LittleFS.exists("/security.dat")) {
            File f = LittleFS.open("/security.dat", "r");
            if (f && f.size() >= 96) {
                memset(ssid, 0, ssidLen);
                memset(pass, 0, passLen);
                f.readBytes(ssid, min((size_t)32, ssidLen - 1));
                f.readBytes(pass, min((size_t)64, passLen - 1));
                if (f.size() >= 192 && ssid2 && ssid2Len > 1) {
                    memset(ssid2, 0, ssid2Len);
                    memset(pass2, 0, pass2Len);
                    f.readBytes(ssid2, min((size_t)32, ssid2Len - 1));
                    f.readBytes(pass2, min((size_t)64, pass2Len - 1));
                }
                f.close();
                if (strlen(ssid) > 0) {
                    logMessageFmt("warn", "ESPEasy security.dat: using SSID=%s", ssid);
                    saveWifiCreds(ssid, pass, ssid2 ? ssid2 : "", pass2 ? pass2 : "");
                    return true;
                }
            } else if (f) f.close();
        }
#endif
        return false;
    }
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

void clearWifiCreds() {
    LittleFS.remove(WIFI_CONF_PATH);
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
    for (uint8_t i = 0; i < HwConfig::GPIO_CTRL_MAX; i++) cfg.gpio_pin[i] = -1;
    cfg.gpio_count = 0;
    cfg.intervalSec  = 60;
    cfg.teleIntervalM = DEFAULT_TELE_INTERVAL_M;
    cfg.sampleNum    = DEFAULT_SAMPLE_NUM;
    cfg.onTime       = 30;
    cfg.deepSleep    = false;
    cfg.ignoreCmd    = false;

    JsonDocument doc;
    if (!readJson(HW_CONF_PATH, doc)) return false;

    if (!doc["i2c_sda"].isNull())       cfg.i2c_sda      = doc["i2c_sda"].as<int8_t>();
    if (!doc["i2c_scl"].isNull())       cfg.i2c_scl      = doc["i2c_scl"].as<int8_t>();
    if (!doc["uart_rx"].isNull())       cfg.uart_rx      = doc["uart_rx"].as<int8_t>();
    if (!doc["uart_tx"].isNull())       cfg.uart_tx      = doc["uart_tx"].as<int8_t>();
    if (!doc["onewire"].isNull())       cfg.onewire      = doc["onewire"].as<int8_t>();
    if (!doc["led_pin"].isNull())       cfg.led_pin      = doc["led_pin"].as<int8_t>();
    if (!doc["5v_pin"].isNull())        cfg.pin5v        = doc["5v_pin"].as<int8_t>();
    for (JsonPair kv : doc.as<JsonObject>()) {
        const char* key = kv.key().c_str();
        if (strncmp(key, "gpio", 4) != 0 || !isdigit((unsigned char)key[4])) continue;
        if (cfg.gpio_count >= HwConfig::GPIO_CTRL_MAX) break;
        int pin = atoi(key + 4);
        cfg.gpio_pin[cfg.gpio_count] = (int8_t)pin;
        strncpy(cfg.gpio_mode[cfg.gpio_count], kv.value() | "invert", 7);
        cfg.gpio_mode[cfg.gpio_count][7] = '\0';
        cfg.gpio_count++;
    }
    if (!doc["interval"].isNull())      cfg.intervalSec  = doc["interval"].as<uint16_t>();
    if (!doc["teleIntervalM"].isNull()) cfg.teleIntervalM = doc["teleIntervalM"].as<uint16_t>();
    if (!doc["sampleNum"].isNull())     cfg.sampleNum    = doc["sampleNum"].as<int8_t>();
    if (!doc["onTime"].isNull()) {
        cfg.onTime = doc["onTime"].as<uint16_t>();
        if (cfg.onTime < 30) cfg.onTime = 30;
    }
    cfg.deepSleep   = doc["deepSleep"]   | false;
    cfg.ignoreCmd   = doc["ignoreCmd"]   | false;
    cfg.provisioned = doc["provisioned"] | false;
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
    for (uint8_t i = 0; i < cfg.gpio_count; i++) {
        if (cfg.gpio_pin[i] < 0) continue;
        char key[10];
        snprintf(key, sizeof(key), "gpio%d", cfg.gpio_pin[i]);
        doc[key] = cfg.gpio_mode[i];
    }
    doc["interval"]      = cfg.intervalSec;
    doc["teleIntervalM"] = cfg.teleIntervalM;
    doc["sampleNum"]     = cfg.sampleNum;
    doc["onTime"]        = cfg.onTime;
    doc["deepSleep"]     = cfg.deepSleep;
    doc["ignoreCmd"]     = cfg.ignoreCmd;
    doc["provisioned"]   = cfg.provisioned;
    return writeJson(HW_CONF_PATH, doc);
}

// ─── Sensor setup ─────────────────────────────────────────────────────────────

bool loadSensorSetup() {
    if (!LittleFS.exists(SENSOR_SETUP_PATH)) {
        logMessageFmt("info", "sensorsetup.json not found — using defaults (all disabled)");
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
    if (ok) logMessageFmt("info", "sensorsetup loaded");
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


// ─── Feature flags ────────────────────────────────────────────────────────────

bool loadFeatures(FeatureFlags& f) {
    f.web   = true;
    f.wsLog = true;
    JsonDocument doc;
    if (!readJson(FEATURES_CONF_PATH, doc)) return false;
    f.web   = doc["web"]   | true;
    f.wsLog = doc["wsLog"] | true;
    return true;
}

bool saveFeatures(const FeatureFlags& f) {
    JsonDocument doc;
    doc["web"]   = f.web;
    doc["wsLog"] = f.wsLog;
    return writeJson(FEATURES_CONF_PATH, doc);
}
