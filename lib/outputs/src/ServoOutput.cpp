#include "outputs/ServoOutput.hpp"

namespace outputs {

ServoOutput::ServoOutput(hal::IPwmOutput& pwm, ServoConfig config) : pwm_(pwm), config_(config) {}

void ServoOutput::setPosition(int16_t normalizedPosition) {
    int32_t clamped = normalizedPosition;
    if (clamped > 1000) {
        clamped = 1000;
    } else if (clamped < -1000) {
        clamped = -1000;
    }

    const int32_t center = static_cast<int32_t>(config_.centerMicros) + config_.trimMicros;
    int32_t micros;
    if (clamped >= 0) {
        micros = center + (clamped * (static_cast<int32_t>(config_.maxMicros) - center)) / 1000;
    } else {
        micros = center + (clamped * (center - static_cast<int32_t>(config_.minMicros))) / 1000;
    }

    // Defensive clamp to the physical endpoints regardless of trim.
    if (micros < config_.minMicros) {
        micros = config_.minMicros;
    } else if (micros > config_.maxMicros) {
        micros = config_.maxMicros;
    }

    pwm_.setPulseMicroseconds(static_cast<uint16_t>(micros));
}

} // namespace outputs
