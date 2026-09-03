#include "telemetry/WheelSpeed.hpp"

#include <cstdint>

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
            const uint32_t rpm = 60000000u / revPeriodMicros;
            if (rpm > config_.maxPlausibleRpm) {
                // REJECT, never clamp (finding correctness-3, ruling OD-11
                // speedo (b)). Clamping reported maxPlausibleRpm -- i.e. the
                // fastest speed the car can go -- from a single EMI
                // double-count: the giftee's speedo pegged on a parked car,
                // the GPS-groundspeed frame with it, and ErsSystem harvesting
                // "coast" energy at rest (it gates only on rpm == 0). 0 is the
                // harvest-safe direction and the honest one: a period this
                // short is not a wheel.
                measuredRpm_ = 0;
                if (rejectedPulses_ != UINT32_MAX) {
                    rejectedPulses_++; // saturate: a diagnostic must not wrap
                }
            } else {
                measuredRpm_ = static_cast<uint16_t>(rpm);
            }
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
