#include "failsafe/GimbalDecay.hpp"

namespace failsafe {

namespace {

// Full one-side deflection in ServoOutput's normalized units -- the "full" in
// GimbalDecayConfig::fullToCenterMs.
constexpr int32_t kFullScaleCounts = 1000;

// A single tick may consume at most this much elapsed time. This bounds the
// largest single-tick movement after a scheduler stall to
// kFullScaleCounts * 100 / fullToCenterMs counts (50 at the 2 s default):
// under sustained starvation the decay stretches in wall time instead of
// jumping, which is the gentle direction for a camera. Healthy ticks are
// 20 ms (kControlPeriodMs), so this clamp never engages in normal operation.
constexpr uint32_t kMaxTickDtMs = 100;

int16_t clampPosition(int16_t v) {
    if (v > kFullScaleCounts) return static_cast<int16_t>(kFullScaleCounts);
    if (v < -kFullScaleCounts) return static_cast<int16_t>(-kFullScaleCounts);
    return v;
}

} // namespace

GimbalDecay::GimbalDecay(GimbalDecayConfig config) : config_(config) {}

void GimbalDecay::setConfig(const GimbalDecayConfig& config) {
    config_ = config;
    carryCountMs_ = 0; // a remainder accumulated against the old rate is meaningless
}

int16_t GimbalDecay::update(uint32_t nowMs, bool failsafeEngaged, int16_t commanded) {
    commanded = clampPosition(commanded);

    // Unsigned subtraction is wrap-correct across a millis() rollover. The
    // first call ever has no previous tick: dt 0, state only.
    uint32_t dtMs = haveTick_ ? (nowMs - lastTickMs_) : 0;
    haveTick_ = true;
    lastTickMs_ = nowMs;
    if (dtMs > kMaxTickDtMs) {
        dtMs = kMaxTickDtMs;
    }

    if (failsafeEngaged) {
        if (!slewing_) {
            // Engage: decay starts from the last passthrough output -- the
            // last commanded look direction -- with a fresh sub-count carry.
            slewing_ = true;
            carryCountMs_ = 0;
        }
        slewToward(0, dtMs);
        return output_;
    }

    if (slewing_) {
        // Failsafe released: converge on the live command at the decay rate
        // -- never snap, even if the stick moved during the outage. The
        // target is re-read every tick (the stick is live again); release
        // completes the moment the output reaches the current command.
        slewToward(commanded, dtMs);
        if (output_ == commanded) {
            slewing_ = false;
            carryCountMs_ = 0;
        }
        return output_;
    }

    output_ = commanded; // transparent passthrough: normal aiming is unshaped
    return output_;
}

void GimbalDecay::slewToward(int16_t target, uint32_t dtMs) {
    // Belt-and-suspenders: every caller path validates the config (defaults
    // are static_asserted, the NVS loader and the console both gate on
    // valid()), but a division on the control board must still be
    // structurally total -- an impossible 0 behaves as the fastest legal
    // rate instead of faulting.
    const uint32_t divider = config_.fullToCenterMs > 0
                                 ? config_.fullToCenterMs
                                 : GimbalDecayConfig::kMinFullToCenterMs;

    // Per-tick movement allowance, exact over time: one count of travel
    // "costs" fullToCenterMs/1000 ms, so
    //     allowance = (1000 * dt + carry) / fullToCenterMs
    // with the division remainder carried to the next tick -- a full-scale
    // decay therefore completes in fullToCenterMs to within one tick, with no
    // truncation drift at any rate. Unused whole counts are deliberately
    // DISCARDED, not banked: sitting at the target must never accumulate
    // budget that would later be spent as a burst.
    // Ranges: carry < divider <= 20000 (fits uint16_t); numer <= 1000*100 +
    // 20000 = 120000; allowance <= 120000/100 = 1200 (fits comfortably).
    const uint32_t numer =
        static_cast<uint32_t>(kFullScaleCounts) * dtMs + carryCountMs_;
    const uint32_t allowance = numer / divider;
    carryCountMs_ = static_cast<uint16_t>(numer % divider);

    const int32_t distance = static_cast<int32_t>(target) - output_;
    const int32_t magnitude = distance < 0 ? -distance : distance;
    if (magnitude <= static_cast<int32_t>(allowance)) {
        output_ = target; // arrive exactly; the surplus allowance is dropped
    } else if (distance > 0) {
        output_ = static_cast<int16_t>(output_ + static_cast<int32_t>(allowance));
    } else {
        output_ = static_cast<int16_t>(output_ - static_cast<int32_t>(allowance));
    }
}

} // namespace failsafe
