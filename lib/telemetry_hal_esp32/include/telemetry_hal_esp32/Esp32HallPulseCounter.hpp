#pragma once

#include <atomic>
#include <cstdint>

#include "hal/IWheelPulseSensor.hpp"

namespace telemetry_hal_esp32 {

// A3144 Hall pulse input: rising-edge ISR counts edges and timestamps the
// period between them (micros()), per the hal::IWheelPulseSensor contract.
//
// Concurrency: count/period are std::atomic<uint32_t> with relaxed ordering
// -- lock-free single-word accesses on Xtensa, no torn reads, and no
// volatile-data-race ambiguity. lastEdgeUs_/edgeSeen_ are ISR-only state.
//
// Debounce: edges closer than kLockoutUs are ignored. The A3144 has magnetic
// hysteresis, but the electrical edge is slow (10k pull-up into wiring
// capacitance) and GPIO35 has no Schmitt trigger, so ESC EMI riding a slow
// edge can double-count. Real pulses are >=18ms apart at the car's top speed
// -- a 2ms lockout is a 9x margin.
class Esp32HallPulseCounter : public hal::IWheelPulseSensor {
public:
    explicit Esp32HallPulseCounter(uint8_t pin);

    // Attaches the rising-edge ISR. Pin is input-only GPIO35 with an external
    // 10k pull-up (CLAUDE.md section 7) -- no internal pull configured.
    void begin();

    hal::WheelPulseSnapshot read() const override;

private:
    static void isrTrampoline(void* arg);
    void onEdge();

    static constexpr uint32_t kLockoutUs = 2000;

    uint8_t pin_;
    std::atomic<uint32_t> count_{0};
    std::atomic<uint32_t> lastPeriodUs_{0};
    uint32_t lastEdgeUs_ = 0; // ISR-only
    bool edgeSeen_ = false;   // ISR-only
};

} // namespace telemetry_hal_esp32
