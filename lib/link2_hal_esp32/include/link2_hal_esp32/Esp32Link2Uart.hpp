#pragma once

#include <cstdint>

#include "hal/IByteSink.hpp"

namespace link2_hal_esp32 {

// UART1 TX-only sink to the sound/light board. 115200 8N1 (CLAUDE.md
// section 1). The RX side (GPIO26 ack channel) is deliberately NOT opened:
// it is unused, and an open undriven input would just collect noise bytes
// and fire RX interrupts for nothing. GPIO26 stays reserved in PinMap.
//
// Never blocks in practice: one 12-byte frame every 50ms against a 128-byte
// TX FIFO that drains in ~1ms.
class Esp32Link2Uart : public hal::IByteSink {
public:
    explicit Esp32Link2Uart(uint8_t txPin);

    // Remap is mandatory: ESP32 UART1's default pins are the flash pins.
    void begin();

    void write(const uint8_t* data, size_t len) override;

private:
    uint8_t txPin_;
};

} // namespace link2_hal_esp32
