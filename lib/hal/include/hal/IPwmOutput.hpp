#pragma once

#include <cstdint>

namespace hal {

// Abstract single-channel 50 Hz servo/ESC PWM output. The esp32 implementation
// (lib/outputs_hal_esp32) maps this onto LEDC duty-cycle math; the native test
// implementation (test/mocks/MockPwmOutput.hpp) just records the last call.
class IPwmOutput {
public:
    virtual ~IPwmOutput() = default;

    // Commands the channel to output a pulse of `microseconds` width.
    virtual void setPulseMicroseconds(uint16_t microseconds) = 0;
};

} // namespace hal
