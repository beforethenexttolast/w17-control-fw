#include <unity.h>

#include "failsafe/FailsafeStateMachine.hpp"
#include "failsafe/GimbalDecay.hpp"

using failsafe::Config;
using failsafe::FailsafeStateMachine;
using failsafe::GimbalDecay;
using failsafe::GimbalDecayConfig;
using failsafe::State;

void setUp() {}
void tearDown() {}

void test_boots_safe_before_any_frame_ever_seen() {
    FailsafeStateMachine fsm;
    const State result = fsm.update(/*nowMs=*/10, /*frameArrivedThisTick=*/false, /*rxFailsafeFlag=*/false);
    TEST_ASSERT_EQUAL(State::Safe, result);
}

// Regression for review finding A1 (docs/ROADMAP.md): the old API inferred
// link health from `nowMs - lastFrameMs` with lastFrameMs booting as 0, so the
// machine went Active ~150-500ms after boot with ZERO frames received and the
// car slammed steering to full lock. With no frame ever arriving, the machine
// must report Safe at every point in time, forever.
void test_never_goes_active_when_no_frame_ever_arrives() {
    FailsafeStateMachine fsm;
    const uint32_t times[] = {0, 100, 155, 499, 500, 651, 10000, 4000000000u};
    for (uint32_t t : times) {
        TEST_ASSERT_EQUAL(State::Safe, fsm.update(t, false, false));
    }
}

void test_default_link_timeout_matches_spec() {
    // Spec-pinning test: CLAUDE.md section 2.4 says "start at 500 ms".
    const Config config;
    TEST_ASSERT_EQUAL_UINT32(500, config.linkTimeoutMs);
}

void test_climbs_to_active_after_rearm_window_with_real_frames() {
    FailsafeStateMachine fsm; // rearmConfirmMs = 150 by default

    TEST_ASSERT_EQUAL(State::Safe, fsm.update(0, true, false));    // first frame ever: window opens
    TEST_ASSERT_EQUAL(State::Safe, fsm.update(100, true, false));  // still within window
    TEST_ASSERT_EQUAL(State::Active, fsm.update(150, true, false)); // exactly rearmConfirmMs elapsed
}

void test_timeout_exceeded_drops_immediately_to_safe() {
    Config config;
    FailsafeStateMachine fsm(config);

    // Climb to Active with real frames; last frame at t = rearmConfirmMs.
    fsm.update(0, true, false);
    TEST_ASSERT_EQUAL(State::Active, fsm.update(config.rearmConfirmMs, true, false));

    // One tick before the timeout: still Active.
    const uint32_t lastFrameAt = config.rearmConfirmMs;
    TEST_ASSERT_EQUAL(State::Active, fsm.update(lastFrameAt + config.linkTimeoutMs - 1, false, false));
    // At the timeout boundary: immediate drop, no grace period on the way in.
    TEST_ASSERT_EQUAL(State::Safe, fsm.update(lastFrameAt + config.linkTimeoutMs, false, false));
}

void test_rx_failsafe_flag_drops_immediately_even_with_fresh_frames() {
    Config config;
    FailsafeStateMachine fsm(config);

    fsm.update(0, true, false);
    TEST_ASSERT_EQUAL(State::Active, fsm.update(config.rearmConfirmMs, true, false));

    // Frames still arriving, but the RX itself signals failsafe: flag wins.
    TEST_ASSERT_EQUAL(State::Safe, fsm.update(config.rearmConfirmMs + 1, true, true));
}

void test_latches_safe_despite_a_single_good_tick() {
    Config config;
    FailsafeStateMachine fsm(config);

    // Climb to Active (last frame at t=150), then time out.
    fsm.update(0, true, false);
    fsm.update(config.rearmConfirmMs, true, false);
    TEST_ASSERT_EQUAL(State::Safe, fsm.update(1000, false, false)); // 1000-150 >= 500

    // A single subsequent tick with a fresh frame must NOT flip straight back
    // to Active -- the re-arm window must still elapse.
    TEST_ASSERT_EQUAL(State::Safe, fsm.update(1001, true, false));
}

void test_rearm_window_chatter_resets_confirmation() {
    Config config; // rearmConfirmMs = 150
    FailsafeStateMachine fsm(config);

    // Establish a link, go Active, then lose it.
    fsm.update(0, true, false);
    fsm.update(150, true, false);   // Active, last frame at t=150
    fsm.update(1000, false, false); // 850ms without a frame -> Safe
    TEST_ASSERT_EQUAL(State::Safe, fsm.state());

    TEST_ASSERT_EQUAL(State::Safe, fsm.update(1000, true, false)); // frame: window opens at t=1000
    TEST_ASSERT_EQUAL(State::Safe, fsm.update(1100, true, false)); // 100ms into the window

    // A single bad tick (failsafe flag) mid-window resets the confirmation.
    TEST_ASSERT_EQUAL(State::Safe, fsm.update(1120, true, true));

    // Window restarts rather than accumulating: 50ms since re-open is not enough...
    TEST_ASSERT_EQUAL(State::Safe, fsm.update(1170, true, false)); // re-opens at t=1170
    TEST_ASSERT_EQUAL(State::Safe, fsm.update(1220, true, false)); // only 50ms since re-open
    // ...but a full uninterrupted window re-arms.
    TEST_ASSERT_EQUAL(State::Active, fsm.update(1320, true, false)); // 150ms since re-open
}

// ---------------------------------------------------------------------------
// GimbalDecay -- vision decision 11 (2026-08-16): on link-loss failsafe the
// gimbal axes DECAY TO CENTER instead of holding the last look direction, and
// recovery releases without a snap. All ticks below are the real 20 ms
// control cadence unless a test is exactly about irregular time.
// ---------------------------------------------------------------------------

void test_gimbal_decay_default_matches_spec_and_bounds() {
    const GimbalDecayConfig config;
    TEST_ASSERT_EQUAL_UINT16(2000, config.fullToCenterMs); // ~2 s full-deflection glide
    TEST_ASSERT_TRUE(config.valid());

    GimbalDecayConfig c;
    c.fullToCenterMs = GimbalDecayConfig::kMinFullToCenterMs - 1;
    TEST_ASSERT_FALSE(c.valid());
    c.fullToCenterMs = GimbalDecayConfig::kMinFullToCenterMs;
    TEST_ASSERT_TRUE(c.valid());
    c.fullToCenterMs = GimbalDecayConfig::kMaxFullToCenterMs;
    TEST_ASSERT_TRUE(c.valid());
    c.fullToCenterMs = GimbalDecayConfig::kMaxFullToCenterMs + 1;
    TEST_ASSERT_FALSE(c.valid());
}

void test_gimbal_passthrough_is_transparent_while_link_ok() {
    GimbalDecay decay;
    // Normal aiming is deliberately unshaped: arbitrary stick jumps pass
    // through exactly, both signs, full range.
    TEST_ASSERT_EQUAL_INT16(0, decay.update(0, false, 0));
    TEST_ASSERT_EQUAL_INT16(1000, decay.update(20, false, 1000));
    TEST_ASSERT_EQUAL_INT16(-1000, decay.update(40, false, -1000));
    TEST_ASSERT_EQUAL_INT16(37, decay.update(60, false, 37));
    TEST_ASSERT_FALSE(decay.slewing());
}

void test_gimbal_decay_full_deflection_reaches_center_in_configured_time() {
    GimbalDecay decay; // default: 1000 counts in 2000 ms = 10 counts / 20 ms tick
    uint32_t t = 0;
    decay.update(t, false, 1000); // passthrough at full deflection

    int16_t previous = 1000;
    for (int tick = 1; tick <= 100; ++tick) {
        t += 20;
        // The commanded value is ignored while engaged (main.cpp freezes
        // `controls` during an outage; the module must not depend on that).
        const int16_t out = decay.update(t, true, 1000);
        TEST_ASSERT_EQUAL_INT16(1000 - 10 * tick, out); // exact linear ramp
        TEST_ASSERT_TRUE(out < previous);               // monotonic toward center
        TEST_ASSERT_TRUE(out >= 0);                     // never overshoots center
        previous = out;
    }
    TEST_ASSERT_EQUAL_INT16(0, previous); // exactly centered at 2000 ms
}

void test_gimbal_decay_negative_deflection_rises_to_center() {
    GimbalDecay decay;
    uint32_t t = 0;
    decay.update(t, false, -600);

    int16_t out = -600;
    for (int tick = 1; tick <= 60; ++tick) { // 600 counts / 10 per tick
        t += 20;
        out = decay.update(t, true, -600);
        TEST_ASSERT_EQUAL_INT16(-600 + 10 * tick, out);
        TEST_ASSERT_TRUE(out <= 0); // approaches center from below, no overshoot
    }
    TEST_ASSERT_EQUAL_INT16(0, out);
}

void test_gimbal_decay_holds_center_once_reached() {
    GimbalDecay decay;
    uint32_t t = 0;
    decay.update(t, false, 200);
    for (int tick = 1; tick <= 20; ++tick) { // 200 counts -> centered
        t += 20;
        decay.update(t, true, 200);
    }
    for (int tick = 0; tick < 500; ++tick) { // long outage: stays centered
        t += 20;
        TEST_ASSERT_EQUAL_INT16(0, decay.update(t, true, 200));
    }
}

void test_gimbal_engage_starts_from_last_commanded_value() {
    GimbalDecay decay;
    decay.update(0, false, 300);
    // First failsafe tick departs from the last look direction by exactly
    // one tick's travel -- no reset, no jump to center.
    TEST_ASSERT_EQUAL_INT16(290, decay.update(20, true, 300));
    TEST_ASSERT_TRUE(decay.slewing());
}

void test_gimbal_release_slews_to_live_command_then_passes_through() {
    GimbalDecay decay;
    uint32_t t = 0;
    decay.update(t, false, 1000);
    for (int tick = 1; tick <= 50; ++tick) { // decay 1000 -> 500 over 1 s
        t += 20;
        decay.update(t, true, 1000);
    }
    // Link recovers with the stick still held at +1000: ramp back, no snap.
    int16_t out = 500;
    for (int tick = 1; tick <= 50; ++tick) {
        t += 20;
        out = decay.update(t, false, 1000);
        TEST_ASSERT_EQUAL_INT16(500 + 10 * tick, out);
    }
    TEST_ASSERT_EQUAL_INT16(1000, out);
    TEST_ASSERT_FALSE(decay.slewing());
    // Released cleanly: passthrough is transparent again, big steps included.
    t += 20;
    TEST_ASSERT_EQUAL_INT16(-1000, decay.update(t, false, -1000));
}

void test_gimbal_release_completes_immediately_when_command_matches() {
    GimbalDecay decay;
    uint32_t t = 0;
    decay.update(t, false, 400);
    for (int tick = 1; tick <= 40; ++tick) { // full decay to center
        t += 20;
        decay.update(t, true, 400);
    }
    // Recovery with the stick at center: released on the spot...
    t += 20;
    TEST_ASSERT_EQUAL_INT16(0, decay.update(t, false, 0));
    TEST_ASSERT_FALSE(decay.slewing());
    // ...and the very next command passes through untouched.
    t += 20;
    TEST_ASSERT_EQUAL_INT16(700, decay.update(t, false, 700));
}

void test_gimbal_release_converges_on_a_moving_command() {
    GimbalDecay decay;
    uint32_t t = 0;
    decay.update(t, false, 800);
    for (int tick = 1; tick <= 30; ++tick) { // decay 800 -> 500
        t += 20;
        decay.update(t, true, 800);
    }
    // The stick is live again and moving: the ramp chases the LIVE value.
    t += 20;
    TEST_ASSERT_EQUAL_INT16(510, decay.update(t, false, 600));
    t += 20;
    TEST_ASSERT_EQUAL_INT16(520, decay.update(t, false, 640));
    // The command comes down to meet the ramp: convergence ends the slew.
    t += 20;
    TEST_ASSERT_EQUAL_INT16(525, decay.update(t, false, 525));
    TEST_ASSERT_FALSE(decay.slewing());
}

void test_gimbal_decay_rate_follows_config() {
    GimbalDecayConfig config;
    config.fullToCenterMs = 500; // 40 counts per 20 ms tick
    GimbalDecay decay(config);
    decay.update(0, false, 1000);
    TEST_ASSERT_EQUAL_INT16(960, decay.update(20, true, 1000));
    TEST_ASSERT_EQUAL_INT16(920, decay.update(40, true, 1000));
}

void test_gimbal_decay_subcount_remainder_carries_without_drift() {
    GimbalDecayConfig config;
    config.fullToCenterMs = 3000; // 20 ms tick = 6.67 counts: 6, 7, 7 repeating
    GimbalDecay decay(config);
    uint32_t t = 0;
    decay.update(t, false, 1000);

    t += 20;
    TEST_ASSERT_EQUAL_INT16(994, decay.update(t, true, 0)); // floor(20000/3000) = 6
    t += 20;
    TEST_ASSERT_EQUAL_INT16(987, decay.update(t, true, 0)); // remainder carries -> 7
    t += 20;
    TEST_ASSERT_EQUAL_INT16(980, decay.update(t, true, 0)); // -> 7, remainder back to 0

    // Exactness over the whole travel: 1000 counts in EXACTLY 150 ticks
    // (3000 ms), not 149 and not 151 -- no truncation drift at odd rates.
    int16_t out = 980;
    int tick = 3;
    while (out != 0) {
        t += 20;
        out = decay.update(t, true, 0);
        ++tick;
    }
    TEST_ASSERT_EQUAL_INT(150, tick);
}

void test_gimbal_decay_stall_tick_movement_is_bounded() {
    GimbalDecay decay; // default 2000 ms
    decay.update(0, false, 1000);
    decay.update(20, true, 1000); // -> 990
    // A 5 s scheduler gap must not become a servo jump: dt clamps at 100 ms,
    // so one tick moves at most 50 counts at the default rate (the decay
    // stretches in wall time instead -- the gentle failure direction).
    TEST_ASSERT_EQUAL_INT16(940, decay.update(5020, true, 1000));
}

void test_gimbal_boot_straight_into_failsafe_stays_centered() {
    // The failsafe FSM boots Safe and setup() centers the gimbal; if the
    // first tick is already failsafe, the module must hold center exactly.
    GimbalDecay decay;
    for (uint32_t t = 0; t <= 1000; t += 20) {
        TEST_ASSERT_EQUAL_INT16(0, decay.update(t, true, 0));
    }
}

void test_gimbal_setconfig_mid_slew_keeps_position_and_state() {
    GimbalDecay decay;
    decay.update(0, false, 1000);
    decay.update(20, true, 1000); // -> 990, slewing
    GimbalDecayConfig faster;
    faster.fullToCenterMs = 500; // console retune mid-decay (applySettings path)
    decay.setConfig(faster);
    TEST_ASSERT_TRUE(decay.slewing());
    // Continues from 990 at the new rate (40/tick); the carry reset means no
    // step can be minted from a remainder accumulated against the old rate.
    TEST_ASSERT_EQUAL_INT16(950, decay.update(40, true, 1000));
}

void test_gimbal_out_of_range_command_is_clamped() {
    GimbalDecay decay;
    // Same defensive-clamp contract as ServoOutput::setPosition.
    TEST_ASSERT_EQUAL_INT16(1000, decay.update(0, false, 30000));
    TEST_ASSERT_EQUAL_INT16(-1000, decay.update(20, false, -30000));
}

void test_gimbal_decay_survives_millis_rollover() {
    GimbalDecay decay;
    const uint32_t nearWrap = 0xFFFFFFF0u;
    decay.update(nearWrap, false, 500);
    // A 20 ms tick straddling the uint32 wrap is still a 20 ms tick.
    TEST_ASSERT_EQUAL_INT16(490, decay.update(nearWrap + 20u, true, 500));
}

// Regression pin (review finding F2): failsafe re-engaging WHILE the
// post-recovery slew is still converging skips the engage-time carry reset
// (update() only initializes on the not-slewing -> slewing edge). Traced
// correct -- the carry is always sub-count (< divider), so the worst it can
// add to the next tick is a fraction of one count's timing -- but the
// re-entry path deserves its own pin: continuation from the CURRENT
// position, per-tick travel never above the configured rate, straight decay
// to center. Two phases: the exact-rate default config, then a non-divisible
// rate so a nonzero carry is genuinely live across the re-engage edge.
void test_gimbal_reengage_mid_recovery_continues_decay_without_step() {
    // --- Phase A: default 2000 ms (exactly 10 counts per 20 ms tick). ---
    GimbalDecay decay;
    uint32_t t = 0;
    decay.update(t, false, 1000); // passthrough at full deflection
    for (int tick = 1; tick <= 50; ++tick) { // decay 1000 -> 500 over 1 s
        t += 20;
        decay.update(t, true, 1000);
    }
    // Link back, stick still at +1000: recovery slew climbs a few ticks...
    t += 20;
    TEST_ASSERT_EQUAL_INT16(510, decay.update(t, false, 1000));
    t += 20;
    TEST_ASSERT_EQUAL_INT16(520, decay.update(t, false, 1000));
    t += 20;
    TEST_ASSERT_EQUAL_INT16(530, decay.update(t, false, 1000));
    TEST_ASSERT_TRUE(decay.slewing()); // ...and is still mid-convergence.

    // ...when the link drops AGAIN. First re-engaged tick: continues from
    // 530, moves exactly one tick's rate toward center -- no restart, no
    // snap, no burst.
    t += 20;
    TEST_ASSERT_EQUAL_INT16(520, decay.update(t, true, 1000));
    int16_t prev = 520;
    for (int tick = 1; tick <= 52; ++tick) { // 520 counts / 10 per tick
        t += 20;
        const int16_t out = decay.update(t, true, 1000);
        TEST_ASSERT_EQUAL_INT16(prev - 10 > 0 ? prev - 10 : 0, out);
        prev = out;
    }
    TEST_ASSERT_EQUAL_INT16(0, prev);

    // --- Phase B: 3000 ms (6.67 counts/tick -> allowances 6,7,7), so the
    // sub-count carry is NONZERO at the re-engage edge -- the exact state
    // the skipped reset leaves behind. ---
    GimbalDecayConfig config;
    config.fullToCenterMs = 3000;
    GimbalDecay decay2(config);
    uint32_t t2 = 0;
    decay2.update(t2, false, 1000);
    int16_t out2 = 1000;
    for (int tick = 1; tick <= 74; ++tick) { // 24 cycles (480) + 6 + 7 = 493
        t2 += 20;
        out2 = decay2.update(t2, true, 1000);
    }
    TEST_ASSERT_EQUAL_INT16(507, out2); // carry now 1000 count*ms
    t2 += 20;
    TEST_ASSERT_EQUAL_INT16(514, decay2.update(t2, false, 1000)); // +7 (carry 0)
    t2 += 20;
    TEST_ASSERT_EQUAL_INT16(520, decay2.update(t2, false, 1000)); // +6 (carry 2000)
    t2 += 20;
    TEST_ASSERT_EQUAL_INT16(527, decay2.update(t2, false, 1000)); // +7 (carry 1000)
    TEST_ASSERT_TRUE(decay2.slewing());

    // Re-engage with carry 1000 live: next tick is (20000+1000)/3000 = 7 --
    // continuation from 527 within the rate ceiling, the carry stays
    // sub-count, and the decay runs monotonically home with every per-tick
    // delta in [1, 7].
    t2 += 20;
    TEST_ASSERT_EQUAL_INT16(520, decay2.update(t2, true, 1000));
    prev = 520;
    int guard = 0;
    while (prev != 0 && ++guard <= 120) { // 520 counts at >= 6/tick: < 90 ticks
        t2 += 20;
        const int16_t out = decay2.update(t2, true, 1000);
        TEST_ASSERT_TRUE(out < prev);            // monotonic toward center
        TEST_ASSERT_TRUE(prev - out <= 7);       // never above the rate ceiling
        TEST_ASSERT_TRUE(out >= 0);              // no overshoot
        prev = out;
    }
    TEST_ASSERT_EQUAL_INT16(0, prev);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_boots_safe_before_any_frame_ever_seen);
    RUN_TEST(test_never_goes_active_when_no_frame_ever_arrives);
    RUN_TEST(test_default_link_timeout_matches_spec);
    RUN_TEST(test_climbs_to_active_after_rearm_window_with_real_frames);
    RUN_TEST(test_timeout_exceeded_drops_immediately_to_safe);
    RUN_TEST(test_rx_failsafe_flag_drops_immediately_even_with_fresh_frames);
    RUN_TEST(test_latches_safe_despite_a_single_good_tick);
    RUN_TEST(test_rearm_window_chatter_resets_confirmation);
    RUN_TEST(test_gimbal_decay_default_matches_spec_and_bounds);
    RUN_TEST(test_gimbal_passthrough_is_transparent_while_link_ok);
    RUN_TEST(test_gimbal_decay_full_deflection_reaches_center_in_configured_time);
    RUN_TEST(test_gimbal_decay_negative_deflection_rises_to_center);
    RUN_TEST(test_gimbal_decay_holds_center_once_reached);
    RUN_TEST(test_gimbal_engage_starts_from_last_commanded_value);
    RUN_TEST(test_gimbal_release_slews_to_live_command_then_passes_through);
    RUN_TEST(test_gimbal_release_completes_immediately_when_command_matches);
    RUN_TEST(test_gimbal_release_converges_on_a_moving_command);
    RUN_TEST(test_gimbal_decay_rate_follows_config);
    RUN_TEST(test_gimbal_decay_subcount_remainder_carries_without_drift);
    RUN_TEST(test_gimbal_decay_stall_tick_movement_is_bounded);
    RUN_TEST(test_gimbal_boot_straight_into_failsafe_stays_centered);
    RUN_TEST(test_gimbal_setconfig_mid_slew_keeps_position_and_state);
    RUN_TEST(test_gimbal_out_of_range_command_is_clamped);
    RUN_TEST(test_gimbal_decay_survives_millis_rollover);
    RUN_TEST(test_gimbal_reengage_mid_recovery_continues_decay_without_step);
    return UNITY_END();
}
