#include "ers/ErsSystem.hpp"

namespace ers {

ErsSystem::ErsSystem(ErsConfig config) : config_(config) {}

void ErsSystem::update(uint32_t nowMs, bool ersActive, int16_t commandedThrottle,
                       uint16_t wheelRpm, bool boostHeld, bool overtakeHeld) {
    activeBonusPermille_ = 0;

    if (!seeded_ || !ersActive) {
        // Frozen: keep re-seeding the clock so reactivation never sees a dt gap.
        seeded_ = true;
        lastMs_ = nowMs;
        return;
    }

    uint32_t dtMs = nowMs - lastMs_;
    lastMs_ = nowMs;
    if (dtMs > kMaxDtMs) {
        dtMs = kMaxDtMs;
    }

    const bool wantsDeploy = (boostHeld || overtakeHeld) && commandedThrottle > 0;
    if (wantsDeploy && energyMicroPermille_ > 0) {
        const uint16_t rate =
            overtakeHeld ? config_.overtakeRatePermille : config_.deployRatePermille;
        const int32_t drain = static_cast<int32_t>(rate) * static_cast<int32_t>(dtMs);
        energyMicroPermille_ = (drain >= energyMicroPermille_) ? 0 : energyMicroPermille_ - drain;
        // Bonus applies this tick (there was energy at tick start).
        activeBonusPermille_ =
            overtakeHeld ? config_.overtakeBonusPermille : config_.boostBonusPermille;
        return;
    }

    if (wheelRpm == 0) {
        return; // no motion, no regen
    }

    uint16_t harvestRate = 0;
    if (commandedThrottle <= config_.brakeThreshold) {
        harvestRate = config_.harvestBrakeRatePermille;
    } else if (commandedThrottle >= -config_.coastThreshold &&
               commandedThrottle <= config_.coastThreshold) {
        harvestRate = config_.harvestCoastRatePermille;
    }
    if (harvestRate > 0) {
        const int32_t gain = static_cast<int32_t>(harvestRate) * static_cast<int32_t>(dtMs);
        energyMicroPermille_ += gain;
        if (energyMicroPermille_ > kFullMicroPermille) {
            energyMicroPermille_ = kFullMicroPermille;
        }
    }
}

int16_t ErsSystem::applyBoost(int16_t shapedThrottle) const {
    if (activeBonusPermille_ == 0 || shapedThrottle <= 0) {
        return shapedThrottle; // multiplicative only: applyBoost(0) == 0, brakes untouched
    }
    int32_t boosted = static_cast<int32_t>(shapedThrottle) *
                      (1000 + static_cast<int32_t>(activeBonusPermille_)) / 1000;
    if (boosted > 1000) {
        boosted = 1000;
    }
    return static_cast<int16_t>(boosted);
}

} // namespace ers
