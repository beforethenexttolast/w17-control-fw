#include "console_hal_esp32/Esp32SerialConsole.hpp"

#include <Arduino.h>

namespace console_hal_esp32 {

void Esp32SerialConsole::begin(unsigned long baud) {
    // MUST precede begin(): Arduino-ESP32 2.0.17 installs UART0 with a
    // ZERO-length TX ring, so Serial.print() below would block until the last
    // byte is on the wire -- ~9 ms for a 100-char `get` dump at 115200, on the
    // loopTask that owns the 50 Hz control tick (finding timing-2). With a ring
    // the write returns as soon as the bytes are copied. kTxBufferBytes is
    // console::kMaxOutput (512), the cap on one command's response text, so no
    // response can outrun the ring -- `help`, the longest one that actually
    // exists, is 403 bytes + CRLF. Bench env only: the delivery image never
    // links this translation unit.
    Serial.setTxBufferSize(kTxBufferBytes);
    Serial.begin(baud);
}

int Esp32SerialConsole::read() {
    return Serial.available() > 0 ? Serial.read() : -1; // non-blocking
}

void Esp32SerialConsole::write(const char* text) { Serial.print(text); }

} // namespace console_hal_esp32
