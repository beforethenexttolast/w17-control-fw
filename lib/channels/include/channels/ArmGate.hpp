#pragma once

#include <cstdint>

namespace channels {

struct ArmGateConfig {
    // |normalized throttle| at or below this counts as "neutral" (~6% of the
    // 1000-unit half-travel -- stick-centering slop plus a little trim).
    int16_t neutralWindow = 60;
};

// The arm-switch gate, CLAUDE.md section 6.2 (non-negotiable safety #2):
// throttle may only pass when the arm switch is ON *and* the throttle has
// been observed at neutral at least once since the gate was last disarmed.
// "No arm-into-full-throttle": flipping the switch with the stick displaced
// keeps the motor off until the stick returns to neutral.
//
// Pure state machine, no hardware dependency.
class ArmGate {
public:
    explicit ArmGate(ArmGateConfig config = ArmGateConfig{});

    // Call every control tick.
    //   armSwitchOn        - decoded arm switch state.
    //   normalizedThrottle - decoded throttle, [-1000, +1000].
    //   forceDisarm        - pass true whenever the failsafe FSM reports Safe
    //                        (named for its effect: true disarms, regardless
    //                        of switch or stick).
    //
    // Order within one update: (1) if !armSwitchOn or forceDisarm, clear the
    // neutral-seen latch and return disarmed; (2) otherwise latch neutral if
    // |throttle| <= neutralWindow; (3) armed == latch. Consequences, both
    // deliberate: switch-on + neutral stick arms on that same tick, and after
    // ANY disarm (switch off, failsafe episode) neutral must be re-observed
    // -- so a link recovery mid-stick-input cannot snap the motor on.
    bool update(bool armSwitchOn, int16_t normalizedThrottle, bool forceDisarm);

    bool isArmed() const { return seenNeutralSinceEnable_; }

private:
    ArmGateConfig config_;
    // Doubles as the armed flag: set only while the switch is on and no
    // disarm condition holds, cleared by any disarm.
    bool seenNeutralSinceEnable_ = false;
};

} // namespace channels
