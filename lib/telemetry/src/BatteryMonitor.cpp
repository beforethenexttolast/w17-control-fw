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

    // --- Sensor plausibility first (OD-10(a), finding fault-injection-3). A
    // reading outside the band is a broken divider, not a battery state, so it
    // must not enter the EMA (which would drag the average toward a lie for
    // seconds after recovery) and must not be published. Reporting NOTHING is
    // the honest output; every consumer already treats 0 mV as "no reading".
    if (batteryMvSample < config_.implausibleBelowMv ||
        batteryMvSample > config_.implausibleAboveMv) {
        if (!implausible_) {
            implausible_ = true;
            implausibleSinceMs_ = nowMs;
        }
        // PUBLICATION stops immediately: batteryMv() -> 0, so main.cpp omits
        // the CRSF battery frame and link2 carries 0.
        seeded_ = false;
        emaAccumulator_ = 0;

        // The WARNING is time-qualified SYMMETRICALLY (review 2026-09-03,
        // finding 6, as ruled). It takes warnDelayMs of sustained low to latch,
        // so it must take warnDelayMs of sustained implausibility to unlatch --
        // and equally to forget a qualification already in progress. Tearing
        // either down on one raw sample was the bug: an intermittent sense lead
        // on a genuinely flat pack (the fault this band exists for) alternates
        // implausible and plausible-low samples, and under the old rule every
        // implausible one reset the timer, so the warning could neither latch
        // nor stay latched -- board #2 stayed quiet on a flat pack. A dropout
        // shorter than warnDelayMs changes nothing we believe about the pack;
        // a longer one means we genuinely no longer know, and then we say so by
        // dropping the claim.
        if (nowMs - implausibleSinceMs_ >= config_.warnDelayMs) {
            warning_ = false;
            belowSince_ = false;
        }
        return;
    }
    implausible_ = false;

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
