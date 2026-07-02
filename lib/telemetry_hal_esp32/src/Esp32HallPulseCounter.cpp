#include "telemetry_hal_esp32/Esp32HallPulseCounter.hpp"

#include <Arduino.h>

namespace telemetry_hal_esp32 {

Esp32HallPulseCounter::Esp32HallPulseCounter(uint8_t pin) : pin_(pin) {}

void Esp32HallPulseCounter::begin() {
    pinMode(pin_, INPUT);
    attachInterruptArg(digitalPinToInterrupt(pin_), &Esp32HallPulseCounter::isrTrampoline, this,
                       RISING);
}

// IRAM_ATTR: the ISR must be executable while the flash cache is disabled
// (e.g. during an NVS write) or it can crash the chip.
void IRAM_ATTR Esp32HallPulseCounter::isrTrampoline(void* arg) {
    static_cast<Esp32HallPulseCounter*>(arg)->onEdge();
}

void IRAM_ATTR Esp32HallPulseCounter::onEdge() {
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

hal::WheelPulseSnapshot Esp32HallPulseCounter::read() const {
    return {count_.load(std::memory_order_relaxed),
            lastPeriodUs_.load(std::memory_order_relaxed)};
}

} // namespace telemetry_hal_esp32
