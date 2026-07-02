#include <unity.h>

#include "gearbox/Gearbox.hpp"

using gearbox::Gearbox;
using gearbox::GearboxConfig;
using gearbox::GearParams;
using gearbox::shapeThrottle;

void setUp() {}
void tearDown() {}

// --- Config validation ---

void test_default_config_is_valid() {
    TEST_ASSERT_TRUE(GearboxConfig{}.valid());
}

void test_config_rejects_bad_values() {
    GearboxConfig zeroGears;
    zeroGears.numGears = 0;
    TEST_ASSERT_FALSE(zeroGears.valid());

    GearboxConfig tooManyGears;
    tooManyGears.numGears = 7; // kMaxGears is 6
    TEST_ASSERT_FALSE(tooManyGears.valid());

    GearboxConfig badInitial;
    badInitial.initialGear = 4; // numGears is 4, so valid indices are 0..3
    TEST_ASSERT_FALSE(badInitial.valid());

    GearboxConfig zeroOutput;
    zeroOutput.gears[1].maxOutput = 0;
    TEST_ASSERT_FALSE(zeroOutput.valid());

    GearboxConfig overOutput;
    overOutput.gears[3].maxOutput = 1001;
    TEST_ASSERT_FALSE(overOutput.valid());

    GearboxConfig badExpo;
    badExpo.gears[0].expoPercent = 101;
    TEST_ASSERT_FALSE(badExpo.valid());

    GearboxConfig decreasing; // low gears gentle, top gear full: must be non-decreasing
    decreasing.gears[1].maxOutput = 300; // below gear 0's 400
    TEST_ASSERT_FALSE(decreasing.valid());
}

void test_constructor_clamps_out_of_range_config() {
    // valid() is the real guard (static_assert at the definition site); the
    // constructor clamps anyway so a bypassed assert cannot index outside the
    // configured gear table.
    GearboxConfig config;
    config.initialGear = 10;
    const Gearbox box(config);
    TEST_ASSERT_EQUAL_UINT8(3, box.currentGear()); // clamped to numGears-1
}

// --- shapeThrottle: linear gears ---

void test_linear_gear_scales_exactly() {
    const GearParams gear{600, 0}; // even maxOutput so x=500 divides exactly

    TEST_ASSERT_EQUAL_INT16(600, shapeThrottle(1000, gear));
    TEST_ASSERT_EQUAL_INT16(300, shapeThrottle(500, gear));
    TEST_ASSERT_EQUAL_INT16(0, shapeThrottle(0, gear));
}

void test_output_is_monotonic_nondecreasing() {
    const GearParams gear{400, 50};
    int16_t previous = 0;
    // Non-strict: integer truncation creates small plateaus, never reversals.
    for (int16_t x = 0; x <= 1000; x += 7) {
        const int16_t out = shapeThrottle(x, gear);
        TEST_ASSERT_TRUE(out >= previous);
        previous = out;
    }
}

// --- shapeThrottle: expo ---

void test_expo_preserves_full_throttle_endpoint() {
    // x=1000 must shape to exactly maxOutput for ANY expo value: the expo
    // blend is endpoint-exact (x3 == x == 1000 there).
    const uint8_t expos[] = {0, 50, 100};
    for (uint8_t expo : expos) {
        const GearParams gear{800, expo};
        TEST_ASSERT_EQUAL_INT16(800, shapeThrottle(1000, gear));
    }
}

void test_full_expo_midpoint_value() {
    // Pure cubic at half stick: x3 = 500^3 / 1e6 = 125, then scaled by 1000.
    const GearParams gear{1000, 100};
    TEST_ASSERT_EQUAL_INT16(125, shapeThrottle(500, gear));
}

void test_expo_softens_midpoint_versus_linear() {
    const GearParams linear{1000, 0};
    const GearParams withExpo{1000, 50};
    TEST_ASSERT_TRUE(shapeThrottle(500, withExpo) < shapeThrottle(500, linear));
}

// --- shapeThrottle: brake/reverse pass-through + clamping ---

void test_negative_throttle_passes_through_unshaped_in_every_gear() {
    const GearboxConfig config;
    for (size_t i = 0; i < config.numGears; ++i) {
        TEST_ASSERT_EQUAL_INT16(-1000, shapeThrottle(-1000, config.gears[i]));
        TEST_ASSERT_EQUAL_INT16(-500, shapeThrottle(-500, config.gears[i]));
    }
}

void test_out_of_range_input_is_clamped() {
    const GearParams gear{400, 0};
    TEST_ASSERT_EQUAL_INT16(400, shapeThrottle(1100, gear));   // never above maxOutput
    TEST_ASSERT_EQUAL_INT16(-1000, shapeThrottle(-1100, gear)); // never below -1000
}

// --- Gearbox: shifting ---

void test_initial_gear_respected() {
    GearboxConfig config;
    config.initialGear = 2;
    const Gearbox box(config);
    TEST_ASSERT_EQUAL_UINT8(2, box.currentGear());
}

void test_shift_up_down_and_saturation() {
    Gearbox box; // 4 gears (indices 0..3), kMaxGears = 6: saturation must hit
                 // numGears-1, not the array bound

    TEST_ASSERT_EQUAL_UINT8(0, box.currentGear());
    box.shiftDown();
    TEST_ASSERT_EQUAL_UINT8(0, box.currentGear()); // saturates at the bottom

    box.shiftUp();
    box.shiftUp();
    box.shiftUp();
    TEST_ASSERT_EQUAL_UINT8(3, box.currentGear());
    box.shiftUp();
    TEST_ASSERT_EQUAL_UINT8(3, box.currentGear()); // saturates at numGears-1
}

void test_set_gear_direct_and_clamped() {
    Gearbox box;
    box.setGear(2);
    TEST_ASSERT_EQUAL_UINT8(2, box.currentGear());
    box.setGear(9);
    TEST_ASSERT_EQUAL_UINT8(3, box.currentGear()); // clamped to top gear
}

void test_apply_follows_current_gear() {
    Gearbox box; // gear 0 = {400, 50}, gear 3 = {1000, 0}

    TEST_ASSERT_EQUAL_INT16(400, box.apply(1000)); // gear 0 ceiling

    box.setGear(3);
    TEST_ASSERT_EQUAL_INT16(1000, box.apply(1000)); // top gear = full power
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_default_config_is_valid);
    RUN_TEST(test_config_rejects_bad_values);
    RUN_TEST(test_constructor_clamps_out_of_range_config);
    RUN_TEST(test_linear_gear_scales_exactly);
    RUN_TEST(test_output_is_monotonic_nondecreasing);
    RUN_TEST(test_expo_preserves_full_throttle_endpoint);
    RUN_TEST(test_full_expo_midpoint_value);
    RUN_TEST(test_expo_softens_midpoint_versus_linear);
    RUN_TEST(test_negative_throttle_passes_through_unshaped_in_every_gear);
    RUN_TEST(test_out_of_range_input_is_clamped);
    RUN_TEST(test_initial_gear_respected);
    RUN_TEST(test_shift_up_down_and_saturation);
    RUN_TEST(test_set_gear_direct_and_clamped);
    RUN_TEST(test_apply_follows_current_gear);
    return UNITY_END();
}
