#pragma once
// ─── ESP32-C3 default pin assignments ────────────────────────────────────────
// These are compile-time fallbacks. Runtime values are loaded from /hwconfig.json.

#define DEFAULT_I2C_SDA     8
#define DEFAULT_I2C_SCL     9
#define DEFAULT_ONEWIRE     2
#define DEFAULT_UART_RX     20
#define DEFAULT_UART_TX     21
#define DEFAULT_LED_PIN     -1  // Set to GPIO pin if WS2812B is connected
