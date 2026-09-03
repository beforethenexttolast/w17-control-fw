#pragma once

#include <cstddef>
#include <cstdint>

namespace hal {

// Abstract outgoing byte stream (fire-and-forget). No backpressure API on
// purpose: a link2 v2 frame (17 bytes -- link2::kFrameLen; v1's 12 is what an
// earlier version of this comment described) is far smaller than the ESP32
// UART TX FIFO (128 bytes) and is sent every 50 ms while draining in ~1.5 ms,
// so a write can never block in this regime. This header stays dependency-free
// on purpose, so the number is not pinned here -- the static_assert that keeps
// it honest lives with the one implementation that relies on it,
// link2_hal_esp32/Esp32Link2Uart.hpp.
class IByteSink {
public:
    virtual ~IByteSink() = default;

    virtual void write(const uint8_t* data, size_t len) = 0;
};

} // namespace hal
