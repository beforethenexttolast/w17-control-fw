#include "telemetry/PulseRateGuard.hpp"

#include <cstdint>

namespace telemetry {

PulseRateGuard::PulseRateGuard(PulseRateGuardConfig config) : config_(config) {}

PulseRateGuard::Action PulseRateGuard::update(uint32_t nowMs, uint32_t entryCount) {
    if (!seeded_) {
        // First call only establishes the baseline: the counter may already be
        // nonzero and must not read as one enormous window.
        seeded_ = true;
        windowStartMs_ = nowMs;
        windowStartCount_ = entryCount;
        return Action::None;
    }

    if (faulted_) {
        if (nowMs - faultSinceMs_ < config_.quietMs) {
            return Action::None;
        }
        // Cool-down elapsed: re-arm and start a fresh window. If the storm is
        // still there the next window trips again, which bounds the ISR's duty
        // cycle instead of latching the sensor off for the rest of the drive.
        faulted_ = false;
        windowStartMs_ = nowMs;
        windowStartCount_ = entryCount;
        return Action::Reattach;
    }

    const uint32_t elapsedMs = nowMs - windowStartMs_;
    if (elapsedMs < config_.windowMs) {
        return Action::None;
    }

    const uint32_t entries = entryCount - windowStartCount_;
    windowStartMs_ = nowMs;
    windowStartCount_ = entryCount;

    // Scale the allowance by the ACTUAL elapsed time so a late tick cannot
    // manufacture a fault out of a perfectly normal rate.
    const uint64_t allowed = static_cast<uint64_t>(config_.maxEntriesPerWindow) *
                             static_cast<uint64_t>(elapsedMs) /
                             static_cast<uint64_t>(config_.windowMs);

    // A window that ran far longer than nominal means the caller stalled -- a
    // long flash write, a debugger -- OR that the very storm this guard exists
    // for is already starving the 50 Hz tick. Refusing to judge that whole
    // class (as this did until the 2026-09-03 review, finding 5) left the guard
    // blind in precisely the regime it was written for, so the rule is now
    // split: near the allowance the sample really does carry no information and
    // is re-seeded without a verdict; kStalledStormFactor x past the
    // elapsed-scaled allowance there is no ambiguity left to protect and it
    // trips. With the default config a 2 s stall allows 3600 entries and needs
    // 36000 to trip -- against the ~166 a real wheel produces in 2 s, so a
    // late tick still cannot manufacture a fault out of real motion.
    if (elapsedMs > kStalledWindowFactor * static_cast<uint32_t>(config_.windowMs) &&
        static_cast<uint64_t>(entries) < kStalledStormFactor * allowed) {
        return Action::None;
    }

    lastWindowEntries_ = entries;

    if (static_cast<uint64_t>(entries) > allowed) {
        faulted_ = true;
        faultSinceMs_ = nowMs;
        if (faultCount_ != UINT32_MAX) {
            faultCount_++; // saturate: a diagnostic must not wrap
        }
        return Action::Detach;
    }
    return Action::None;
}

} // namespace telemetry
