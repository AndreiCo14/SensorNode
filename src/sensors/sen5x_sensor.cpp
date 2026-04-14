#include "sen5x_sensor.h"
#include "../logger.h"
#include <Wire.h>
#include <math.h>   // isnan()

bool Sen5xSensor::begin(int, int, int, int, int) {
    _ready  = false;
    _hasNox = false;

    _sen5x.begin(Wire);

    uint16_t err = _sen5x.deviceReset();
    if (err) {
        logMessage("SEN5x: reset error " + String(err), "warn");
        return false;
    }
    delay(1200);  // SEN5x needs ~1 s after reset before accepting commands

    err = _sen5x.startMeasurement();
    if (err) {
        logMessage("SEN5x: startMeasurement error " + String(err), "warn");
        return false;
    }

    // Read product name to distinguish SEN54 from SEN55
    unsigned char name[32] = {};
    _sen5x.getProductName(name, sizeof(name));
    _hasNox = (strstr((char*)name, "SEN55") != nullptr ||
               strstr((char*)name, "55")    != nullptr);

    _ready = true;
    logMessage(String("SEN5x OK (") + (char*)name + ") @ 0x69" +
               (_hasNox ? " NOx=yes" : " NOx=no"), "info");
    return true;
}

bool Sen5xSensor::read(SensorReading& r) {
    if (!_ready) return false;

    bool dataReady = false;
    uint16_t err = _sen5x.readDataReady(dataReady);
    if (err || !dataReady) return false;

    float pm1p0, pm2p5, pm4p0, pm10p0, hum, temp, voc, nox;
    err = _sen5x.readMeasuredValues(pm1p0, pm2p5, pm4p0, pm10p0,
                                    hum, temp, voc, nox);
    if (err) {
        logMessage("SEN5x read error " + String(err), "warn");
        return false;
    }

    _lastTemp = isnan(temp) ? _lastTemp : temp;
    _lastHum  = isnan(hum)  ? _lastHum  : hum;

    r.msgType = msgType();

    if (_hasNox && !isnan(nox)) {
        snprintf(r.data, sizeof(r.data),
                 "{\"PM1\":%.1f,\"PM25\":%.1f,\"PM4\":%.1f,\"PM10\":%.1f"
                 ",\"Temp\":%.2f,\"Hum\":%.1f,\"VOC\":%.0f,\"NOx\":%.0f}",
                 pm1p0, pm2p5, pm4p0, pm10p0, temp, hum, voc, nox);
    } else {
        snprintf(r.data, sizeof(r.data),
                 "{\"PM1\":%.1f,\"PM25\":%.1f,\"PM4\":%.1f,\"PM10\":%.1f"
                 ",\"Temp\":%.2f,\"Hum\":%.1f,\"VOC\":%.0f}",
                 pm1p0, pm2p5, pm4p0, pm10p0, temp, hum, voc);
    }
    return true;
}
