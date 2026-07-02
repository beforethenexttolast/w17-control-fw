#include "telemetry/WheelSpeed.hpp"

namespace telemetry {

WheelSpeed::WheelSpeed(hal::IWheelPulseSensor& sensor, WheelSpeedConfig config)
    : sensor_(sensor), config_(config) {}

void WheelSpeed::update(uint32_t nowMs) {
    const hal::WheelPulseSnapshot snapshot = sensor_.read();

    if (!seeded_) {
        // First update only seeds: the counter may already be nonzero (wheel
        // spun before construction) and must not produce a bogus spike.
        seeded_ = true;
        lastCount_ = snapshot.count;
        lastPulseSeenMs_ = nowMs;
        return;
    }

    if (snapshot.count != lastCount_) {
        lastCount_ = snapshot.count;
        lastPulseSeenMs_ = nowMs;

        // lastPeriodMicros == 0 until two edges have ever been seen -- the
        // first-ever edge has no period, keep the previous value.
        if (snapshot.lastPeriodMicros != 0) {
            const uint32_t revPeriodMicros =
                snapshot.lastPeriodMicros * config_.magnetsPerRev;
            uint32_t rpm = 60000000u / revPeriodMicros;
            if (rpm > config_.maxPlausibleRpm) {
                rpm = config_.maxPlausibleRpm; // EMI/glitch clamp
            }
            measuredRpm_ = static_cast<uint16_t>(rpm);
        }
        reportedRpm_ = measuredRpm_;
        return;
    }

    // No new pulse. Cap the report by the fastest the wheel could still be
    // turning given the silence so far: one revolution per elapsedMs is
    // 60000/elapsed rpm (per magnet). Then truncate the tail to hard zero.
    const uint32_t elapsedMs = nowMs - lastPulseSeenMs_;
    if (elapsedMs >= config_.zeroSpeedTimeoutMs) {
        measuredRpm_ = 0;
        reportedRpm_ = 0;
        return;
    }
    if (elapsedMs > 0) {
        const uint32_t impliedCeilingRpm =
            60000u / (elapsedMs * config_.magnetsPerRev);
        if (impliedCeilingRpm < reportedRpm_) {
            reportedRpm_ = static_cast<uint16_t>(impliedCeilingRpm);
        }
    }
}

} // namespace telemetry
