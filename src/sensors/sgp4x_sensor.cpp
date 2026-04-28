#include "sgp4x_sensor.h"
#include "../logger.h"
#include <Wire.h>

bool Sgp4xSensor::begin(int, int, int, int, int) {
    _ready = false;
    _mode  = MODE_UNKNOWN;
    _sgp.begin(Wire);

    // Execute conditioning for 10 s — required by SGP41 after power-up.
    // We run one conditioning step here; the sensor will be fully conditioned
    // by the time the first scheduled read() is called (typically minutes later).
    uint16_t srawVoc = 0;
    uint16_t err = _sgp.executeConditioning(0x8000, 0x6666, srawVoc);
    if (err) {
        logMessageFmt("warn", "SGP4x: conditioning error %d", err);
        return false;
    }
    _ready = true;
    logMessageFmt("info", "SGP4x OK (0x59)");
    return true;
}

bool Sgp4xSensor::isReady() { return _ready; }

bool Sgp4xSensor::read(SensorReading& r) {
    if (!_ready) return false;

    uint16_t compRh = 0x8000;  // 50 %RH default
    uint16_t compT  = 0x6666;  // 25 °C default
    if (_thSrc) {
        compT  = toTempTicks(_thSrc->lastTemp());
        compRh = toRhTicks(_thSrc->lastHum());
    }

    uint16_t srawVoc = 0, srawNox = 0;

    if (_mode != MODE_SGP40) {
        // Try SGP41 full measurement (VOC + NOx)
        uint16_t err = _sgp.measureRawSignals(compRh, compT, srawVoc, srawNox);
        if (!err) {
            if (_mode == MODE_UNKNOWN) {
                _mode = MODE_SGP41;
                logMessageFmt("info", "SGP4x: SGP41 detected (VOC+NOx)");
            }
        } else {
            // measureRawSignals failed — assume SGP40, fall through
            _mode = MODE_SGP40;
            logMessageFmt("info", "SGP4x: SGP40 detected (VOC only)");
        }
    }

    if (_mode == MODE_SGP40) {
        uint16_t err = _sgp.executeConditioning(compRh, compT, srawVoc);
        if (err) {
            logMessageFmt("warn", "SGP4x read error %d", err);
            return false;
        }
    }

    int32_t vocIndex = _vocAlgo.process(srawVoc);
    r.msgType = msgType();

    if (_mode == MODE_SGP41) {
        int32_t noxIndex = _noxAlgo.process(srawNox);
        snprintf(r.data, sizeof(r.data),
                 "{\"VOC\":%ld,\"NOx\":%ld}", (long)vocIndex, (long)noxIndex);
    } else {
        snprintf(r.data, sizeof(r.data), "{\"VOC\":%ld}", (long)vocIndex);
    }
    return true;
}
