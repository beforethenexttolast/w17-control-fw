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

void test_normalization_clamps_plausible_out_of_range_raw() {
    ChannelDecoder decoder;
    auto frame = makeFrame();

    // Outside the nominal 172..1811 but inside the plausibility band: an
    // expanded-endpoint TX meant full deflection, so clamp to the endpoint.
    frame.channels[0] = crsf::kChannelRawPlausibleMin; // 100
    Controls c = decoder.decode(frame);
    TEST_ASSERT_EQUAL_INT16(-1000, c.steering);

    frame.channels[0] = crsf::kChannelRawPlausibleMax; // 1900
    c = decoder.decode(frame);
    TEST_ASSERT_EQUAL_INT16(1000, c.steering);
}

void test_implausible_raw_decodes_as_absent_not_full_deflection() {
    ChannelDecoder decoder;
    auto frame = makeFrame();

    // The 11-bit field can physically carry 0..2047. A value this far outside
    // the nominal range is not an expanded endpoint -- it is a sender that is
    // not speaking the protocol. Decoding it as full deflection would slam the
    // steering to full lock on a frame that passes CRC, so failsafe never fires.
    frame.channels[0] = 0;
    Controls c = decoder.decode(frame);
    TEST_ASSERT_EQUAL_INT16(0, c.steering);

    frame.channels[0] = 2047;
    c = decoder.decode(frame);
    TEST_ASSERT_EQUAL_INT16(0, c.steering);
}

void test_plausibility_band_boundaries_are_inclusive() {
    ChannelDecoder decoder;
    auto frame = makeFrame();

    // Inclusive on both edges, absent one step outside.
    frame.channels[0] = crsf::kChannelRawPlausibleMin - 1; // 99
    TEST_ASSERT_EQUAL_INT16(0, decoder.decode(frame).steering);

    frame.channels[0] = crsf::kChannelRawPlausibleMin; // 100
    TEST_ASSERT_EQUAL_INT16(-1000, decoder.decode(frame).steering);

    frame.channels[0] = crsf::kChannelRawPlausibleMax; // 1900
    TEST_ASSERT_EQUAL_INT16(1000, decoder.decode(frame).steering);

    frame.channels[0] = crsf::kChannelRawPlausibleMax + 1; // 1901
    TEST_ASSERT_EQUAL_INT16(0, decoder.decode(frame).steering);
}

void test_implausible_raw_forces_switch_off_and_does_not_hold() {
    ChannelDecoder decoder;

    // Seed the arm switch ON from a valid frame.
    auto frame = makeFrame();
    frame.channels[4] = kRawOn;
    TEST_ASSERT_TRUE(decoder.decode(frame).armSwitch);

    // Now an implausible raw on the same channel. This must force OFF, NOT fall
    // through to hysteresis: a neutral 0 sits inside the dead band and would
    // HOLD the previous state, meaning a garbage payload could not disarm.
    frame.channels[4] = 0;
    TEST_ASSERT_FALSE(decoder.decode(frame).armSwitch);
}

void test_all_zero_payload_decodes_fully_safe() {
    ChannelDecoder decoder;

    // The concrete regression: a sender emitting an all-zeros channel array
    // (e.g. an out-of-band "no data" sentinel) inside a well-formed, CRC-valid
    // frame. Before the plausibility band every analog read full negative
    // deflection -- steering to full lock -- while the link stayed up and
    // failsafe never fired.
    auto frame = makeFrame(0);
    Controls c = decoder.decode(frame);

    TEST_ASSERT_EQUAL_INT16(0, c.steering);
    TEST_ASSERT_EQUAL_INT16(0, c.throttle);
    TEST_ASSERT_EQUAL_INT16(0, c.pan);
    TEST_ASSERT_EQUAL_INT16(0, c.tilt);
    TEST_ASSERT_FALSE(c.armSwitch);
    TEST_ASSERT_FALSE(c.drsSwitch);
    TEST_ASSERT_FALSE(c.boostHeld);
    TEST_ASSERT_FALSE(c.overtakeHeld);
    TEST_ASSERT_EQUAL_UINT8(1, c.driveMode); // RACE, the safe middle
    TEST_ASSERT_FALSE(c.gearUpEdge);
    TEST_ASSERT_FALSE(c.gearDownEdge);
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

void test_pan_tilt_decode_and_invert() {
    ChannelDecoder decoder;
    auto frame = makeFrame();
    frame.channels[8] = crsf::kChannelRawMax;  // pan (ch9) full one way
    frame.channels[9] = crsf::kChannelRawMin;  // tilt (ch10) full the other
    Controls c = decoder.decode(frame);
    TEST_ASSERT_EQUAL_INT16(1000, c.pan);
    TEST_ASSERT_EQUAL_INT16(-1000, c.tilt);

    // Invert flags flip the camera axes (bench direction fix).
    ChannelMapConfig inv;
    inv.invertPan = true;
    inv.invertTilt = true;
    ChannelDecoder decoder2(inv);
    c = decoder2.decode(frame);
    TEST_ASSERT_EQUAL_INT16(-1000, c.pan);
    TEST_ASSERT_EQUAL_INT16(1000, c.tilt);
}

void test_boost_overtake_held_switches() {
    ChannelDecoder decoder;
    auto frame = makeFrame();
    frame.channels[10] = kRawOn;  // boost held
    frame.channels[11] = kRawOff; // overtake off

    Controls c = decoder.decode(frame);
    TEST_ASSERT_TRUE(c.boostHeld); // first decode seeds from level
    TEST_ASSERT_FALSE(c.overtakeHeld);

    frame.channels[10] = kRawOff;
    frame.channels[11] = kRawOn;
    c = decoder.decode(frame);
    TEST_ASSERT_FALSE(c.boostHeld);
    TEST_ASSERT_TRUE(c.overtakeHeld);
}

void test_drive_mode_tri_state_positions() {
    ChannelDecoder decoder;
    auto frame = makeFrame();

    frame.channels[12] = kRawOff; // low detent
    TEST_ASSERT_EQUAL_UINT8(0, decoder.decode(frame).driveMode); // Training

    frame.channels[12] = kRawMid;
    TEST_ASSERT_EQUAL_UINT8(1, decoder.decode(frame).driveMode); // Gearbox

    frame.channels[12] = kRawOn;
    TEST_ASSERT_EQUAL_UINT8(2, decoder.decode(frame).driveMode); // Gearbox+ERS
}

void test_drive_mode_boundaries_are_exclusive() {
    ChannelDecoder decoder;
    auto frame = makeFrame();

    // Normalized -332 (raw 719) and +333 (raw 1265) stay in the mid band;
    // -334 (raw 718) and +334 (raw 1266) tip into the outer modes.
    frame.channels[12] = 719;
    TEST_ASSERT_EQUAL_UINT8(1, decoder.decode(frame).driveMode);
    frame.channels[12] = 718;
    TEST_ASSERT_EQUAL_UINT8(0, decoder.decode(frame).driveMode);
    frame.channels[12] = 1265;
    TEST_ASSERT_EQUAL_UINT8(1, decoder.decode(frame).driveMode);
    frame.channels[12] = 1266;
    TEST_ASSERT_EQUAL_UINT8(2, decoder.decode(frame).driveMode);
}

void test_absent_mode_and_boost_channels_degrade_safely() {
    ChannelMapConfig config;
    config.driveModeIndex = 255;
    config.boostIndex = 255;
    ChannelDecoder decoder(config);

    auto frame = makeFrame(crsf::kChannelRawMax); // everything pegged high
    const Controls c = decoder.decode(frame);

    TEST_ASSERT_EQUAL_UINT8(1, c.driveMode); // absent mode channel = Gearbox
    TEST_ASSERT_FALSE(c.boostHeld);          // absent boost = off
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

// OWNER-RATIFIED 2026-08-20: a failsafe episode latches a disarm. Link
// recovery with the switch still on can NEVER re-arm by itself -- not with
// the stick pinned (review finding A3, unchanged) and not at neutral either
// (the pre-2026-08-20 behavior this test used to pin). Re-arming needs the
// switch seen OFF, then ON, then the fresh-neutral rule as always.
void test_armgate_failsafe_episode_latches_until_switch_toggle() {
    ArmGate gate;
    gate.update(true, 0, false);   // armed
    gate.update(true, 900, false); // driving at high throttle

    TEST_ASSERT_FALSE(gate.update(true, 900, true)); // failsafe episode: disarm

    // Link recovers, switch still on: stick pinned OR neutral, many ticks --
    // the car stays dead.
    TEST_ASSERT_FALSE(gate.update(true, 900, false));
    for (int i = 0; i < 50; ++i) {
        TEST_ASSERT_FALSE(gate.update(true, 0, false));
    }

    TEST_ASSERT_FALSE(gate.update(false, 0, false)); // switch OFF: toggle half 1
    TEST_ASSERT_FALSE(gate.update(true, 500, false)); // ON, stick displaced: neutral rule holds
    TEST_ASSERT_TRUE(gate.update(true, 0, false));    // ON + neutral: re-armed
}

// Boot with the switch already ON (FSM boots Safe => forceDisarm true on
// every pre-proof tick): equivalent to a latched episode -- the first arm
// demands a deliberate OFF->ON toggle. Strictly more conservative than the
// pre-2026-08-20 gate, which armed at proof completion + neutral.
void test_armgate_boot_with_switch_on_requires_toggle() {
    ArmGate gate;
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_FALSE(gate.update(true, 0, true)); // boot Safe, switch on
    }
    // Link proof completes (forceDisarm drops): still latched, never arms.
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_FALSE(gate.update(true, 0, false));
    }
    gate.update(false, 0, false);                  // deliberate OFF
    TEST_ASSERT_TRUE(gate.update(true, 0, false)); // ON + neutral: first arm
}

// Boot with the switch OFF (the normal case) is unaffected: the latch never
// sets, and the first switch-on + neutral arms exactly as before.
void test_armgate_boot_with_switch_off_first_arm_unchanged() {
    ArmGate gate;
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_FALSE(gate.update(false, 0, true)); // boot Safe, switch off
    }
    TEST_ASSERT_FALSE(gate.update(false, 0, false)); // proof done, still off
    TEST_ASSERT_TRUE(gate.update(true, 0, false));   // switch on + neutral: arms
}

// The latch is level-driven: an OFF->ON toggle completed entirely inside the
// outage does not count -- the ON tick, still Safe, re-latches. The arming
// ON must be observed on a proven link (fail-closed direction).
void test_armgate_toggle_during_outage_does_not_count() {
    ArmGate gate;
    gate.update(true, 0, false);          // armed
    gate.update(true, 0, true);           // episode with switch on: latched
    gate.update(false, 0, true);          // OFF during the outage (clears)...
    TEST_ASSERT_FALSE(gate.update(true, 0, true)); // ...ON still Safe: re-latches
    // Recovery: switch on + neutral, still dead.
    TEST_ASSERT_FALSE(gate.update(true, 0, false));
    // Toggle on the proven link works.
    gate.update(false, 0, false);
    TEST_ASSERT_TRUE(gate.update(true, 0, false));
}

// An episode with the switch OFF does not latch: flipping the switch on
// after recovery is already a fresh, deliberate arm action.
void test_armgate_episode_with_switch_off_does_not_latch() {
    ArmGate gate;
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_FALSE(gate.update(false, 0, true)); // outage, switch off
    }
    TEST_ASSERT_TRUE(gate.update(true, 0, false)); // recovery, then switch on: arms
}

// The latch is reusable: after a full re-arm, a second episode latches again
// and demands its own toggle.
void test_armgate_repeated_episodes_each_require_a_toggle() {
    ArmGate gate;
    gate.update(true, 0, false); // armed

    gate.update(true, 0, true); // episode 1
    gate.update(false, 0, false);
    TEST_ASSERT_TRUE(gate.update(true, 0, false)); // toggled: re-armed

    TEST_ASSERT_FALSE(gate.update(true, 0, true));  // episode 2: latches again
    TEST_ASSERT_FALSE(gate.update(true, 0, false)); // recovery alone: dead
    gate.update(false, 0, false);
    TEST_ASSERT_TRUE(gate.update(true, 0, false)); // toggle 2: re-armed
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
    RUN_TEST(test_normalization_clamps_plausible_out_of_range_raw);
    RUN_TEST(test_implausible_raw_decodes_as_absent_not_full_deflection);
    RUN_TEST(test_plausibility_band_boundaries_are_inclusive);
    RUN_TEST(test_implausible_raw_forces_switch_off_and_does_not_hold);
    RUN_TEST(test_all_zero_payload_decodes_fully_safe);
    RUN_TEST(test_invert_flags_flip_analog_sign);
    RUN_TEST(test_switch_hysteresis_on_off_and_hold_in_band);
    RUN_TEST(test_first_decode_seeds_levels_and_fires_no_edges);
    RUN_TEST(test_exactly_one_edge_per_transition);
    RUN_TEST(test_custom_channel_remap);
    RUN_TEST(test_invalid_index_means_control_absent);
    RUN_TEST(test_pan_tilt_decode_and_invert);
    RUN_TEST(test_boost_overtake_held_switches);
    RUN_TEST(test_drive_mode_tri_state_positions);
    RUN_TEST(test_drive_mode_boundaries_are_exclusive);
    RUN_TEST(test_absent_mode_and_boost_channels_degrade_safely);
    RUN_TEST(test_config_valid_rejects_bad_values);
    RUN_TEST(test_armgate_blocks_arm_into_full_throttle);
    RUN_TEST(test_armgate_switch_on_with_neutral_arms_same_tick);
    RUN_TEST(test_armgate_disarms_on_switch_off_and_requires_neutral_again);
    RUN_TEST(test_armgate_force_disarm_polarity);
    RUN_TEST(test_armgate_failsafe_episode_latches_until_switch_toggle);
    RUN_TEST(test_armgate_boot_with_switch_on_requires_toggle);
    RUN_TEST(test_armgate_boot_with_switch_off_first_arm_unchanged);
    RUN_TEST(test_armgate_toggle_during_outage_does_not_count);
    RUN_TEST(test_armgate_episode_with_switch_off_does_not_latch);
    RUN_TEST(test_armgate_repeated_episodes_each_require_a_toggle);
    RUN_TEST(test_armgate_neutral_window_boundary);
    return UNITY_END();
}
