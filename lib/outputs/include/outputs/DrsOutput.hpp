#pragma once

#include <cstdint>

#include "hal/IPwmOutput.hpp"

namespace outputs {

struct DrsConfig {
    uint16_t closedMicros = 1000; // wing closed position, servo-specific, tune on bench
    uint16_t openMicros = 2000;   // wing open position, servo-specific, tune on bench
};

// 2-position DRS wing-flap output. Closed is the failsafe-safe position.
class DrsOutput {
public:
    DrsOutput(hal::IPwmOutput& pwm, DrsConfig config = DrsConfig{});

    void setOpen(bool open);

private:
    hal::IPwmOutput& pwm_;
    DrsConfig config_;
};

} // namespace outputs
