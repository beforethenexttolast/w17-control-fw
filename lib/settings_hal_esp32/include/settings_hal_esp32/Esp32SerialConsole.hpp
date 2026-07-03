#pragma once

#include "hal/ICharIO.hpp"

namespace settings_hal_esp32 {

// UART0 (USB serial) char IO for the bench console. The real firmware
// otherwise opens no UART0 -- this is only constructed under the
// W17_TUNING_CONSOLE build flag.
class Esp32SerialConsole : public hal::ICharIO {
public:
    void begin(unsigned long baud = 115200);
    int read() override;
    void write(const char* text) override;
};

} // namespace settings_hal_esp32
