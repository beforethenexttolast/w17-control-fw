#include "telemetry/BatteryMonitor.hpp"

namespace telemetry {

BatteryMonitor::BatteryMonitor(hal::IVoltageSensor& sensor, BatteryConfig config)
    : sensor_(sensor), config_(config) {}

uint16_t BatteryMonitor::convertPinToBatteryMv(uint16_t pinMv) const {
    // One combined division with rounding: chaining two truncating divides
    // (divider first, trim second) would lose up to ~4 mV.
    const uint32_t numerator = static_cast<uint32_t>(pinMv) *
                               static_cast<uint32_t>(config_.dividerNum) *
                               static_cast<uint32_t>(config_.calibrationPpt);
    const uint32_t denominator = static_cast<uint32_t>(config_.dividerDen) * 1000u;
    return static_cast<uint16_t>((numerator + denominator / 2) / denominator);
}

uint16_t BatteryMonitor::batteryMv() const {
    if (!seeded_) {
        return 0;
    }
    // Rounded readout of the scaled accumulator.
    return static_cast<uint16_t>((emaAccumulator_ + (1u << (config_.emaShift - 1))) >>
                                  config_.emaShift);
}

void BatteryMonitor::sample(uint32_t nowMs) {
    const uint16_t batteryMvSample = convertPinToBatteryMv(sensor_.readPinMillivolts());

    if (!seeded_) {
        // Seed from the first sample: starting the EMA at 0 would both
        // misreport the voltage and fire a spurious low-voltage warning for
        // the first second or two after every boot.
        emaAccumulator_ = static_cast<uint32_t>(batteryMvSample) << config_.emaShift;
        seeded_ = true;
    } else {
        // Scaled-accumulator EMA: acc += sample - acc/2^shift. Unlike the
        // naive (avg*(2^s-1)+sample)>>s form, this does not stall below a
        // rising input from truncation.
        emaAccumulator_ += batteryMvSample - (emaAccumulator_ >> config_.emaShift);
    }

    const uint16_t smoothed = batteryMv();

    if (warning_) {
        if (smoothed > config_.warnMv + config_.warnClearHysteresisMv) {
            warning_ = false;
            belowSince_ = false;
        }
        return;
    }

    if (smoothed < config_.warnMv) {
        if (!belowSince_) {
            belowSince_ = true;
            belowSinceMs_ = nowMs;
        } else if (nowMs - belowSinceMs_ >= config_.warnDelayMs) {
            warning_ = true; // sustained low: latch (monitoring only, CLAUDE.md 6.4)
        }
    } else {
        belowSince_ = false; // recovered before the delay elapsed: sag, not empty
    }
}

} // namespace telemetry
