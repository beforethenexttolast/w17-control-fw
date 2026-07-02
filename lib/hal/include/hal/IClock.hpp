#pragma once

#include <cstdint>

namespace hal {

// Abstract monotonic millisecond clock. Injected wherever logic needs "now"
// (failsafe timing, ESC boot-arm hold) so it can be driven by a fake clock in
// native tests instead of calling millis() directly.
class IClock {
public:
    virtual ~IClock() = default;

    virtual uint32_t nowMs() const = 0;
};

} // namespace hal
