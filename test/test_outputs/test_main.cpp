#include <unity.h>

#include "outputs/DrsOutput.hpp"
#include "outputs/EscOutput.hpp"
#include "outputs/ServoOutput.hpp"

#include "../mocks/FakeClock.hpp"
#include "../mocks/MockPwmOutput.hpp"

using outputs::DrsConfig;
using outputs::DrsOutput;
using outputs::EscConfig;
using outputs::EscOutput;
using outputs::ServoConfig;
using outputs::ServoOutput;
using test_mocks::FakeClock;
using test_mocks::MockPwmOutput;

void setUp() {}
void tearDown() {}

void test_servo_center_position_default_config() {
    MockPwmOutput pwm;
    ServoOutput servo(pwm);

    servo.setPosition(0);

    TEST_ASSERT_EQUAL_UINT16(1500, pwm.lastMicroseconds);
}

void test_servo_endpoint_positions() {
    MockPwmOutput pwm;
    ServoOutput servo(pwm);

    servo.setPosition(-1000);
    TEST_ASSERT_EQUAL_UINT16(500, pwm.lastMicroseconds);

    servo.setPosition(1000);
    TEST_ASSERT_EQUAL_UINT16(2500, pwm.lastMicroseconds);
}

void test_servo_trim_shifts_center() {
    MockPwmOutput pwm;
    ServoConfig config;
    config.trimMicros = 50;
    ServoOutput servo(pwm, config);

    servo.setPosition(0);

    TEST_ASSERT_EQUAL_UINT16(1550, pwm.lastMicroseconds);
}

void test_servo_clamps_out_of_range_input() {
    MockPwmOutput pwm;
    ServoOutput servo(pwm);

    servo.setPosition(5000);
    TEST_ASSERT_EQUAL_UINT16(2500, pwm.lastMicroseconds);

    servo.setPosition(-5000);
    TEST_ASSERT_EQUAL_UINT16(500, pwm.lastMicroseconds);
}

void test_esc_holds_neutral_before_boot_arm_elapses() {
    MockPwmOutput pwm;
    FakeClock clock;
    clock.setNowMs(0);
    EscOutput esc(pwm, clock); // bootArmHoldMs = 2000 by default

    esc.setThrottle(0); // first command starts the hold (mirrors setup())

    clock.setNowMs(1999);
    esc.setThrottle(1000); // full throttle requested, but still pre-arm

    TEST_ASSERT_FALSE(esc.isArmed());
    TEST_ASSERT_EQUAL_UINT16(1500, pwm.lastMicroseconds);
}

void test_esc_passes_through_after_boot_arm_elapses() {
    MockPwmOutput pwm;
    FakeClock clock;
    clock.setNowMs(0);
    EscOutput esc(pwm, clock);

    esc.setThrottle(0); // hold starts at t=0

    clock.setNowMs(2500); // well past bootArmHoldMs
    esc.setThrottle(1000);

    TEST_ASSERT_TRUE(esc.isArmed());
    TEST_ASSERT_EQUAL_UINT16(2000, pwm.lastMicroseconds);
}

void test_esc_neutral_requested_pre_arm_is_still_neutral() {
    MockPwmOutput pwm;
    FakeClock clock;
    clock.setNowMs(0);
    EscOutput esc(pwm, clock);

    clock.setNowMs(500);
    esc.setThrottle(0);

    TEST_ASSERT_EQUAL_UINT16(1500, pwm.lastMicroseconds);
}

void test_esc_boundary_tick_exactly_at_boot_arm_hold_is_armed() {
    MockPwmOutput pwm;
    FakeClock clock;
    clock.setNowMs(1000);
    EscConfig config;
    config.bootArmHoldMs = 2000;
    EscOutput esc(pwm, clock, config);

    esc.setThrottle(0); // hold starts at t=1000

    // Exactly bootArmHoldMs elapsed since the first command: inclusive
    // boundary, must be armed.
    clock.setNowMs(1000 + 2000);
    esc.setThrottle(1000);

    TEST_ASSERT_TRUE(esc.isArmed());
    TEST_ASSERT_EQUAL_UINT16(2000, pwm.lastMicroseconds);
}

// Regression for review finding A5 (docs/ROADMAP.md): the hold used to be
// anchored to construction time. Globals are constructed during static init,
// so by the time setup() ran, part or all of the hold could already have
// elapsed with the ESC never having seen a single neutral pulse. The hold
// must start at the FIRST setThrottle() call, not at construction.
void test_esc_hold_starts_at_first_command_not_construction() {
    MockPwmOutput pwm;
    FakeClock clock;
    clock.setNowMs(0);
    EscOutput esc(pwm, clock); // constructed at t=0

    TEST_ASSERT_FALSE(esc.isArmed()); // never armed before any command

    clock.setNowMs(5000); // long after construction, first command only now
    esc.setThrottle(1000);
    TEST_ASSERT_FALSE(esc.isArmed());
    TEST_ASSERT_EQUAL_UINT16(1500, pwm.lastMicroseconds); // still held neutral

    clock.setNowMs(5000 + 2000); // full hold elapsed since the first command
    esc.setThrottle(1000);
    TEST_ASSERT_TRUE(esc.isArmed());
    TEST_ASSERT_EQUAL_UINT16(2000, pwm.lastMicroseconds);
}

void test_drs_open_and_closed_positions() {
    MockPwmOutput pwm;
    DrsOutput drs(pwm);

    drs.setOpen(true);
    TEST_ASSERT_EQUAL_UINT16(2000, pwm.lastMicroseconds);

    drs.setOpen(false);
    TEST_ASSERT_EQUAL_UINT16(1000, pwm.lastMicroseconds);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_servo_center_position_default_config);
    RUN_TEST(test_servo_endpoint_positions);
    RUN_TEST(test_servo_trim_shifts_center);
    RUN_TEST(test_servo_clamps_out_of_range_input);
    RUN_TEST(test_esc_holds_neutral_before_boot_arm_elapses);
    RUN_TEST(test_esc_passes_through_after_boot_arm_elapses);
    RUN_TEST(test_esc_neutral_requested_pre_arm_is_still_neutral);
    RUN_TEST(test_esc_boundary_tick_exactly_at_boot_arm_hold_is_armed);
    RUN_TEST(test_esc_hold_starts_at_first_command_not_construction);
    RUN_TEST(test_drs_open_and_closed_positions);
    return UNITY_END();
}
