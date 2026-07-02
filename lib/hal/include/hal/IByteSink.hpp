#pragma once

#include <cstddef>
#include <cstdint>

namespace hal {

// Abstract outgoing byte stream (fire-and-forget). No backpressure API on
// purpose: link2 frames (12 bytes) are far smaller than the ESP32 UART TX
// FIFO (128 bytes) and are sent every 50 ms while draining in ~1 ms, so a
// write can never block in this regime.
class IByteSink {
public:
    virtual ~IByteSink() = default;

    virtual void write(const uint8_t* data, size_t len) = 0;
};

} // namespace hal
