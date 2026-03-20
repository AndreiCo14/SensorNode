#pragma once
// ─── ESP8266 default pin assignments (Wemos D1 Mini / NodeMCU) ───────────────
// These are compile-time fallbacks. Runtime values are loaded from /hwconfig.json.

#define DEFAULT_I2C_SDA     4   // D2 on D1 Mini
#define DEFAULT_I2C_SCL     5   // D1 on D1 Mini
#define DEFAULT_ONEWIRE     2   // D4 on D1 Mini (also boot LED — use with care)
#define DEFAULT_UART_RX     3   // RX pin (UART0)
#define DEFAULT_UART_TX     1   // TX pin (UART0)
#define DEFAULT_LED_PIN     -1  // No WS2812B by default
