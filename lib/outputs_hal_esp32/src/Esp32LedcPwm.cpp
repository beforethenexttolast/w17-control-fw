#include "outputs_hal_esp32/Esp32LedcPwm.hpp"

#include <Arduino.h>

namespace outputs_hal_esp32 {

Esp32LedcPwm::Esp32LedcPwm(uint8_t pin, uint8_t channel) : pin_(pin), channel_(channel) {}

void Esp32LedcPwm::begin(uint16_t initialPulseMicros) {
    ledcSetup(channel_, kFrequencyHz, kResolutionBits);
    ledcAttachPin(pin_, channel_);
    setPulseMicroseconds(initialPulseMicros);
}

void Esp32LedcPwm::setPulseMicroseconds(uint16_t microseconds) {
    const uint32_t duty = (static_cast<uint32_t>(microseconds) * kMaxDuty) / kPeriodMicros;
    ledcWrite(channel_, duty);
}

} // namespace outputs_hal_esp32
