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
    // nextReadMs: absolute millis() when the next scheduled read will occur,
    // so sensors that need warm-up (e.g. PMS7003) can start ahead of time.
    // Returns value is ignored — reads are triggered only by the interval timer.
    virtual void tick(uint32_t nextReadMs) { (void)nextReadMs; }

    // Short type identifier string: "bme280", "sht30", "ds18b20", ...
    virtual const char* type() const = 0;

    // MQTT msgType numeric identifier
    virtual uint8_t msgType() const = 0;

    // I2C address (0 = not an I2C sensor)
    virtual uint8_t i2cAddr() const { return 0; }

    // Set I2C address (no-op for non-I2C or fixed-address sensors)
    virtual void setAddr(uint8_t) {}

    // Last measured temperature (°C) — valid only if providesTH() is true
    virtual float lastTemp() const { return 25.0f; }

    // Last measured relative humidity (%RH) — valid only if providesTH() is true
    virtual float lastHum()  const { return 50.0f; }

    // Returns true if this sensor measures temperature and humidity
    virtual bool  providesTH() const { return false; }
};
