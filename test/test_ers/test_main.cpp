#include <unity.h>

#include "ers/ErsSystem.hpp"

using ers::ErsConfig;
using ers::ErsSystem;

void setUp() {}
void tearDown() {}

namespace {

// Runs `n` active update ticks 20ms apart, starting after a seed at t0=0.
// Returns the timestamp of the last tick.
uint32_t runTicks(ErsSystem& e, int n, int16_t cmd, uint16_t rpm, bool boost, bool overtake,
                   uint32_t startMs = 0) {
    uint32_t t = startMs;
    e.update(t, true, 0, 0, false, false); // seed
    for (int i = 0; i < n; ++i) {
        t += 20;
        e.update(t, true, cmd, rpm, boost, overtake);
    }
    return t;
}

} // namespace

void test_starts_full() {
    ErsSystem e;
    TEST_ASSERT_EQUAL_UINT8(100, e.energyPercent());
    TEST_ASSERT_FALSE(e.deploying());
}

void test_deploy_drains_at_exact_rate() {
    ErsSystem e;
    // 50 ticks x 20ms = 1s of boost at 260 permille/s = -26%: micro-permille
    // accumulation is exact (260 * 20 * 50 = 260000 = 26.0%).
    runTicks(e, 50, /*cmd=*/500, /*rpm=*/1000, /*boost=*/true, false);
    TEST_ASSERT_EQUAL_UINT8(74, e.energyPercent());
    TEST_ASSERT_TRUE(e.deploying());
}

void test_overtake_drains_faster_and_wins_over_boost() {
    ErsSystem e;
    // Both buttons held: overtake takes precedence (400 permille/s -> -40%).
    runTicks(e, 50, 500, 1000, true, true);
    TEST_ASSERT_EQUAL_UINT8(60, e.energyPercent());
}

void test_no_deploy_without_positive_throttle() {
    ErsSystem e;
    runTicks(e, 50, /*cmd=*/0, 1000, true, false); // disarmed/neutral: cmd 0
    TEST_ASSERT_EQUAL_UINT8(100, e.energyPercent());
    TEST_ASSERT_FALSE(e.deploying());

    runTicks(e, 50, /*cmd=*/-500, 1000, true, false); // braking: no deploy either
    TEST_ASSERT_FALSE(e.deploying());
}

void test_harvest_requires_motion() {
    ErsConfig config;
    ErsSystem e(config);
    // Drain some first so there is headroom.
    uint32_t t = runTicks(e, 50, 500, 1000, true, false); // 74%

    // Braking at standstill: nothing (a parked car never creeps energy).
    for (int i = 0; i < 50; ++i) {
        t += 20;
        e.update(t, true, -500, /*rpm=*/0, false, false);
    }
    TEST_ASSERT_EQUAL_UINT8(74, e.energyPercent());

    // Braking while moving: 1s at 110 permille/s = +11%.
    for (int i = 0; i < 50; ++i) {
        t += 20;
        e.update(t, true, -500, /*rpm=*/800, false, false);
    }
    TEST_ASSERT_EQUAL_UINT8(85, e.energyPercent());
}

void test_coast_harvest_exact_slow_rate() {
    ErsSystem e;
    uint32_t t = runTicks(e, 100, 500, 1000, true, false); // 2s deploy -> 48%

    // Coasting (|cmd| <= 100) while rolling: 60 permille/s must accumulate
    // exactly -- this pins the micro-permille scheme (a plain permille
    // accumulator truncates 1.2/tick down to 1, -17%).
    for (int i = 0; i < 50; ++i) {
        t += 20;
        e.update(t, true, 50, 600, false, false);
    }
    TEST_ASSERT_EQUAL_UINT8(54, e.energyPercent()); // 48 + 6
}

void test_energy_caps_at_full() {
    ErsSystem e;
    runTicks(e, 200, -500, 1000, false, false); // 4s braking from full
    TEST_ASSERT_EQUAL_UINT8(100, e.energyPercent());
}

void test_empty_store_stops_deploying() {
    ErsConfig config;
    ErsSystem e(config);
    // 260 permille/s empties the store in ~3.85s; run 5s.
    runTicks(e, 250, 500, 1000, true, false);
    TEST_ASSERT_EQUAL_UINT8(0, e.energyPercent());
    TEST_ASSERT_FALSE(e.deploying()); // no energy at tick start -> no bonus
    TEST_ASSERT_EQUAL_INT16(400, e.applyBoost(400));
}

void test_freeze_preserves_energy_and_clock() {
    ErsSystem e;
    uint32_t t = runTicks(e, 50, 500, 1000, true, false); // 74%

    // 30s frozen (other mode / failsafe), boost switch stale-held the whole
    // time: energy must not move and deploying must read false.
    for (int i = 0; i < 10; ++i) {
        t += 3000;
        e.update(t, /*ersActive=*/false, 500, 1000, true, false);
        TEST_ASSERT_FALSE(e.deploying());
    }
    TEST_ASSERT_EQUAL_UINT8(74, e.energyPercent());

    // First tick after reactivation: dt is one tick, not the 30s gap.
    t += 20;
    e.update(t, true, 500, 1000, true, false);
    TEST_ASSERT_EQUAL_UINT8(73, e.energyPercent()); // -0.52%, floor of 73.48
}

void test_dt_stall_clamp() {
    ErsSystem e;
    e.update(0, true, 0, 0, false, false); // seed
    // One late tick of 5s while boosting: clamped to 100ms of drain (2.6%).
    e.update(5000, true, 500, 1000, true, false);
    TEST_ASSERT_EQUAL_UINT8(97, e.energyPercent()); // 100 - 2.6 -> 97.4 -> 97
}

void test_apply_boost_multiplies_and_clamps() {
    ErsSystem e;
    runTicks(e, 1, 500, 1000, true, false); // deploying with boost (+18%)

    TEST_ASSERT_TRUE(e.deploying());
    TEST_ASSERT_EQUAL_INT16(472, e.applyBoost(400));  // gear 1 ceiling: 400 * 1.18
    TEST_ASSERT_EQUAL_INT16(944, e.applyBoost(800));
    TEST_ASSERT_EQUAL_INT16(1000, e.applyBoost(900)); // 1062 clamped to physical max
    TEST_ASSERT_EQUAL_INT16(1000, e.applyBoost(1000)); // top gear: no headroom, no-op
}

void test_apply_boost_zero_and_negative_invariant() {
    ErsSystem e;
    runTicks(e, 1, 500, 1000, true, false); // deploying

    // HARD INVARIANT: purely multiplicative. applyBoost(0) == 0 means the
    // boost can never bypass the arm gate; negatives (brake) pass through.
    TEST_ASSERT_EQUAL_INT16(0, e.applyBoost(0));
    TEST_ASSERT_EQUAL_INT16(-600, e.applyBoost(-600));
}

void test_overtake_bonus_hits_exactly_full_in_gear_three() {
    ErsSystem e;
    runTicks(e, 1, 500, 1000, false, /*overtake=*/true);
    TEST_ASSERT_EQUAL_INT16(1000, e.applyBoost(800)); // 800 * 1.25 = 1000 exactly
}

void test_config_valid_rejects_bad_values() {
    TEST_ASSERT_TRUE(ErsConfig{}.valid());

    ErsConfig zeroDeploy;
    zeroDeploy.deployRatePermille = 0;
    TEST_ASSERT_FALSE(zeroDeploy.valid());

    ErsConfig wildBonus;
    wildBonus.boostBonusPermille = 1001;
    TEST_ASSERT_FALSE(wildBonus.valid());

    ErsConfig positiveBrake;
    positiveBrake.brakeThreshold = 40;
    TEST_ASSERT_FALSE(positiveBrake.valid());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_full);
    RUN_TEST(test_deploy_drains_at_exact_rate);
    RUN_TEST(test_overtake_drains_faster_and_wins_over_boost);
    RUN_TEST(test_no_deploy_without_positive_throttle);
    RUN_TEST(test_harvest_requires_motion);
    RUN_TEST(test_coast_harvest_exact_slow_rate);
    RUN_TEST(test_energy_caps_at_full);
    RUN_TEST(test_empty_store_stops_deploying);
    RUN_TEST(test_freeze_preserves_energy_and_clock);
    RUN_TEST(test_dt_stall_clamp);
    RUN_TEST(test_apply_boost_multiplies_and_clamps);
    RUN_TEST(test_apply_boost_zero_and_negative_invariant);
    RUN_TEST(test_overtake_bonus_hits_exactly_full_in_gear_three);
    RUN_TEST(test_config_valid_rejects_bad_values);
    return UNITY_END();
}
