#pragma once

#include <cstdint>

#include "hal/IPwmOutput.hpp"

namespace outputs_hal_esp32 {

// Concrete hal::IPwmOutput backed by an ESP32 LEDC channel at 50 Hz --
// standard analog servo/ESC PWM rate. Isolates ledcSetup/ledcAttachPin/
// ledcWrite from the pure outputs:: scaling logic.
class Esp32LedcPwm : public hal::IPwmOutput {
public:
    // `channel` is an LEDC channel number (0-15); each PWM output needs a
    // distinct channel.
    Esp32LedcPwm(uint8_t pin, uint8_t channel);

    // Attaches the pin and immediately commands `initialPulseMicros` (pass the
    // channel's known-safe position: servo center, ESC neutral, DRS closed).
    // Without this, LEDC idles at duty 0 -- no pulses at all -- until the
    // first setPulseMicroseconds() call, making safe output depend on caller
    // ordering (review finding A4, docs/ROADMAP.md).
    void begin(uint16_t initialPulseMicros);

    void setPulseMicroseconds(uint16_t microseconds) override;

private:
    static constexpr uint32_t kFrequencyHz = 50;
    static constexpr uint8_t kResolutionBits = 16;
    static constexpr uint32_t kPeriodMicros = 1000000UL / kFrequencyHz; // 20000us
    static constexpr uint32_t kMaxDuty = (1UL << kResolutionBits) - 1;

    uint8_t pin_;
    uint8_t channel_;
};

} // namespace outputs_hal_esp32
