#include "outputs/EscOutput.hpp"

namespace outputs {

EscOutput::EscOutput(hal::IPwmOutput& pwm, hal::IClock& clock, EscConfig config)
    : pwm_(pwm), clock_(clock), config_(config) {}

bool EscOutput::isArmed() const {
    return armHoldStarted_ && (clock_.nowMs() - armHoldStartMs_) >= config_.bootArmHoldMs;
}

void EscOutput::setThrottle(int16_t normalizedThrottle) {
    if (!armHoldStarted_) {
        armHoldStarted_ = true;
        armHoldStartMs_ = clock_.nowMs();
    }

    if (!isArmed()) {
        pwm_.setPulseMicroseconds(config_.neutralMicros);
        return;
    }

    int32_t clamped = normalizedThrottle;
    if (clamped > 1000) {
        clamped = 1000;
    } else if (clamped < -1000) {
        clamped = -1000;
    }

    const int32_t neutral = config_.neutralMicros;
    int32_t micros;
    if (clamped >= 0) {
        micros = neutral + (clamped * (static_cast<int32_t>(config_.maxMicros) - neutral)) / 1000;
    } else {
        micros = neutral + (clamped * (neutral - static_cast<int32_t>(config_.minMicros))) / 1000;
    }

    if (micros < config_.minMicros) {
        micros = config_.minMicros;
    } else if (micros > config_.maxMicros) {
        micros = config_.maxMicros;
    }

    pwm_.setPulseMicroseconds(static_cast<uint16_t>(micros));
}

} // namespace outputs
