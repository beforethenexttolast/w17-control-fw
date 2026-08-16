#pragma once

#include <cstdint>

namespace failsafe {

// Tunable for the gimbal link-loss decay (vision decision 11, 2026-08-16).
// Persisted in the Settings blob and editable as `gimbal.decay` on the
// tuning console; validated at the definition site like every config struct.
struct GimbalDecayConfig {
    // Time for a FULL deflection (1000 normalized counts) to reach center,
    // in ms. Default 2000 ms, reasoned as follows:
    //   - at the 50 Hz control tick this is 10 counts/tick (1% of half-travel
    //     per tick) -- a visibly smooth glide, never a snap;
    //   - the MG90S sweeps full travel in roughly 0.4 s unloaded, so a 2 s
    //     ramp is well inside what the servo tracks faithfully;
    //   - it is fast enough that the FPV view is already homing to a useful
    //     forward view within a driver's reaction time to a link loss, and
    //     gentle enough for the showpiece bar (gentle > racy, vision intent);
    //   - same feel class as the mapper-side U4 stale decay design
    //     (rate-limited return to center, never a step) -- the two failsafe
    //     layers stay consistent to the eye.
    uint16_t fullToCenterMs = 2000;

    // Floor 100 ms: faster is indistinguishable from the snap this decay
    // exists to avoid (5 control ticks). Ceil 20 s: slower reads as "stuck",
    // defeating decision 11. Both are type-representable in uint16_t.
    static constexpr uint16_t kMinFullToCenterMs = 100;
    static constexpr uint16_t kMaxFullToCenterMs = 20000;

    constexpr bool valid() const {
        return fullToCenterMs >= kMinFullToCenterMs && fullToCenterMs <= kMaxFullToCenterMs;
    }
};

// Gimbal link-loss failsafe shaping, one instance per axis (pan, tilt).
//
// Vision decision 11 (2026-08-16): on radio-loss failsafe the camera gimbal
// DECAYS TO CENTER instead of holding its last look direction (the previous
// shipped behavior). This class is the pure-logic slew implementing it:
//
//   - link OK, not slewing -> transparent passthrough. Normal stick aiming is
//     deliberately NOT rate-limited: outside failsafe episodes the gimbal
//     behaves exactly as it did before this module existed.
//   - failsafe engaged     -> the output walks from its current value toward
//     0 (center) at the configured rate, then holds center. The commanded
//     input is ignored while engaged (main.cpp freezes `controls` during an
//     outage anyway; ignoring it here makes that a non-dependency).
//   - failsafe released    -> the output walks toward the LIVE stick command
//     at the same rate until it reaches it (release without a snap, even if
//     the stick moved during the outage), then passthrough resumes.
//
// Pure logic: caller-supplied time, no hardware, no Arduino headers. It sits
// between ChannelDecoder's pan/tilt and ServoOutput::setPosition() inside the
// 50 Hz control tick. It computes gimbal positions ONLY -- steering, throttle
// and DRS failsafe semantics are untouched by design (test-pinned).
class GimbalDecay {
public:
    explicit GimbalDecay(GimbalDecayConfig config = GimbalDecayConfig{});

    // Call every control tick.
    //   nowMs           - current time (e.g. millis()), supplied by the caller.
    //                     Unsigned tick math is rollover-safe.
    //   failsafeEngaged - true while the failsafe FSM reports Safe.
    //   commanded       - stick-commanded position in [-1000, +1000]
    //                     (out-of-range input is clamped defensively, the
    //                     same contract as ServoOutput::setPosition).
    // Returns the position to hand to ServoOutput::setPosition() this tick.
    int16_t update(uint32_t nowMs, bool failsafeEngaged, int16_t commanded);

    // Runtime reconfiguration (bench tuning console). Config-copy plus a
    // reset of the sub-count carry ONLY -- position and slewing state are
    // preserved, and the carry reset means a mid-slew retune can never mint a
    // step out of a remainder accumulated against the old rate. Caller is
    // responsible for having validated the config (loader/console both do).
    void setConfig(const GimbalDecayConfig& config);
    const GimbalDecayConfig& config() const { return config_; }

    // True while the output is being slewed (failsafe decay or post-recovery
    // reconvergence) rather than passed through.
    bool slewing() const { return slewing_; }

private:
    void slewToward(int16_t target, uint32_t dtMs);

    GimbalDecayConfig config_;
    int16_t output_ = 0;        // boot-safe: center, matching setup()'s gimbal init
    bool slewing_ = false;
    bool haveTick_ = false;     // first update() ever has no dt, only state
    uint32_t lastTickMs_ = 0;
    uint16_t carryCountMs_ = 0; // sub-count step-budget remainder, in count*ms
};

} // namespace failsafe
