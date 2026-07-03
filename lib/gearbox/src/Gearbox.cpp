#include "gearbox/Gearbox.hpp"

namespace gearbox {

int16_t shapeThrottle(int16_t normalizedThrottle, const GearParams& gear) {
    int32_t x = normalizedThrottle;
    if (x > 1000) {
        x = 1000;
    } else if (x < -1000) {
        x = -1000;
    }

    // Brake/reverse passes through unshaped (see header note on ESC modes).
    if (x <= 0) {
        return static_cast<int16_t>(x);
    }

    // Standard RC expo blend: out = expo * x^3 + (1 - expo) * x, in integer
    // math on the 0..1000 scale. x^3 <= 1e9 fits int32; endpoint-exact:
    // x=1000 -> x3=1000 -> shaped=1000 for every expo value.
    const int32_t x3 = (x * x * x) / 1000000;
    const int32_t expo = gear.expoPercent;
    const int32_t shaped = (expo * x3 + (100 - expo) * x) / 100;

    return static_cast<int16_t>((shaped * gear.maxOutput) / 1000);
}

Gearbox::Gearbox(GearboxConfig config) : config_(config) {
    if (config_.numGears < 1) {
        config_.numGears = 1;
    } else if (config_.numGears > GearboxConfig::kMaxGears) {
        config_.numGears = GearboxConfig::kMaxGears;
    }
    if (config_.initialGear >= config_.numGears) {
        config_.initialGear = config_.numGears - 1;
    }
    currentGear_ = config_.initialGear;
}

void Gearbox::shiftUp() {
    if (currentGear_ + 1 < config_.numGears) {
        currentGear_ += 1;
    }
}

void Gearbox::shiftDown() {
    if (currentGear_ > 0) {
        currentGear_ -= 1;
    }
}

void Gearbox::setGear(uint8_t gear) {
    currentGear_ = (gear < config_.numGears) ? gear : static_cast<uint8_t>(config_.numGears - 1);
}

void Gearbox::setConfig(const GearboxConfig& config) {
    config_ = config;
    if (config_.numGears < 1) {
        config_.numGears = 1;
    } else if (config_.numGears > GearboxConfig::kMaxGears) {
        config_.numGears = GearboxConfig::kMaxGears;
    }
    // Re-clamp the CURRENT gear into the (possibly smaller) new range; do not
    // reset to initialGear.
    if (currentGear_ >= config_.numGears) {
        currentGear_ = static_cast<uint8_t>(config_.numGears - 1);
    }
}

int16_t Gearbox::apply(int16_t normalizedThrottle) const {
    return shapeThrottle(normalizedThrottle, config_.gears[currentGear_]);
}

} // namespace gearbox
