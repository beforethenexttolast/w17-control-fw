#pragma once

#include "hal/ICharIO.hpp"

namespace console_hal_esp32 {

// UART0 (USB serial) char IO for the bench tuning console. This is the ONLY
// place the firmware opens UART0 for a console: it lives in its own library so
// it is compiled and linked exclusively when src/main.cpp includes it under the
// W17_TUNING_CONSOLE build flag. The delivery esp32dev build never references
// this header, so its translation unit -- and the Serial.begin() inside -- is
// not part of that binary at all.
class Esp32SerialConsole : public hal::ICharIO {
public:
    void begin(unsigned long baud = 115200);
    int read() override;
    void write(const char* text) override;
};

} // namespace console_hal_esp32
