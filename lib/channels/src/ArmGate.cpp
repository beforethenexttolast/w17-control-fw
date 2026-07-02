#include "channels/ArmGate.hpp"

namespace channels {

ArmGate::ArmGate(ArmGateConfig config) : config_(config) {}

bool ArmGate::update(bool armSwitchOn, int16_t normalizedThrottle, bool forceDisarm) {
    if (!armSwitchOn || forceDisarm) {
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
