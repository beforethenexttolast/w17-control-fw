#include "channels/ArmGate.hpp"

namespace channels {

ArmGate::ArmGate(ArmGateConfig config) : config_(config) {}

bool ArmGate::update(bool armSwitchOn, int16_t normalizedThrottle, bool forceDisarm) {
    // Failsafe episode seen with the switch on (includes boot-with-switch-on:
    // the FSM boots Safe): latch the OFF->ON toggle requirement. Level-driven
    // on purpose -- a toggle completed while still Safe re-latches here, so
    // the arming ON edge must be observed on a proven link (fail-closed;
    // OWNER-RATIFIED 2026-08-20, header contract).
    if (armSwitchOn && forceDisarm) {
        switchToggleRequired_ = true;
    }

    if (!armSwitchOn) {
        // Switch observed OFF: the toggle's first half. Also the ordinary
        // switch-off disarm.
        switchToggleRequired_ = false;
        seenNeutralSinceEnable_ = false;
        return false;
    }

    if (forceDisarm || switchToggleRequired_) {
        seenNeutralSinceEnable_ = false;
        return false;
    }

    if (!seenNeutralSinceEnable_) {
        const int16_t magnitude =
            normalizedThrottle >= 0 ? normalizedThrottle : static_cast<int16_t>(-normalizedThrottle);
        if (magnitude <= config_.neutralWindow) {
            seenNeutralSinceEnable_ = true;
        }
    }

    return seenNeutralSinceEnable_;
}

} // namespace channels
