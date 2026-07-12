#include "console_hal_esp32/Esp32SerialConsole.hpp"

#include <Arduino.h>

namespace console_hal_esp32 {

void Esp32SerialConsole::begin(unsigned long baud) { Serial.begin(baud); }

int Esp32SerialConsole::read() {
    return Serial.available() > 0 ? Serial.read() : -1; // non-blocking
}

void Esp32SerialConsole::write(const char* text) { Serial.print(text); }

} // namespace console_hal_esp32
