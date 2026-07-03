#include "crsf_hal_esp32/Esp32CrsfUart.hpp"

#include <Arduino.h>

#include "crsf/CrsfFrame.hpp"

namespace crsf_hal_esp32 {

Esp32CrsfUart::Esp32CrsfUart(uint8_t rxPin, uint8_t txPin) : rxPin_(rxPin), txPin_(txPin) {}

void Esp32CrsfUart::begin() {
    Serial2.begin(crsf::kCrsfBaud, SERIAL_8N1, rxPin_, txPin_);
}

int Esp32CrsfUart::available() const {
    return Serial2.available();
}

uint8_t Esp32CrsfUart::read() {
    return static_cast<uint8_t>(Serial2.read());
}

void Esp32CrsfUart::write(const uint8_t* data, size_t len) {
    Serial2.write(data, len);
}

} // namespace crsf_hal_esp32
