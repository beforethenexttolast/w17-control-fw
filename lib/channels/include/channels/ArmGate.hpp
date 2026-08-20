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
// Failsafe-episode latch (OWNER-RATIFIED 2026-08-20): a failsafe episode
// observed while the switch is on latches a disarm that outlives the
// episode. Re-arming then requires the switch to be seen OFF and then ON
// again (plus the neutral-stick condition, as always) -- a link recovery
// with the switch still taped on can NEVER re-arm the car by itself; after
// a radio dropout it stays dead until a deliberate restart.
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
    // Order within one update: (1) any tick with forceDisarm && armSwitchOn
    // sets the require-toggle latch; (2) if !armSwitchOn, clear BOTH latches
    // (the OFF half of the required toggle) and return disarmed; (3) if
    // forceDisarm or the require-toggle latch holds, clear the neutral-seen
    // latch and return disarmed; (4) otherwise latch neutral if |throttle| <=
    // neutralWindow; (5) armed == neutral latch. Consequences, all
    // deliberate:
    //   - switch-on + neutral stick still arms on that same tick when no
    //     latch holds, and after ANY disarm neutral must be re-observed --
    //     a link recovery mid-stick-input cannot snap the motor on;
    //   - after a failsafe episode with the switch on, recovery + neutral +
    //     switch-still-on NEVER arms: the switch must be seen OFF (clears
    //     the latch), then ON again, then neutral (2026-08-20 rule above);
    //   - the latch is LEVEL-driven, so an OFF->ON toggle completed entirely
    //     inside the outage does not count (the ON tick, still Safe,
    //     re-latches): the arming ON must be observed on a proven link --
    //     strictly the fail-closed direction;
    //   - boot with the switch already ON is indistinguishable from a
    //     latched episode (the FSM boots Safe, so the first decoded
    //     switch-on tick latches): the gate demands one deliberate OFF->ON
    //     toggle before the first arm. Boot with the switch OFF (the normal
    //     case) never sets the latch and first arm is unchanged.
    bool update(bool armSwitchOn, int16_t normalizedThrottle, bool forceDisarm);

    bool isArmed() const { return seenNeutralSinceEnable_; }

private:
    ArmGateConfig config_;
    // Doubles as the armed flag: set only while the switch is on and no
    // disarm condition holds, cleared by any disarm.
    bool seenNeutralSinceEnable_ = false;
    // The 2026-08-20 failsafe-episode latch: set on any (forceDisarm &&
    // armSwitchOn) tick, cleared only by observing the switch OFF.
    bool switchToggleRequired_ = false;
};

} // namespace channels
