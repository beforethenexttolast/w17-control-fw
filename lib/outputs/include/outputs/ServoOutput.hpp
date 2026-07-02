#pragma once

#include <cstdint>

#include "hal/IPwmOutput.hpp"

namespace outputs {

struct ServoConfig {
    uint16_t minMicros = 500;     // physical endpoint, configurable per servo
    uint16_t maxMicros = 2500;    // physical endpoint, configurable per servo
    uint16_t centerMicros = 1500; // center, before trim. CLAUDE.md section 1: steering center 1500us
    int16_t trimMicros = 0;       // signed trim offset added to center, operator-tunable on the bench
};

// Linear position-to-microseconds scaling for a single servo channel (e.g.
// steering). Pure logic over an injected hal::IPwmOutput so it is testable
// with a mock.
class ServoOutput {
public:
    ServoOutput(hal::IPwmOutput& pwm, ServoConfig config = ServoConfig{});

    // normalizedPosition in [-1000, +1000]: -1000 = full one direction,
    // 0 = center (+ trim), +1000 = full other direction. Out-of-range input
    // is clamped defensively before scaling.
    void setPosition(int16_t normalizedPosition);

private:
    hal::IPwmOutput& pwm_;
    ServoConfig config_;
};

} // namespace outputs
