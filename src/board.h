#pragma once
// Dispatches to the correct board pin definition header.

#if defined(BOARD_ESP8266)
#  include "boards/esp8266.h"
#elif defined(BOARD_ESP32C3)
#  include "boards/esp32c3.h"
#else
#  error "No board defined. Set BOARD_ESP8266 or BOARD_ESP32C3 in platformio.ini build_flags."
#endif
