#pragma once

#include <cstddef>
#include <cstdint>

namespace crsf_hal_esp32 {

// Thin wrapper around ESP32 UART2 (Serial2), configured for CRSF: 420000
// baud, 8N1, not inverted (CLAUDE.md section 1). Isolates Arduino/
// HardwareSerial specifics from the pure crsf:: parser/assembler logic --
// this class is referenced only from src/main.cpp, never from tests.
class Esp32CrsfUart {
public:
    Esp32CrsfUart(uint8_t rxPin, uint8_t txPin);

    void begin();

    // Number of bytes available to read without blocking.
    int available() const;

    // Reads one byte. Caller must check available() > 0 first.
    uint8_t read();

    // Writes bytes out UART2 TX (GPIO17 -> RP1 RX pad) -- e.g. CRSF battery
    // telemetry up to the RX. TX is a separate pad from the RX (GPIO16), so
    // this never collides with inbound RC-frame parsing.
    void write(const uint8_t* data, size_t len);

private:
    uint8_t rxPin_;
    uint8_t txPin_;
};

} // namespace crsf_hal_esp32
