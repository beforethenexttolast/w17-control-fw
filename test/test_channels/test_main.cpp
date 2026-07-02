#include <unity.h>

#include "channels/ArmGate.hpp"
#include "channels/ChannelDecoder.hpp"

using channels::ArmGate;
using channels::ArmGateConfig;
using channels::ChannelDecoder;
using channels::ChannelMapConfig;
using channels::Controls;

namespace {

constexpr uint16_t kRawOff = crsf::kChannelRawMin;    // 172, switch low position
constexpr uint16_t kRawOn = crsf::kChannelRawMax;     // 1811, switch high position
constexpr uint16_t kRawMid = crsf::kChannelRawCenter; // 992, inside the hysteresis band

crsf::RcChannelsFrame makeFrame(uint16_t fill = crsf::kChannelRawCenter) {
    crsf::RcChannelsFrame frame{};
    for (size_t i = 0; i < crsf::kNumChannels; ++i) {
        frame.channels[i] = fill;
    }
    return frame;
}

} // namespace

void setUp() {}
void tearDown() {}

// --- ChannelDecoder: analog normalization ---

void test_normalization_exact_at_crsf_anchors() {
    ChannelDecoder decoder;
    auto frame = makeFrame();

    frame.channels[0] = crsf::kChannelRawMin;    // steering
    frame.channels[2] = crsf::kChannelRawMax;    // throttle
    Controls c = decoder.decode(frame);
    TEST_ASSERT_EQUAL_INT16(-1000, c.steering);
    TEST_ASSERT_EQUAL_INT16(1000, c.throttle);

    frame.channels[0] = crsf::kChannelRawCenter;
    c = decoder.decode(frame);
    TEST_ASSERT_EQUAL_INT16(0, c.steering);
}

void test_normalization_truncates_toward_neutral() {
    ChannelDecoder decoder;
    auto frame = makeFrame();

    // One raw unit off center on each side: low span is 820, high span 819,
    // integer division truncates toward zero (the safe, neutral-biased direction).
    frame.channels[0] = 991;
    Controls c = decoder.decode(frame);
    TEST_ASSERT_EQUAL_INT16(-1, c.steering);

    frame.channels[0] = 993;
    c = decoder.decode(frame);
    TEST_ASSERT_EQUAL_INT16(1, c.steering);
}

void test_normalization_clamps_out_of_range_raw() {
    ChannelDecoder decoder;
    auto frame = makeFrame();

    // The 11-bit field can physically carry 0..2047, outside the nominal
    // 172..1811 CRSF range (e.g. a zero-initialized frame).
    frame.channels[0] = 0;
    Controls c = decoder.decode(frame);
    TEST_ASSERT_EQUAL_INT16(-1000, c.steering);

    frame.channels[0] = 2047;
    c = decoder.decode(frame);
    TEST_ASSERT_EQUAL_INT16(1000, c.steering);
}

void test_invert_flags_flip_analog_sign() {
    ChannelMapConfig config;
    config.invertSteering = true;
    config.invertThrottle = true;
    ChannelDecoder decoder(config);

    auto frame = makeFrame();
    frame.channels[0] = crsf::kChannelRawMax;
    frame.channels[2] = crsf::kChannelRawMin;
    const Controls c = decoder.decode(frame);

    TEST_ASSERT_EQUAL_INT16(-1000, c.steering);
    TEST_ASSERT_EQUAL_INT16(1000, c.throttle);
}

// --- ChannelDecoder: switches, hysteresis, edges ---

void test_switch_hysteresis_on_off_and_hold_in_band() {
    ChannelDecoder decoder;
    auto frame = makeFrame();

    frame.channels[5] = kRawOff; // DRS switch low
    Controls c = decoder.decode(frame);
    TEST_ASSERT_FALSE(c.drsSwitch);

    frame.channels[5] = kRawOn;
    c = decoder.decode(frame);
    TEST_ASSERT_TRUE(c.drsSwitch);

    frame.channels[5] = kRawMid; // inside the band: hold ON
    c = decoder.decode(frame);
    TEST_ASSERT_TRUE(c.drsSwitch);

    frame.channels[5] = kRawOff;
    c = decoder.decode(frame);
    TEST_ASSERT_FALSE(c.drsSwitch);

    frame.channels[5] = kRawMid; // inside the band: hold OFF
    c = decoder.decode(frame);
    TEST_ASSERT_FALSE(c.drsSwitch);
}

void test_first_decode_seeds_levels_and_fires_no_edges() {
    ChannelDecoder decoder;
    auto frame = makeFrame();
    frame.channels[4] = kRawOn; // arm switch parked ON at boot
    frame.channels[6] = kRawOn; // gear-up switch parked ON at boot

    const Controls c = decoder.decode(frame);

    TEST_ASSERT_TRUE(c.armSwitch);     // level seeds from the frame...
    TEST_ASSERT_FALSE(c.gearUpEdge);   // ...but a parked switch is not a request
    TEST_ASSERT_FALSE(c.gearDownEdge);
}

void test_exactly_one_edge_per_transition() {
    ChannelDecoder decoder;
    auto frame = makeFrame();

    frame.channels[6] = kRawOff;
    decoder.decode(frame); // first decode: seed OFF

    frame.channels[6] = kRawOn;
    Controls c = decoder.decode(frame);
    TEST_ASSERT_TRUE(c.gearUpEdge); // the transition tick

    // Held ON across several decodes: no further edges.
    for (int i = 0; i < 5; ++i) {
        c = decoder.decode(frame);
        TEST_ASSERT_FALSE(c.gearUpEdge);
    }

    frame.channels[6] = kRawOff;
    c = decoder.decode(frame);
    TEST_ASSERT_FALSE(c.gearUpEdge); // ON->OFF is not an edge

    frame.channels[6] = kRawOn;
    c = decoder.decode(frame);
    TEST_ASSERT_TRUE(c.gearUpEdge); // a fresh OFF->ON fires again
}

void test_custom_channel_remap() {
    ChannelMapConfig config;
    config.steeringIndex = 3;
    config.drsIndex = 10;
    TEST_ASSERT_TRUE(config.valid());
    ChannelDecoder decoder(config);

    auto frame = makeFrame();
    frame.channels[3] = crsf::kChannelRawMax;
    frame.channels[10] = kRawOn;
    const Controls c = decoder.decode(frame);

    TEST_ASSERT_EQUAL_INT16(1000, c.steering);
    TEST_ASSERT_TRUE(c.drsSwitch);
}

void test_invalid_index_means_control_absent() {
    ChannelMapConfig config;
    config.panIndex = 255; // deliberately absent (valid() does not police pan/tilt)
    ChannelDecoder decoder(config);

    auto frame = makeFrame(crsf::kChannelRawMax); // every real channel pegged high
    const Controls c = decoder.decode(frame);

    TEST_ASSERT_EQUAL_INT16(0, c.pan); // absent control decodes to neutral
}

void test_config_valid_rejects_bad_values() {
    TEST_ASSERT_TRUE(ChannelMapConfig{}.valid());

    ChannelMapConfig badIndex;
    badIndex.throttleIndex = 16;
    TEST_ASSERT_FALSE(badIndex.valid());

    ChannelMapConfig badThresholds;
    badThresholds.switchOnAbove = -300; // on threshold below off threshold
    TEST_ASSERT_FALSE(badThresholds.valid());
}

// --- ArmGate ---

void test_armgate_blocks_arm_into_full_throttle() {
    ArmGate gate;

    // CLAUDE.md 6.2: switch ON with the stick displaced must NOT arm...
    TEST_ASSERT_FALSE(gate.update(true, 1000, false));
    TEST_ASSERT_FALSE(gate.update(true, 800, false));
    // ...until the throttle is observed at neutral once...
    TEST_ASSERT_TRUE(gate.update(true, 0, false));
    // ...after which throttle passes normally.
    TEST_ASSERT_TRUE(gate.update(true, 1000, false));
}

void test_armgate_switch_on_with_neutral_arms_same_tick() {
    ArmGate gate;
    TEST_ASSERT_TRUE(gate.update(true, 0, false));
}

void test_armgate_disarms_on_switch_off_and_requires_neutral_again() {
    ArmGate gate;
    gate.update(true, 0, false); // armed

    TEST_ASSERT_FALSE(gate.update(false, 0, false)); // switch off: instant disarm
    TEST_ASSERT_FALSE(gate.update(true, 500, false)); // back on, stick displaced: blocked
    TEST_ASSERT_TRUE(gate.update(true, 30, false));   // neutral re-observed: armed
}

void test_armgate_force_disarm_polarity() {
    ArmGate gate;
    // forceDisarm=true (failsafe Safe) must disarm even with switch ON and
    // stick at neutral -- pins the parameter polarity.
    TEST_ASSERT_FALSE(gate.update(true, 0, true));
    TEST_ASSERT_FALSE(gate.isArmed());
}

void test_armgate_failsafe_recovery_requires_fresh_neutral() {
    ArmGate gate;
    gate.update(true, 0, false); // armed
    gate.update(true, 900, false); // driving at high throttle

    TEST_ASSERT_FALSE(gate.update(true, 900, true)); // failsafe episode: disarm

    // Link recovers while the stick is still pinned (review finding A3):
    // throttle must NOT snap on.
    TEST_ASSERT_FALSE(gate.update(true, 900, false));
    TEST_ASSERT_TRUE(gate.update(true, 0, false)); // neutral again: re-armed
}

void test_armgate_neutral_while_switch_off_does_not_prearm() {
    ArmGate gate;
    gate.update(false, 0, false); // neutral observed, but switch is OFF
    // Turning the switch on with the stick displaced must still be blocked --
    // proves the latch was not set while disarmed.
    TEST_ASSERT_FALSE(gate.update(true, 500, false));
}

void test_armgate_neutral_window_boundary() {
    ArmGate gateAtBoundary;
    TEST_ASSERT_TRUE(gateAtBoundary.update(true, 60, false)); // |thr| == window: neutral

    ArmGate gateJustOutside;
    TEST_ASSERT_FALSE(gateJustOutside.update(true, 61, false));

    ArmGate gateNegativeBoundary;
    TEST_ASSERT_TRUE(gateNegativeBoundary.update(true, -60, false));

    ArmGate gateNegativeOutside;
    TEST_ASSERT_FALSE(gateNegativeOutside.update(true, -61, false));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_normalization_exact_at_crsf_anchors);
    RUN_TEST(test_normalization_truncates_toward_neutral);
    RUN_TEST(test_normalization_clamps_out_of_range_raw);
    RUN_TEST(test_invert_flags_flip_analog_sign);
    RUN_TEST(test_switch_hysteresis_on_off_and_hold_in_band);
    RUN_TEST(test_first_decode_seeds_levels_and_fires_no_edges);
    RUN_TEST(test_exactly_one_edge_per_transition);
    RUN_TEST(test_custom_channel_remap);
    RUN_TEST(test_invalid_index_means_control_absent);
    RUN_TEST(test_config_valid_rejects_bad_values);
    RUN_TEST(test_armgate_blocks_arm_into_full_throttle);
    RUN_TEST(test_armgate_switch_on_with_neutral_arms_same_tick);
    RUN_TEST(test_armgate_disarms_on_switch_off_and_requires_neutral_again);
    RUN_TEST(test_armgate_force_disarm_polarity);
    RUN_TEST(test_armgate_failsafe_recovery_requires_fresh_neutral);
    RUN_TEST(test_armgate_neutral_window_boundary);
    return UNITY_END();
}
