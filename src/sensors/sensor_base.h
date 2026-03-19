#pragma once

#include "../queues.h"
#include <stdint.h>

// ─── Abstract sensor interface ───────────────────────────────────────────────
// Each concrete sensor class implements this interface.
// begin() is called once at startup with the current HwConfig values.
// read() fills in a SensorReading and returns true on success.

class SensorBase {
public:
    virtual ~SensorBase() {}

    // Initialize hardware. Returns true if sensor is present and ready.
    virtual bool begin(int i2c_sda, int i2c_scl,
                       int uart_rx,  int uart_tx,
                       int onewire_pin) = 0;

    // Returns true if the sensor is initialized and available.
    virtual bool isReady() = 0;

    // Read sensor data. Fills r.data (JSON snippet) and r.msgType.
    // r.deviceId, r.time, r.count, r.mV, r.immTX should be set by caller.
    virtual bool read(SensorReading& r) = 0;

    // Periodic tick — called every loop iteration; default is no-op.
    // Returns true to request an immediate combined sensor read (e.g. PMS7003 ready).
    virtual bool tick() { return false; }

    // Short type identifier string: "bme280", "sht30", "ds18b20", ...
    virtual const char* type() const = 0;

    // MQTT msgType numeric identifier
    virtual uint8_t msgType() const = 0;
};
