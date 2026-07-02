#pragma once

#include <cstdint>

namespace ers {

struct ErsConfig {
    // Rates in per-mille of the full store per second. Values match the
    // ground-station HUD model (docs/f1_hud.html: deploy 26%/s, harvest
    // 11%/s, boost ceiling x1.18); the separate, deeper overtake deploy is
    // our own flavor on top of the HUD's single boost.
    uint16_t deployRatePermille = 260;
    uint16_t overtakeRatePermille = 400;
    uint16_t harvestBrakeRatePermille = 110;
    uint16_t harvestCoastRatePermille = 60;

    // Output multiplier bonuses while deploying, applied post-gearbox.
    uint16_t boostBonusPermille = 180;    // +18% (HUD BOOST_CAP_BONUS)
    uint16_t overtakeBonusPermille = 250; // +25% (gear 3: 800 -> exactly 1000)

    // Commanded-throttle bands (same +/-1000 scale as the control chain).
    // brakeThreshold matches Link2Sender's brake-light on-threshold so the
    // brake light and brake-harvest broadly agree; commanded in
    // (brakeThreshold, -coastThreshold] harvests at coast rate -- the knobs
    // are deliberately independent.
    int16_t brakeThreshold = -40;
    int16_t coastThreshold = 100; // |commanded| <= this while MOVING = coasting

    constexpr bool valid() const {
        return deployRatePermille >= 1 && deployRatePermille <= 1000 &&
               overtakeRatePermille >= 1 && overtakeRatePermille <= 1000 &&
               harvestBrakeRatePermille <= 1000 && harvestCoastRatePermille <= 1000 &&
               boostBonusPermille <= 1000 && overtakeBonusPermille <= 1000 &&
               brakeThreshold < 0 && coastThreshold > 0 && coastThreshold <= 500;
    }
};

// ERS energy store + deploy bonus. Pure logic; time is caller-supplied.
//
// Call update() EVERY control tick, in every drive mode and failsafe state.
// While `ersActive` is false (any mode other than GearboxErs, or failsafe)
// the store FREEZES and the internal clock keeps re-seeding, so reactivation
// never sees a dt gap -- and a stale boost switch held through a failsafe
// episode can neither drain energy nor report "deploying" to the sound board.
//
// Harvest (both brake and coast) requires wheel rpm > 0: braking or coasting
// at standstill charges nothing, so a parked car never creeps energy -- but
// regen while rolling to a stop is (correctly) allowed.
class ErsSystem {
public:
    explicit ErsSystem(ErsConfig config = ErsConfig{});

    //   ersActive         - mode == GearboxErs AND failsafe is Active.
    //   commandedThrottle - post-arm-gate, pre-boost value (0 while disarmed).
    //                       Deploy additionally requires it to be > 0: boost
    //                       is multiplicative, so draining at zero/negative
    //                       throttle would spend energy for nothing.
    //   wheelRpm          - harvest gate: no motion, no regen.
    //   boostHeld/overtakeHeld - decoded switch states; overtake wins.
    void update(uint32_t nowMs, bool ersActive, int16_t commandedThrottle,
                uint16_t wheelRpm, bool boostHeld, bool overtakeHeld);

    uint8_t energyPercent() const {
        return static_cast<uint8_t>(energyMicroPermille_ / 10000);
    }

    bool deploying() const { return activeBonusPermille_ != 0; }

    // Multiplies a POSITIVE post-gearbox throttle by the active deploy bonus,
    // clamped to 1000. HARD INVARIANT (test-pinned): applyBoost(0) == 0 and
    // negative inputs pass through -- the boost is purely multiplicative, so
    // it can never bypass the arm gate or touch braking. In top gear (cap
    // already 1000) boost therefore does nothing: deliberate F1 flavor --
    // ERS punches out of corners in lower gears, DRS is the top-speed tool.
    int16_t applyBoost(int16_t shapedThrottle) const;

private:
    // Energy in micro-permille of full (0..1,000,000): drain/harvest per tick
    // is exactly ratePermillePerSecond * dtMs with NO division, so even the
    // slowest rate accumulates without truncation at 20ms ticks.
    static constexpr int32_t kFullMicroPermille = 1000000;
    // Stall guard: one late tick must not dump seconds of drain/harvest.
    static constexpr uint32_t kMaxDtMs = 100;

    ErsConfig config_;
    int32_t energyMicroPermille_ = kFullMicroPermille; // starts full
    bool seeded_ = false;
    uint32_t lastMs_ = 0;
    uint16_t activeBonusPermille_ = 0; // nonzero only while actually deploying
};

} // namespace ers
