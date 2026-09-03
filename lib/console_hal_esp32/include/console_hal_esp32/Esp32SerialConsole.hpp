#pragma once

#include <cstddef>

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
    // TX ring installed in begin() so console writes cannot block the loopTask
    // (finding timing-2). Sized from console::kMaxOutput (512, lib/console/
    // include/console/Console.hpp), which is the hard cap on a single command's
    // response text -- NOT from a guess: the longest real response, `help`, is
    // 403 bytes (lib/console/src/Console.cpp, the "commands: ..." literal) plus
    // the "\r\n" ConsoleRunner writes after it. src/main.cpp static_asserts
    // this against console::kMaxOutput in the tuning build, where both headers
    // are in scope; keep that assertion if this constant ever moves.
    // IDF also requires a ring LARGER than SOC_UART_FIFO_LEN (128), which 512
    // satisfies with the FIFO itself as extra absorbency behind it.
    static constexpr size_t kTxBufferBytes = 512;

    void begin(unsigned long baud = 115200);
    int read() override;
    void write(const char* text) override;
};

} // namespace console_hal_esp32
