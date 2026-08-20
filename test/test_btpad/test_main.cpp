#include <unity.h>

#include "btpad/BtPadConfig.hpp"
#include "btpad/PadDecoder.hpp"
#include "btpad/PadFrame.hpp"
#include "btpad/PadLinkMonitor.hpp"

// The BT head must reuse the EXISTING arbitration objects (design §3): these
// suites drive the real ArmGate / FailsafeStateMachine / shapeThrottle, not
// re-implementations.
#include "channels/ArmGate.hpp"
#include "failsafe/FailsafeStateMachine.hpp"
#include "gearbox/Gearbox.hpp"

#include "../mocks/FakePadSource.hpp"

using btpad::BtPadConfig;
using btpad::PadDecoder;
using btpad::PadFrame;
using btpad::PadLinkMonitor;
using channels::Controls;

void setUp() {}
void tearDown() {}

// --- helpers -----------------------------------------------------------------

static PadFrame frameStick(int16_t leftStickX) {
    PadFrame f;
    f.leftStickX = leftStickX;
    return f;
}

static PadFrame frameTriggers(int16_t r2, int16_t l2) {
    PadFrame f;
    f.throttle = r2;
    f.brake = l2;
    return f;
}

static PadFrame frameButtons(uint16_t buttons, uint16_t misc = 0) {
    PadFrame f;
    f.buttons = buttons;
    f.miscButtons = misc;
    return f;
}

static const uint16_t kL1R1 = btpad::kButtonL1 | btpad::kButtonR1;

// --- mapping: steering -------------------------------------------------------

void test_stick_normalization_anchors_through_decode() {
    PadDecoder dec; // default deadzone 40; anchors are outside it
    TEST_ASSERT_EQUAL_INT16(0, dec.decode(frameStick(0), 0).steering);
    TEST_ASSERT_EQUAL_INT16(1000, dec.decode(frameStick(btpad::kStickRawMax), 0).steering);
    TEST_ASSERT_EQUAL_INT16(-1000, dec.decode(frameStick(btpad::kStickRawMin), 0).steering);
    // Mid-travel: raw 256 -> normalized 500 -> deadzone-rescaled (500-40)*1000/960.
    TEST_ASSERT_EQUAL_INT16(479, dec.decode(frameStick(256), 0).steering);
}

void test_steering_deadzone_edges() {
    PadDecoder dec; // steerDeadzone = 40 (normalized units)
    // raw 20 -> normalized 39 (inside) -> 0; raw 21 -> 41 (outside) -> rescaled 1.
    TEST_ASSERT_EQUAL_INT16(0, dec.decode(frameStick(20), 0).steering);
    TEST_ASSERT_EQUAL_INT16(1, dec.decode(frameStick(21), 0).steering);
    // Symmetric on the negative side: raw -20 -> -39 -> 0; raw -22 -> -42 -> -2.
    TEST_ASSERT_EQUAL_INT16(0, dec.decode(frameStick(-20), 0).steering);
    TEST_ASSERT_EQUAL_INT16(-2, dec.decode(frameStick(-22), 0).steering);
    // Rescale means full deflection still reaches the endpoint (no lost range).
    TEST_ASSERT_EQUAL_INT16(1000, dec.decode(frameStick(511), 0).steering);
}

void test_steering_inversion_flips_sign() {
    BtPadConfig cfg;
    cfg.invertSteering = 1;
    PadDecoder dec(cfg);
    TEST_ASSERT_EQUAL_INT16(-1000, dec.decode(frameStick(btpad::kStickRawMax), 0).steering);
    TEST_ASSERT_EQUAL_INT16(1000, dec.decode(frameStick(btpad::kStickRawMin), 0).steering);
}

// --- mapping: triggers -------------------------------------------------------

void test_trigger_scaling_anchors() {
    PadDecoder dec;
    TEST_ASSERT_EQUAL_INT16(0, dec.decode(frameTriggers(0, 0), 0).throttle);
    TEST_ASSERT_EQUAL_INT16(1000, dec.decode(frameTriggers(1023, 0), 0).throttle);
    TEST_ASSERT_EQUAL_INT16(500, dec.decode(frameTriggers(512, 0), 0).throttle);
}

void test_net_throttle_is_r2_minus_l2() {
    PadDecoder dec;
    // Both fully pulled cancel to zero; brake alone is negative (brake, never
    // reverse -- the ESC runs forward/brake, design §3.4).
    TEST_ASSERT_EQUAL_INT16(0, dec.decode(frameTriggers(1023, 1023), 0).throttle);
    TEST_ASSERT_EQUAL_INT16(-1000, dec.decode(frameTriggers(0, 1023), 0).throttle);
    TEST_ASSERT_EQUAL_INT16(500, dec.decode(frameTriggers(1023, 512), 0).throttle);
}

void test_out_of_range_raw_values_clamp() {
    PadDecoder dec;
    TEST_ASSERT_EQUAL_INT16(1000, dec.decode(frameStick(600), 0).steering);
    TEST_ASSERT_EQUAL_INT16(-1000, dec.decode(frameStick(-600), 0).steering);
    TEST_ASSERT_EQUAL_INT16(1000, dec.decode(frameTriggers(2000, 0), 0).throttle);
    TEST_ASSERT_EQUAL_INT16(0, dec.decode(frameTriggers(0, -5), 0).throttle);
}

// --- arm ritual (design §3.2, OWNER-PENDING(BT-3) recommended variant (a)) ----

void test_arm_ritual_hold_duration_boundary() {
    PadDecoder dec; // armHoldMs = 1000
    TEST_ASSERT_FALSE(dec.decode(frameButtons(kL1R1), 100).armSwitch);
    TEST_ASSERT_FALSE(dec.decode(frameButtons(kL1R1), 1099).armSwitch); // 999 ms: not yet
    TEST_ASSERT_TRUE(dec.decode(frameButtons(kL1R1), 1100).armSwitch);  // 1000 ms: armed
    // Latch holds across frames with the buttons released.
    TEST_ASSERT_TRUE(dec.decode(frameButtons(0), 1200).armSwitch);
}

void test_arm_ritual_release_resets_the_timer() {
    PadDecoder dec;
    dec.decode(frameButtons(kL1R1), 0);
    // R1 slips at 500 ms: the pair is no longer simultaneously held.
    dec.decode(frameButtons(btpad::kButtonL1), 500);
    dec.decode(frameButtons(kL1R1), 600); // fresh hold starts here
    TEST_ASSERT_FALSE(dec.decode(frameButtons(kL1R1), 1599).armSwitch);
    TEST_ASSERT_TRUE(dec.decode(frameButtons(kL1R1), 1600).armSwitch);
}

void test_options_disarms_instantly() {
    PadDecoder dec;
    dec.decode(frameButtons(kL1R1), 0);
    TEST_ASSERT_TRUE(dec.decode(frameButtons(kL1R1), 1000).armSwitch);
    TEST_ASSERT_FALSE(dec.decode(frameButtons(0, btpad::kMiscOptions), 1050).armSwitch);
}

void test_options_wins_over_a_hold_completing_in_the_same_frame() {
    PadDecoder dec;
    dec.decode(frameButtons(kL1R1, btpad::kMiscOptions), 0);
    TEST_ASSERT_FALSE(dec.decode(frameButtons(kL1R1, btpad::kMiscOptions), 1000).armSwitch);
    TEST_ASSERT_FALSE(dec.decode(frameButtons(kL1R1, btpad::kMiscOptions), 5000).armSwitch);
}

void test_force_disarm_requires_release_before_rearm() {
    PadDecoder dec;
    dec.decode(frameButtons(kL1R1), 0);
    TEST_ASSERT_TRUE(dec.decode(frameButtons(kL1R1), 1000).armSwitch);

    // Failsafe episode / disconnect: wiring clears the ritual (strict BT-3).
    dec.forceDisarmRitual();

    // Grips frozen on L1+R1 through the outage: must NOT silently re-arm, no
    // matter how long they stay held.
    TEST_ASSERT_FALSE(dec.decode(frameButtons(kL1R1), 1100).armSwitch);
    TEST_ASSERT_FALSE(dec.decode(frameButtons(kL1R1), 60000).armSwitch);

    // A fresh, deliberate ritual: release, re-hold, full hold duration again.
    TEST_ASSERT_FALSE(dec.decode(frameButtons(0), 60100).armSwitch);
    dec.decode(frameButtons(kL1R1), 60200);
    TEST_ASSERT_FALSE(dec.decode(frameButtons(kL1R1), 61199).armSwitch);
    TEST_ASSERT_TRUE(dec.decode(frameButtons(kL1R1), 61200).armSwitch);
}

void test_options_tap_with_grips_held_also_requires_release() {
    PadDecoder dec;
    dec.decode(frameButtons(kL1R1), 0);
    TEST_ASSERT_TRUE(dec.decode(frameButtons(kL1R1), 1000).armSwitch);
    // OPTIONS tapped while both grips stay held...
    TEST_ASSERT_FALSE(dec.decode(frameButtons(kL1R1, btpad::kMiscOptions), 2000).armSwitch);
    // ...and released again, grips never leaving L1+R1: still disarmed, even
    // well past another hold period.
    TEST_ASSERT_FALSE(dec.decode(frameButtons(kL1R1), 2100).armSwitch);
    TEST_ASSERT_FALSE(dec.decode(frameButtons(kL1R1), 3200).armSwitch);
    // Fresh ritual after a real release.
    dec.decode(frameButtons(0), 3300);
    dec.decode(frameButtons(kL1R1), 3400);
    TEST_ASSERT_TRUE(dec.decode(frameButtons(kL1R1), 4400).armSwitch);
}

void test_ritual_with_real_arm_gate_keeps_neutral_precondition() {
    PadDecoder dec;
    channels::ArmGate gate;

    // L1+R1 held with the throttle trigger fully pulled the whole time.
    PadFrame f = frameButtons(kL1R1);
    f.throttle = 1023;

    Controls c = dec.decode(f, 0);
    TEST_ASSERT_FALSE(gate.update(c.armSwitch, c.throttle, /*forceDisarm=*/false));

    c = dec.decode(f, 1000); // ritual latches...
    TEST_ASSERT_TRUE(c.armSwitch);
    // ...but the UNCHANGED ArmGate still demands throttle seen at neutral:
    // no arm-into-full-throttle, exactly as on the CRSF path.
    TEST_ASSERT_FALSE(gate.update(c.armSwitch, c.throttle, false));

    f.throttle = 0; // trigger released: net throttle 0, inside the +/-60 window
    c = dec.decode(f, 1100);
    TEST_ASSERT_TRUE(gate.update(c.armSwitch, c.throttle, false));

    // Failsafe episode: gate force-disarms (same call as CRSF wiring), and the
    // BT wiring additionally clears the ritual (strict BT-3).
    TEST_ASSERT_FALSE(gate.update(c.armSwitch, c.throttle, /*forceDisarm=*/true));
    dec.forceDisarmRitual();
    c = dec.decode(f, 1200); // grips still on L1+R1: no silent re-arm
    TEST_ASSERT_FALSE(c.armSwitch);
    TEST_ASSERT_FALSE(gate.update(c.armSwitch, c.throttle, false));
}

// --- DRS toggle (OWNER-PENDING(BT-5)) -----------------------------------------

void test_square_press_edges_toggle_drs() {
    PadDecoder dec;
    TEST_ASSERT_FALSE(dec.decode(frameButtons(0), 0).drsSwitch);
    TEST_ASSERT_TRUE(dec.decode(frameButtons(btpad::kButtonSquare), 10).drsSwitch);
    // Holding is not a new press; releasing does not untoggle.
    TEST_ASSERT_TRUE(dec.decode(frameButtons(btpad::kButtonSquare), 20).drsSwitch);
    TEST_ASSERT_TRUE(dec.decode(frameButtons(0), 30).drsSwitch);
    // Next press toggles back.
    TEST_ASSERT_FALSE(dec.decode(frameButtons(btpad::kButtonSquare), 40).drsSwitch);
}

// --- demo-envelope pins (design §3.3) -----------------------------------------

void test_drive_mode_pinned_to_training() {
    PadDecoder dec;
    // OWNER-PENDING(BT-4): driveMode 0 in EVERY decode -- ERS (driveMode 2)
    // is structurally unreachable from the pad.
    TEST_ASSERT_EQUAL_UINT8(0, dec.decode(PadFrame{}, 0).driveMode);
    PadFrame mashed;
    mashed.buttons = 0xFFFF;
    mashed.miscButtons = 0xFFFF;
    mashed.leftStickX = 511;
    mashed.throttle = 1023;
    mashed.brake = 1023;
    TEST_ASSERT_EQUAL_UINT8(0, dec.decode(mashed, 100).driveMode);
}

void test_pan_tilt_fixed_center_regardless_of_right_stick() {
    PadDecoder dec;
    PadFrame f;
    f.rightStickX = 511; // OWNER-PENDING(BT-9): pad sticks are not CRSF ch9/10
    f.rightStickY = -512;
    const Controls c = dec.decode(f, 0);
    TEST_ASSERT_EQUAL_INT16(0, c.pan);
    TEST_ASSERT_EQUAL_INT16(0, c.tilt);
}

void test_no_gear_edges_and_no_ers_controls_ever() {
    PadDecoder dec;
    PadFrame mashed;
    mashed.buttons = 0xFFFF;
    mashed.miscButtons = 0xFFFF;
    for (uint32_t t = 0; t <= 2000; t += 100) {
        const Controls c = dec.decode(mashed, t);
        TEST_ASSERT_FALSE(c.gearUpEdge);
        TEST_ASSERT_FALSE(c.gearDownEdge);
        TEST_ASSERT_FALSE(c.boostHeld);
        TEST_ASSERT_FALSE(c.overtakeHeld);
    }
}

// --- envelope shaping: same shapeThrottle the training mode uses --------------

void test_btpad_gear_params_shape_endpoints() {
    // The BT tick shapes with GearParams{btpad.maxOutput, btpad.expoPercent}
    // through the SAME gearbox::shapeThrottle as training mode (design §3.3).
    const BtPadConfig cfg; // defaults 400 / 50, mirroring kTrainingGearParams
    const gearbox::GearParams params{cfg.maxOutput, cfg.expoPercent};
    TEST_ASSERT_EQUAL_INT16(0, gearbox::shapeThrottle(0, params));
    TEST_ASSERT_EQUAL_INT16(400, gearbox::shapeThrottle(1000, params)); // full stick = cap
    const gearbox::GearParams tighter{250, 0};
    TEST_ASSERT_EQUAL_INT16(250, gearbox::shapeThrottle(1000, tighter));
}

void test_brake_passes_through_the_shaper_unshaped() {
    // Full brake authority is retained regardless of the demo cap (brake is
    // the safety-positive direction; gearbox passes x <= 0 through unshaped).
    const gearbox::GearParams params{400, 50};
    TEST_ASSERT_EQUAL_INT16(-1000, gearbox::shapeThrottle(-1000, params));
    TEST_ASSERT_EQUAL_INT16(-500, gearbox::shapeThrottle(-500, params));
}

// --- BtPadConfig::valid() ------------------------------------------------------

void test_btpad_config_defaults_valid_and_bounds_enforced() {
    TEST_ASSERT_TRUE(BtPadConfig{}.valid());
    BtPadConfig c;
    c.maxOutput = 0;
    TEST_ASSERT_FALSE(c.valid());
    c = BtPadConfig{};
    c.maxOutput = 1001;
    TEST_ASSERT_FALSE(c.valid());
    c = BtPadConfig{};
    c.expoPercent = 101;
    TEST_ASSERT_FALSE(c.valid());
    c = BtPadConfig{};
    c.steerDeadzone = -1;
    TEST_ASSERT_FALSE(c.valid());
    c = BtPadConfig{};
    c.steerDeadzone = 301;
    TEST_ASSERT_FALSE(c.valid());
    c = BtPadConfig{};
    c.invertSteering = 2;
    TEST_ASSERT_FALSE(c.valid());
    c = BtPadConfig{};
    c.armHoldMs = 99;
    TEST_ASSERT_FALSE(c.valid());
    c = BtPadConfig{};
    c.armHoldMs = 10001;
    TEST_ASSERT_FALSE(c.valid());
    c = BtPadConfig{};
    c.pairWindowMs = 60001;
    TEST_ASSERT_FALSE(c.valid());
}

// --- pad-link monitor: the CRSF LQ==0 latch mirror ------------------------------

void test_link_monitor_never_connected_never_flags() {
    PadLinkMonitor m;
    for (int i = 0; i < 100; ++i) {
        m.update(/*newReport=*/false, /*connectedNow=*/false);
        TEST_ASSERT_FALSE(m.padSignalsFailsafe());
    }
}

void test_link_monitor_latches_on_disconnect_clears_on_reconnect_plus_report() {
    PadLinkMonitor m;
    m.update(true, true); // connected + first report
    TEST_ASSERT_FALSE(m.padSignalsFailsafe());

    m.update(false, false); // stack reports the pad gone -> latch
    TEST_ASSERT_TRUE(m.padSignalsFailsafe());
    m.update(false, false);
    TEST_ASSERT_TRUE(m.padSignalsFailsafe());

    // A bare "connected again" claim must NOT clear the latch...
    m.update(false, true);
    TEST_ASSERT_TRUE(m.padSignalsFailsafe());
    // ...only reconnect + the first consumed report does (mirror of the CRSF
    // rule that only fresh LQ>0 stats clear the LQ==0 latch).
    m.update(true, true);
    TEST_ASSERT_FALSE(m.padSignalsFailsafe());
}

void test_link_monitor_same_pass_disconnect_wins_over_report() {
    PadLinkMonitor m;
    m.update(true, true);
    // A drained report and a disconnect observed in the same pass: latch wins
    // (the conservative direction).
    m.update(true, false);
    TEST_ASSERT_TRUE(m.padSignalsFailsafe());
}

// --- seam contract -------------------------------------------------------------

void test_fake_source_poll_true_once_per_report() {
    test_mocks::FakePadSource src;
    btpad::PadFrame f;
    TEST_ASSERT_FALSE(src.poll(f));

    btpad::PadFrame scripted;
    scripted.throttle = 512;
    src.scriptReport(scripted);
    TEST_ASSERT_TRUE(src.poll(f));
    TEST_ASSERT_EQUAL_INT16(512, f.throttle);
    TEST_ASSERT_FALSE(src.poll(f)); // no second "new report" for the same data
}

// --- staleness -> failsafe: the same machine, same Config, same outcomes --------

// Mini-harness mirroring the BT head's wiring in src/main.cpp: poll -> monitor
// -> the UNCHANGED FailsafeStateMachine with its DEFAULT Config (500 ms
// timeout / 150 ms re-arm -- the exact CRSF values; failsafe timing stays
// non-tunable in both modes, design §3 row 1).
struct BtHeadHarness {
    test_mocks::FakePadSource source;
    PadLinkMonitor monitor;
    failsafe::FailsafeStateMachine fsm; // default Config, same as the CRSF path
    bool frameSinceTick = false;

    failsafe::State tick(uint32_t nowMs) {
        btpad::PadFrame f;
        const bool newReport = source.poll(f);
        monitor.update(newReport, source.connected());
        frameSinceTick |= newReport;
        const failsafe::State s =
            fsm.update(nowMs, frameSinceTick, monitor.padSignalsFailsafe());
        frameSinceTick = false;
        return s;
    }
};

void test_staleness_timeout_matches_crsf_outcomes() {
    BtHeadHarness h;
    h.source.setConnected(true);

    // Reports every 20 ms: Active exactly once 150 ms of continuous link is
    // confirmed (same climb as test_failsafe's re-arm window).
    for (uint32_t t = 0; t <= 140; t += 20) {
        h.source.scriptReport(PadFrame{});
        TEST_ASSERT_EQUAL(failsafe::State::Safe, h.tick(t));
    }
    h.source.scriptReport(PadFrame{});
    TEST_ASSERT_EQUAL(failsafe::State::Active, h.tick(160));

    // Reports stop at t=160 (pad still "connected": an RF hole the stack has
    // not noticed yet -- the staleness clock is the PRIMARY loss signal).
    TEST_ASSERT_EQUAL(failsafe::State::Active, h.tick(659)); // 499 ms stale
    TEST_ASSERT_EQUAL(failsafe::State::Safe, h.tick(660));   // 500 ms: Safe, no grace
}

void test_disconnect_flag_drops_active_immediately() {
    BtHeadHarness h;
    h.source.setConnected(true);
    for (uint32_t t = 0; t <= 160; t += 20) {
        h.source.scriptReport(PadFrame{});
        h.tick(t);
    }
    TEST_ASSERT_EQUAL(failsafe::State::Active, h.fsm.state());

    // Stack reports the disconnect 20 ms later: Safe on the very next tick,
    // long before the 500 ms staleness clock would fire (the secondary
    // trigger, same role as the CRSF LQ==0 latch).
    h.source.setConnected(false);
    TEST_ASSERT_EQUAL(failsafe::State::Safe, h.tick(180));
}

void test_recovery_needs_reconnect_report_and_continuous_good_window() {
    BtHeadHarness h;
    h.source.setConnected(true);
    for (uint32_t t = 0; t <= 160; t += 20) {
        h.source.scriptReport(PadFrame{});
        h.tick(t);
    }
    h.source.setConnected(false);
    TEST_ASSERT_EQUAL(failsafe::State::Safe, h.tick(180));

    // Reconnect claim alone: latch still set, Safe.
    h.source.setConnected(true);
    TEST_ASSERT_EQUAL(failsafe::State::Safe, h.tick(1000));

    // Reports resume at t=1020: latch clears, and Active only after the same
    // 150 ms continuous-good confirmation as a CRSF recovery.
    for (uint32_t t = 1020; t <= 1160; t += 20) {
        h.source.scriptReport(PadFrame{});
        TEST_ASSERT_EQUAL(failsafe::State::Safe, h.tick(t));
    }
    h.source.scriptReport(PadFrame{});
    TEST_ASSERT_EQUAL(failsafe::State::Active, h.tick(1170)); // 150 ms after 1020
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_stick_normalization_anchors_through_decode);
    RUN_TEST(test_steering_deadzone_edges);
    RUN_TEST(test_steering_inversion_flips_sign);
    RUN_TEST(test_trigger_scaling_anchors);
    RUN_TEST(test_net_throttle_is_r2_minus_l2);
    RUN_TEST(test_out_of_range_raw_values_clamp);
    RUN_TEST(test_arm_ritual_hold_duration_boundary);
    RUN_TEST(test_arm_ritual_release_resets_the_timer);
    RUN_TEST(test_options_disarms_instantly);
    RUN_TEST(test_options_wins_over_a_hold_completing_in_the_same_frame);
    RUN_TEST(test_force_disarm_requires_release_before_rearm);
    RUN_TEST(test_options_tap_with_grips_held_also_requires_release);
    RUN_TEST(test_ritual_with_real_arm_gate_keeps_neutral_precondition);
    RUN_TEST(test_square_press_edges_toggle_drs);
    RUN_TEST(test_drive_mode_pinned_to_training);
    RUN_TEST(test_pan_tilt_fixed_center_regardless_of_right_stick);
    RUN_TEST(test_no_gear_edges_and_no_ers_controls_ever);
    RUN_TEST(test_btpad_gear_params_shape_endpoints);
    RUN_TEST(test_brake_passes_through_the_shaper_unshaped);
    RUN_TEST(test_btpad_config_defaults_valid_and_bounds_enforced);
    RUN_TEST(test_link_monitor_never_connected_never_flags);
    RUN_TEST(test_link_monitor_latches_on_disconnect_clears_on_reconnect_plus_report);
    RUN_TEST(test_link_monitor_same_pass_disconnect_wins_over_report);
    RUN_TEST(test_fake_source_poll_true_once_per_report);
    RUN_TEST(test_staleness_timeout_matches_crsf_outcomes);
    RUN_TEST(test_disconnect_flag_drops_active_immediately);
    RUN_TEST(test_recovery_needs_reconnect_report_and_continuous_good_window);
    return UNITY_END();
}
