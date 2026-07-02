#include "link2_hal_esp32/Esp32Link2Uart.hpp"

#include <Arduino.h>

namespace link2_hal_esp32 {

Esp32Link2Uart::Esp32Link2Uart(uint8_t txPin) : txPin_(txPin) {}

void Esp32Link2Uart::begin() {
    Serial1.begin(115200, SERIAL_8N1, /*rxPin=*/-1, txPin_);
}

void Esp32Link2Uart::write(const uint8_t* data, size_t len) {
    Serial1.write(data, len);
}

} // namespace link2_hal_esp32
