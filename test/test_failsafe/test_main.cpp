#include <unity.h>

#include "failsafe/FailsafeStateMachine.hpp"

using failsafe::Config;
using failsafe::FailsafeStateMachine;
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
    return UNITY_END();
}
