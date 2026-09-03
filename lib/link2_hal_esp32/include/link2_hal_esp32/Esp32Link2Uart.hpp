#pragma once

#include <cstdint>

#include "hal/IByteSink.hpp"
#include "link2/Link2Frame.hpp" // kFrameLen, for the non-blocking static_assert

namespace link2_hal_esp32 {

// The whole justification for IByteSink having no backpressure API, pinned so a
// protocol bump cannot silently invalidate it: one frame must fit the ESP32
// UART1 TX FIFO in a single go. v1 was 12 bytes, v2 is 17 -- the comment that
// used to say "12" here was stale by ~40 % (finding timing-5).
inline constexpr size_t kUart1TxFifoBytes = 128;
static_assert(link2::kFrameLen <= kUart1TxFifoBytes,
              "link2 frame must fit the UART1 TX FIFO in one go, or the "
              "fire-and-forget IByteSink contract can block the control loop");

// UART1 TX-only sink to the sound/light board. 115200 8N1 (CLAUDE.md
// section 1). The RX side (GPIO26 ack channel) is deliberately NOT opened:
// it is unused, and an open undriven input would just collect noise bytes
// and fire RX interrupts for nothing. GPIO26 stays reserved in PinMap.
//
// Never blocks in practice: one 17-byte v2 frame (link2::kFrameLen) every 50 ms
// against the 128-byte TX FIFO above, which drains in ~1.5 ms at 115200 8N1
// (17 bytes = 10 bits each / 115200 = ~1.5 ms). The static_assert above keeps
// that argument true across protocol bumps instead of trusting this sentence.
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
