#include "telemetry_hal_esp32/Esp32HallPulseCounter.hpp"

#include <Arduino.h>

namespace telemetry_hal_esp32 {

Esp32HallPulseCounter::Esp32HallPulseCounter(uint8_t pin,
                                             telemetry::PulseRateGuardConfig guardConfig)
    : pin_(pin), guard_(guardConfig) {}

void Esp32HallPulseCounter::attach() {
    attachInterruptArg(digitalPinToInterrupt(pin_), &Esp32HallPulseCounter::isrTrampoline, this,
                       RISING);
    attached_ = true;
}

void Esp32HallPulseCounter::begin() {
    pinMode(pin_, INPUT);
    attach();
}

// IRAM_ATTR: the ISR must be executable while the flash cache is disabled
// (e.g. during an NVS write) or it can crash the chip.
void IRAM_ATTR Esp32HallPulseCounter::isrTrampoline(void* arg) {
    static_cast<Esp32HallPulseCounter*>(arg)->onEdge();
}

void IRAM_ATTR Esp32HallPulseCounter::onEdge() {
    // FIRST, before the debounce early-return: this counter measures how often
    // the interrupt FIRES, which is the quantity that starves the control tick.
    // count_ below measures how often we accept an edge, which the lockout
    // already bounds -- that is exactly why it cannot see an edge storm.
    entries_.fetch_add(1, std::memory_order_relaxed);

    const uint32_t nowUs = micros();
    if (edgeSeen_ && (nowUs - lastEdgeUs_) < kLockoutUs) {
        return; // EMI/bounce, not a real magnet pass
    }
    if (edgeSeen_) {
        lastPeriodUs_.store(nowUs - lastEdgeUs_, std::memory_order_relaxed);
    }
    lastEdgeUs_ = nowUs;
    edgeSeen_ = true;
    count_.fetch_add(1, std::memory_order_relaxed);
}

void Esp32HallPulseCounter::poll(uint32_t nowMs) {
    switch (guard_.update(nowMs, entries_.load(std::memory_order_relaxed))) {
        case telemetry::PulseRateGuard::Action::Detach:
            if (attached_) {
                detachInterrupt(digitalPinToInterrupt(pin_));
                attached_ = false;
            }
            // The last measured period would otherwise linger as a plausible
            // speed while nothing is being measured. read() reports
            // sensorFault anyway; zeroing it means even a consumer that
            // ignored the flag cannot see motion that is not there.
            lastPeriodUs_.store(0, std::memory_order_relaxed);
            break;

        case telemetry::PulseRateGuard::Action::Reattach:
            if (!attached_) {
                // Safe to touch ISR-only state here: the interrupt is detached,
                // so nothing can be running concurrently. Clearing the edge
                // history stops the first post-rearm edge from producing a
                // period that spans the whole outage.
                edgeSeen_ = false;
                lastEdgeUs_ = 0;
                attach();
            }
            break;

        case telemetry::PulseRateGuard::Action::None:
            break;
    }
}

hal::WheelPulseSnapshot Esp32HallPulseCounter::read() const {
    return {count_.load(std::memory_order_relaxed), lastPeriodUs_.load(std::memory_order_relaxed),
            guard_.faulted()};
}

} // namespace telemetry_hal_esp32
