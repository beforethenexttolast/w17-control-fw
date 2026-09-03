#pragma once

#include <atomic>
#include <cstdint>

#include "hal/IWheelPulseSensor.hpp"
#include "telemetry/PulseRateGuard.hpp"

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
//
// Rate guard (finding timing-1, ruling OD-11 guard (c)): the lockout above is
// an early return INSIDE onEdge(), so it bounds COUNTING, not ISR ENTRY. A
// floating pin (GPIO34-39 have no internal pull-ups, so a lifted external 10k
// cannot be mitigated in software any other way) or ESC EMI can therefore enter
// the ISR at kHz while count_ still looks sane, stealing time from the 50 Hz
// control tick that enforces failsafe. entries_ counts EVERY entry, the pure
// telemetry::PulseRateGuard judges the rate from the control tick (poll(), never
// the ISR), and an implausible rate detaches the interrupt, raises sensorFault
// in the snapshot -- WheelSpeed then reports 0 -- and re-arms after a quiet
// window.
class Esp32HallPulseCounter : public hal::IWheelPulseSensor {
public:
    explicit Esp32HallPulseCounter(
        uint8_t pin, telemetry::PulseRateGuardConfig guardConfig = telemetry::PulseRateGuardConfig{});

    // Attaches the rising-edge ISR. Pin is input-only GPIO35 with an external
    // 10k pull-up (CLAUDE.md section 7) -- no internal pull configured.
    void begin();

    // Call from the CONTROL TICK (never from an ISR): evaluates the interrupt
    // rate and detaches/re-arms the ISR accordingly. Cheap when nothing is
    // wrong -- one atomic load and an unsigned compare until a window closes.
    void poll(uint32_t nowMs);

    hal::WheelPulseSnapshot read() const override;

    // Diagnostics for the bench (and the Phase-B margin measurement): total ISR
    // entries incl. debounce-rejected ones, and how often the guard has tripped.
    uint32_t isrEntries() const { return entries_.load(std::memory_order_relaxed); }
    uint32_t guardFaults() const { return guard_.faultCount(); }
    uint32_t lastWindowEntries() const { return guard_.lastWindowEntries(); }

private:
    static void isrTrampoline(void* arg);
    void onEdge();
    void attach();

    static constexpr uint32_t kLockoutUs = 2000;

    uint8_t pin_;
    telemetry::PulseRateGuard guard_;
    std::atomic<uint32_t> count_{0};
    std::atomic<uint32_t> lastPeriodUs_{0};
    std::atomic<uint32_t> entries_{0}; // EVERY ISR entry, lockout or not
    bool attached_ = false;   // poll()-only (main task)
    uint32_t lastEdgeUs_ = 0; // ISR-only
    bool edgeSeen_ = false;   // ISR-only
};

} // namespace telemetry_hal_esp32
