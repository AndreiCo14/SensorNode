#include "webserver.h"
#include "config.h"
#include "settings.h"
#include "logger.h"
#include "storage.h"
#include "system_state.h"
#include "platform.h"
#include "queues.h"
#include "index_html.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>

static WebServerT httpServer(80);
static volatile bool rebootPending = false;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static void sendJson(int code, const String& payload) {
    httpServer.send(code, "application/json", payload);
}

static void sendJsonDoc(int code, JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    sendJson(code, out);
}

// ─── GET /api/state ───────────────────────────────────────────────────────────

static void handleGetState() {
    STATE_LOCK();
    uint32_t chipId      = sysState.chipId;
    uint32_t startTime   = sysState.startTime;
    uint32_t readings    = sysState.readingCount;
    uint8_t  active      = sysState.sensorsActive;
    uint16_t teleInt     = sysState.teleIntervalM;
    int8_t   sampleNum   = sysState.sampleNum;
    bool     mqttConn    = sysState.mqttConnected;
    bool     wifiConn    = sysState.wifiConnected;
    bool     apMode      = sysState.apMode;
    char     sysname[17];
    strncpy(sysname, sysState.sysname, sizeof(sysname));
    STATE_UNLOCK();

    uint32_t now = millis() / 1000;

    JsonDocument doc;
    doc["chipId"]        = chipId;
    doc["sysname"]       = sysname;
    doc["uptime"]        = (now - startTime) / 60;
    doc["readingCount"]  = readings;
    doc["sensorsActive"] = active;
    doc["teleInterval"]  = teleInt;
    doc["sampleNum"]     = sampleNum;
    doc["mqttConnected"] = mqttConn;
    doc["wifiConnected"] = wifiConn;
    doc["apMode"]        = apMode;
    doc["freeHeap"]      = (uint32_t)FREE_HEAP();
    doc["build"]         = FW_BUILD;
#ifdef ESP8266
    doc["wifiRSSI"]      = apMode ? 0 : WiFi.RSSI();
#else
    doc["wifiRSSI"]      = apMode ? 0 : WiFi.RSSI();
#endif

    time_t epoch = time(NULL);
    doc["epochTime"]  = (uint32_t)epoch;
    doc["ntpSynced"]  = (epoch > 1000000000UL);

    sendJsonDoc(200, doc);
}

// ─── GET /api/wifi ────────────────────────────────────────────────────────────

static void handleGetWifi() {
    char ssid[33]={}, pass[65]={}, ssid2[33]={}, pass2[65]={};
    loadWifiCreds(ssid, sizeof(ssid), pass, sizeof(pass),
                  ssid2, sizeof(ssid2), pass2, sizeof(pass2));

    JsonDocument doc;
    doc["apMode"]    = STATE_GET(apMode);
    doc["ssid"]      = ssid;
    doc["ssid2"]     = ssid2;
    doc["connected"] = (WiFi.status() == WL_CONNECTED);
    sendJsonDoc(200, doc);
}

// ─── POST /api/wifi ───────────────────────────────────────────────────────────

static void handlePostWifi() {
    if (!httpServer.hasArg("plain")) { sendJson(400, "{\"error\":\"no body\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, httpServer.arg("plain")) || !doc["ssid"].is<const char*>()) {
        sendJson(400, "{\"error\":\"invalid JSON\"}");
        return;
    }
    const char* ssid  = doc["ssid"]  | "";
    const char* pass  = doc["pass"]  | "";
    const char* ssid2 = doc["ssid2"] | "";
    const char* pass2 = doc["pass2"] | "";
    if (strlen(ssid) == 0) { sendJson(400, "{\"error\":\"ssid required\"}"); return; }
    saveWifiCreds(ssid, pass, ssid2, pass2);
    logMessage(String("WiFi saved: ") + ssid, "info");
    sendJson(200, "{\"ok\":true,\"msg\":\"Saved. Rebooting...\"}");
    rebootPending = true;
}

// ─── GET /api/wifi/scan ───────────────────────────────────────────────────────

static void handleGetWifiScan() {
    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n && i < 20; i++) {
        JsonObject net = arr.add<JsonObject>();
        net["ssid"]   = WiFi.SSID(i);
        net["rssi"]   = WiFi.RSSI(i);
#ifdef ESP8266
        net["secure"] = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
#else
        net["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
#endif
    }
    WiFi.scanDelete();
    sendJsonDoc(200, doc);
}

// ─── GET /api/mqtt/config ─────────────────────────────────────────────────────

static void handleGetMqttConfig() {
    MqttConfig cfg;
    loadMqttConfig(cfg);
    JsonDocument doc;
    doc["broker"] = cfg.broker;
    doc["port"]   = cfg.port;
    doc["prefix"] = cfg.prefix;
    doc["tls"]    = cfg.tls;
    sendJsonDoc(200, doc);
}

// ─── POST /api/mqtt/config ────────────────────────────────────────────────────

static void handlePostMqttConfig() {
    if (!httpServer.hasArg("plain")) { sendJson(400, "{\"error\":\"no body\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, httpServer.arg("plain"))) {
        sendJson(400, "{\"error\":\"invalid JSON\"}"); return;
    }
    MqttConfig cfg;
    strncpy(cfg.broker, doc["broker"] | DEFAULT_MQTT_BROKER, sizeof(cfg.broker) - 1);
    cfg.port = doc["port"] | DEFAULT_MQTT_PORT;
    strncpy(cfg.prefix, doc["prefix"] | DEFAULT_MQTT_PREFIX, sizeof(cfg.prefix) - 1);
    cfg.tls  = doc["tls"]  | DEFAULT_MQTT_TLS;
    cfg.broker[sizeof(cfg.broker)-1] = '\0';
    cfg.prefix[sizeof(cfg.prefix)-1] = '\0';
    if (strlen(cfg.broker) == 0) { sendJson(400, "{\"error\":\"broker required\"}"); return; }
    saveMqttConfig(cfg);
    logMessage("MQTT config saved", "info");
    sendJson(200, "{\"ok\":true,\"msg\":\"Saved. Rebooting...\"}");
    rebootPending = true;
}

// ─── GET /api/hw/config ───────────────────────────────────────────────────────

static void handleGetHwConfig() {
    HwConfig cfg;
    loadHwConfig(cfg);
    JsonDocument doc;
    doc["i2c_sda"]       = cfg.i2c_sda;
    doc["i2c_scl"]       = cfg.i2c_scl;
    doc["uart_rx"]       = cfg.uart_rx;
    doc["uart_tx"]       = cfg.uart_tx;
    doc["onewire"]       = cfg.onewire;
    doc["led_pin"]       = cfg.led_pin;
    doc["interval"]      = cfg.intervalSec;
    doc["teleIntervalM"] = cfg.teleIntervalM;
    doc["sampleNum"]     = cfg.sampleNum;
    doc["onTime"]        = cfg.onTime;
    sendJsonDoc(200, doc);
}

// ─── POST /api/hw/config ──────────────────────────────────────────────────────

static void handlePostHwConfig() {
    if (!httpServer.hasArg("plain")) { sendJson(400, "{\"error\":\"no body\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, httpServer.arg("plain"))) {
        sendJson(400, "{\"error\":\"invalid JSON\"}"); return;
    }
    HwConfig cfg;
    loadHwConfig(cfg);  // start from current defaults
    if (!doc["i2c_sda"].isNull())       cfg.i2c_sda       = doc["i2c_sda"].as<int8_t>();
    if (!doc["i2c_scl"].isNull())       cfg.i2c_scl       = doc["i2c_scl"].as<int8_t>();
    if (!doc["uart_rx"].isNull())       cfg.uart_rx       = doc["uart_rx"].as<int8_t>();
    if (!doc["uart_tx"].isNull())       cfg.uart_tx       = doc["uart_tx"].as<int8_t>();
    if (!doc["onewire"].isNull())       cfg.onewire       = doc["onewire"].as<int8_t>();
    if (!doc["led_pin"].isNull())       cfg.led_pin       = doc["led_pin"].as<int8_t>();
    if (!doc["interval"].isNull())      cfg.intervalSec   = doc["interval"].as<uint16_t>();
    if (!doc["teleIntervalM"].isNull()) cfg.teleIntervalM = doc["teleIntervalM"].as<uint16_t>();
    if (!doc["sampleNum"].isNull())     cfg.sampleNum     = doc["sampleNum"].as<int8_t>();
    if (!doc["onTime"].isNull())        cfg.onTime        = doc["onTime"].as<uint16_t>();
    saveHwConfig(cfg);
    logMessage("HW config saved", "info");
    sendJson(200, "{\"ok\":true,\"msg\":\"Saved. Rebooting...\"}");
    rebootPending = true;
}

// ─── GET /api/sensors/setup ───────────────────────────────────────────────────

static void handleGetSensorSetup() {
    if (xSemaphoreTake(sensorSetupMutex, pdMS_TO_TICKS(500))) {
        String out;
        serializeJson(sensorSetupData, out);
        xSemaphoreGive(sensorSetupMutex);
        sendJson(200, out);
    } else {
        sendJson(503, "{\"error\":\"busy\"}");
    }
}

// ─── GET /api/config/export ───────────────────────────────────────────────────

static void handleGetConfigExport() {
    JsonDocument doc;

    MqttConfig mqtt;
    loadMqttConfig(mqtt);
    doc["mqtt"]["broker"] = mqtt.broker;
    doc["mqtt"]["port"]   = mqtt.port;
    doc["mqtt"]["prefix"] = mqtt.prefix;
    doc["mqtt"]["tls"]    = mqtt.tls;

    HwConfig hw;
    loadHwConfig(hw);
    doc["hw"]["i2c_sda"]       = hw.i2c_sda;
    doc["hw"]["i2c_scl"]       = hw.i2c_scl;
    doc["hw"]["uart_rx"]       = hw.uart_rx;
    doc["hw"]["uart_tx"]       = hw.uart_tx;
    doc["hw"]["onewire"]       = hw.onewire;
    doc["hw"]["led_pin"]       = hw.led_pin;
    doc["hw"]["interval"]      = hw.intervalSec;
    doc["hw"]["teleIntervalM"] = hw.teleIntervalM;
    doc["hw"]["sampleNum"]     = hw.sampleNum;
    doc["hw"]["onTime"]        = hw.onTime;

    if (xSemaphoreTake(sensorSetupMutex, pdMS_TO_TICKS(500))) {
        doc["sensorSetup"] = sensorSetupData;
        xSemaphoreGive(sensorSetupMutex);
    }

    String out;
    serializeJsonPretty(doc, out);
    httpServer.sendHeader("Content-Disposition", "attachment; filename=\"sensornode-config.json\"");
    httpServer.send(200, "application/json", out);
}

// ─── POST /api/sensors/setup ──────────────────────────────────────────────────

static void handlePostSensorSetup() {
    if (!httpServer.hasArg("plain")) { sendJson(400, "{\"error\":\"no body\"}"); return; }
    if (xSemaphoreTake(sensorSetupMutex, pdMS_TO_TICKS(1000))) {
        DeserializationError err = deserializeJson(sensorSetupData, httpServer.arg("plain"));
        xSemaphoreGive(sensorSetupMutex);
        if (err) { sendJson(400, "{\"error\":\"invalid JSON\"}"); return; }
        saveSensorSetup();
        sendJson(200, "{\"ok\":true}");
        logMessage("Sensor setup updated via web", "info");
    } else {
        sendJson(503, "{\"error\":\"busy\"}");
    }
}

// ─── POST /api/cmd ────────────────────────────────────────────────────────────

static void handlePostCmd() {
    if (!httpServer.hasArg("plain")) { sendJson(400, "{\"error\":\"no body\"}"); return; }
    MqttCommand cmd;
    strncpy(cmd.payload, httpServer.arg("plain").c_str(), sizeof(cmd.payload) - 1);
    if (xQueueSend(cmdQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE)
        sendJson(200, "{\"ok\":true}");
    else
        sendJson(503, "{\"error\":\"cmdQueue full\"}");
}

// ─── POST /api/ota ────────────────────────────────────────────────────────────

static void handleOtaUpload() {
    HTTPUpload& upload = httpServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
        logMessage("OTA upload: " + upload.filename, "info");
        if (!OTA_BEGIN(upload.contentLength))
            logMessage("OTA begin failed: " + String(OTA_ERROR_STRING()), "error");
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
            logMessage("OTA write error", "error");
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true))
            logMessage("OTA upload complete: " + String(upload.totalSize) + " bytes", "info");
        else
            logMessage("OTA end failed: " + String(OTA_ERROR_STRING()), "error");
    }
}

static void handleOtaResponse() {
    if (Update.hasError())
        sendJson(500, String("{\"ok\":false,\"error\":\"") + OTA_ERROR_STRING() + "\"}");
    else {
        sendJson(200, "{\"ok\":true,\"msg\":\"Rebooting...\"}");
        logMessage("OTA done — rebooting", "info");
        vTaskDelay(pdMS_TO_TICKS(500));
        DEVICE_RESTART();
    }
}

// ─── GET /api/fs ──────────────────────────────────────────────────────────────

static void handleGetFs() {
    JsonDocument doc;
    doc["mounted"] = lfsReady;
    JsonArray files = doc["files"].to<JsonArray>();
    if (lfsReady) {
        File root = LittleFS.open("/", "r");
        File entry = root.openNextFile();
        while (entry) {
            JsonObject f = files.add<JsonObject>();
            f["name"] = String("/") + entry.name();
            f["size"] = entry.size();
            entry = root.openNextFile();
        }
        root.close();
    }
    sendJsonDoc(200, doc);
}

// ─── Captive portal ───────────────────────────────────────────────────────────

static void handleCaptivePortal() {
    httpServer.sendHeader("Location", "http://192.168.4.1/", true);
    httpServer.send(302, "text/plain", "");
}

// ─── Static file fallback ─────────────────────────────────────────────────────

static void handleStaticFile(const String& path) {
    String fullPath = path;
    if (fullPath.endsWith("/")) fullPath += "index.html";

    if (fullPath == "/index.html") {
        httpServer.send_P(200, "text/html", INDEX_HTML);
        return;
    }

    if (!lfsReady || !LittleFS.exists(fullPath)) {
        httpServer.send(404, "text/plain", "Not found: " + fullPath);
        return;
    }

    String ct = "text/plain";
    if (fullPath.endsWith(".html")) ct = "text/html";
    else if (fullPath.endsWith(".css")) ct = "text/css";
    else if (fullPath.endsWith(".js"))  ct = "application/javascript";
    else if (fullPath.endsWith(".svg")) ct = "image/svg+xml";

    File file = LittleFS.open(fullPath, "r");
    httpServer.streamFile(file, ct);
    file.close();
}

// ─── Web task ─────────────────────────────────────────────────────────────────

static void webServerSetup() {
    // Captive portal probes
    const char* captiveUris[] = {
        "/generate_204", "/gen_204", "/hotspot-detect.html",
        "/library/test/success.html", "/ncsi.txt",
        "/connecttest.txt", "/success.txt", "/redirect"
    };
    for (auto uri : captiveUris)
        httpServer.on(uri, HTTP_GET, handleCaptivePortal);

    // WiFi
    httpServer.on("/api/wifi",       HTTP_GET,  handleGetWifi);
    httpServer.on("/api/wifi",       HTTP_POST, handlePostWifi);
    httpServer.on("/api/wifi/scan",  HTTP_GET,  handleGetWifiScan);

    // MQTT config
    httpServer.on("/api/mqtt/config", HTTP_GET,  handleGetMqttConfig);
    httpServer.on("/api/mqtt/config", HTTP_POST, handlePostMqttConfig);

    // HW config
    httpServer.on("/api/hw/config",  HTTP_GET,  handleGetHwConfig);
    httpServer.on("/api/hw/config",  HTTP_POST, handlePostHwConfig);

    // Sensor setup
    httpServer.on("/api/sensors/setup", HTTP_GET,  handleGetSensorSetup);
    httpServer.on("/api/sensors/setup", HTTP_POST, handlePostSensorSetup);
    httpServer.on("/api/config/export", HTTP_GET,  handleGetConfigExport);

    // Other
    httpServer.on("/api/state",      HTTP_GET,  handleGetState);
    httpServer.on("/api/cmd",        HTTP_POST, handlePostCmd);
    httpServer.on("/api/fs",         HTTP_GET,  handleGetFs);
    httpServer.on("/api/ota",        HTTP_POST, handleOtaResponse, handleOtaUpload);

    httpServer.onNotFound([]() {
        String path = httpServer.uri();
        if (STATE_GET(apMode) && path != "/" && path != "/index.html") {
            httpServer.sendHeader("Location", "http://192.168.4.1/", true);
            httpServer.send(302, "text/plain", "");
        } else {
            handleStaticFile(path);
        }
    });

    httpServer.begin();
    logMessage("HTTP server started on port 80", "info");
    logMessage("WebSocket log on port 81", "info");
}

void webTask(void* pvParameters) {
    webServerSetup();
    for (;;) {
        httpServer.handleClient();
        if (rebootPending) {
            logMessage("Rebooting...", "info");
            vTaskDelay(pdMS_TO_TICKS(500));
            DEVICE_RESTART();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

#ifdef ESP8266
void webInit() {
    webServerSetup();
}

void webProcess() {
    httpServer.handleClient();
    if (rebootPending) {
        logMessage("Rebooting...", "info");
        delay(500);
        DEVICE_RESTART();
    }
}
#endif
