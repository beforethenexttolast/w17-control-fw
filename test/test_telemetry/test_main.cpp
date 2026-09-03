#include <unity.h>

#include "telemetry/BatteryMonitor.hpp"
#include "telemetry/WheelSpeed.hpp"

#include "../mocks/FakeVoltageSensor.hpp"
#include "../mocks/FakeWheelPulseSensor.hpp"

using telemetry::BatteryConfig;
using telemetry::BatteryMonitor;
using telemetry::WheelSpeed;
using telemetry::WheelSpeedConfig;
using test_mocks::FakeVoltageSensor;
using test_mocks::FakeWheelPulseSensor;

void setUp() {}
void tearDown() {}

// --- BatteryMonitor: conversion ---

void test_battery_divider_conversion() {
    FakeVoltageSensor sensor;
    BatteryMonitor monitor(sensor);

    // 8.4V through 27k/10k reads ~2270mV at the pin (CLAUDE.md section 7);
    // (2270 * 37 * 1000 + 5000) / 10000 = 8399 (rounded combined division).
    sensor.pinMillivolts = 2270;
    monitor.sample(0);
    TEST_ASSERT_EQUAL_UINT16(8399, monitor.batteryMv());
}

void test_battery_calibration_trim() {
    FakeVoltageSensor sensor;
    BatteryConfig config;
    config.calibrationPpt = 1010; // multimeter said we read 1% low
    BatteryMonitor monitor(sensor, config);

    sensor.pinMillivolts = 2270;
    monitor.sample(0);
    // (2270 * 37 * 1010 + 5000) / 10000 = 8483
    TEST_ASSERT_EQUAL_UINT16(8483, monitor.batteryMv());
}

void test_battery_ema_seeds_from_first_sample() {
    FakeVoltageSensor sensor;
    BatteryMonitor monitor(sensor);

    TEST_ASSERT_EQUAL_UINT16(0, monitor.batteryMv()); // nothing sampled yet

    sensor.pinMillivolts = 2000;
    monitor.sample(0);
    // Exact immediately -- no climb-from-zero boot artifact.
    TEST_ASSERT_EQUAL_UINT16(7400, monitor.batteryMv()); // 2000*37/10
    TEST_ASSERT_FALSE(monitor.lowVoltageWarning());      // and no spurious warning
}

void test_battery_ema_converges_exactly_upward() {
    FakeVoltageSensor sensor;
    BatteryMonitor monitor(sensor);

    sensor.pinMillivolts = 1800; // 6660 battery mV
    monitor.sample(0);

    sensor.pinMillivolts = 2200; // 8140 battery mV
    for (uint32_t t = 100; t <= 10000; t += 100) {
        monitor.sample(t);
    }
    // The scaled-accumulator EMA must reach the input exactly (the naive
    // formulation stalls up to 2^shift-1 counts below a rising input).
    TEST_ASSERT_EQUAL_UINT16(8140, monitor.batteryMv());
}

// --- BatteryMonitor: low-voltage warning ---

void test_battery_warning_requires_sustained_low() {
    FakeVoltageSensor sensor;
    BatteryMonitor monitor(sensor);

    sensor.pinMillivolts = 2200; // healthy 8140mV
    monitor.sample(0);

    sensor.pinMillivolts = 1700; // 6290mV, well under warnMv=7000
    uint32_t t = 100;
    for (; t <= 1000; t += 100) {
        monitor.sample(t);
    }
    TEST_ASSERT_FALSE(monitor.lowVoltageWarning()); // not sustained long enough yet

    for (; t <= 8000; t += 100) {
        monitor.sample(t);
    }
    TEST_ASSERT_TRUE(monitor.lowVoltageWarning()); // sustained >3s below: latch
}

void test_battery_brief_sag_does_not_warn() {
    FakeVoltageSensor sensor;
    BatteryMonitor monitor(sensor);

    sensor.pinMillivolts = 2200; // healthy
    monitor.sample(0);

    // A ~1.5s full-throttle sag dips the smoothed value under the threshold
    // for less than warnDelayMs, then recovers.
    sensor.pinMillivolts = 1700;
    uint32_t t = 100;
    for (; t <= 1500; t += 100) {
        monitor.sample(t);
    }
    sensor.pinMillivolts = 2200;
    for (; t <= 6000; t += 100) {
        monitor.sample(t);
    }
    TEST_ASSERT_FALSE(monitor.lowVoltageWarning());
}

void test_battery_warning_clears_only_above_hysteresis() {
    FakeVoltageSensor sensor;
    BatteryMonitor monitor(sensor);

    // Latch the warning.
    sensor.pinMillivolts = 1700;
    uint32_t t = 0;
    for (; t <= 5000; t += 100) {
        monitor.sample(t);
    }
    TEST_ASSERT_TRUE(monitor.lowVoltageWarning());

    // Recover into the hysteresis band (7000..7400): must stay warned.
    sensor.pinMillivolts = 1950; // 7215mV
    for (; t <= 12000; t += 100) {
        monitor.sample(t);
    }
    TEST_ASSERT_TRUE(monitor.lowVoltageWarning());

    // Recover above warnMv + 400: clears.
    sensor.pinMillivolts = 2200; // 8140mV
    for (; t <= 20000; t += 100) {
        monitor.sample(t);
    }
    TEST_ASSERT_FALSE(monitor.lowVoltageWarning());
}

void test_battery_config_valid_rejects_bad_values() {
    TEST_ASSERT_TRUE(BatteryConfig{}.valid());

    BatteryConfig zeroDen;
    zeroDen.dividerDen = 0;
    TEST_ASSERT_FALSE(zeroDen.valid());

    BatteryConfig wildTrim;
    wildTrim.calibrationPpt = 1200;
    TEST_ASSERT_FALSE(wildTrim.valid());

    BatteryConfig zeroShift;
    zeroShift.emaShift = 0;
    TEST_ASSERT_FALSE(zeroShift.valid());

    BatteryConfig noHysteresis;
    noHysteresis.warnClearHysteresisMv = 0;
    TEST_ASSERT_FALSE(noHysteresis.valid());
}

// --- BatteryMonitor: sensor plausibility (fault-injection-3 / OD-10(a)) ---
//
// Pin millivolts convert as pinMv * 37 / 10 (default divider, no trim), so the
// numbers below are chosen against the two DIVIDER FAULTS, not against battery
// states: an open UPPER leg floats GPIO34 to ~0, an open LOWER leg pulls it
// toward Vbat through the 27k and saturates the ADC at ~3.1-3.3 V.

void test_battery_open_upper_leg_reads_implausible_not_flat() {
    FakeVoltageSensor sensor;
    BatteryMonitor monitor(sensor);

    sensor.pinMillivolts = 0; // divider's upper leg open: pin at ground
    monitor.sample(0);

    TEST_ASSERT_TRUE(monitor.sensorImplausible());
    TEST_ASSERT_EQUAL_UINT16(0, monitor.batteryMv()); // "no reading", not 0 V
    TEST_ASSERT_FALSE(monitor.lowVoltageWarning());   // and NOT a low-battery lie
}

void test_battery_open_lower_leg_saturation_is_implausible_not_full() {
    FakeVoltageSensor sensor;
    BatteryMonitor monitor(sensor);

    // ADC saturated near the top of the 11 dB range: 3200 * 3.7 = ~11841 mV.
    // This is the case that made the finding gift-blocking -- untreated it
    // reports a 100 %-full pack forever and the warning never fires.
    sensor.pinMillivolts = 3200;
    monitor.sample(0);

    TEST_ASSERT_TRUE(monitor.sensorImplausible());
    TEST_ASSERT_EQUAL_UINT16(0, monitor.batteryMv());
    TEST_ASSERT_FALSE(monitor.lowVoltageWarning());
}

void test_battery_plausibility_band_boundaries() {
    FakeVoltageSensor sensor;
    BatteryMonitor monitor(sensor);

    sensor.pinMillivolts = 1081; // -> 4000 mV exactly: AT the floor, plausible
    monitor.sample(0);
    TEST_ASSERT_FALSE(monitor.sensorImplausible());
    TEST_ASSERT_EQUAL_UINT16(4000, monitor.batteryMv());

    sensor.pinMillivolts = 1080; // -> 3996 mV: below the floor
    monitor.sample(100);
    TEST_ASSERT_TRUE(monitor.sensorImplausible());

    sensor.pinMillivolts = 2432; // -> 8998 mV: just under the ceiling
    monitor.sample(200);
    TEST_ASSERT_FALSE(monitor.sensorImplausible());
    TEST_ASSERT_EQUAL_UINT16(8998, monitor.batteryMv());

    sensor.pinMillivolts = 2433; // -> 9002 mV: over the ceiling
    monitor.sample(300);
    TEST_ASSERT_TRUE(monitor.sensorImplausible());
}

void test_battery_implausible_sample_never_enters_the_ema() {
    FakeVoltageSensor sensor;
    BatteryMonitor monitor(sensor);

    sensor.pinMillivolts = 2270; // 8399 mV, healthy
    monitor.sample(0);
    TEST_ASSERT_EQUAL_UINT16(8399, monitor.batteryMv());

    sensor.pinMillivolts = 0; // divider breaks
    monitor.sample(100);
    monitor.sample(200);
    monitor.sample(300);
    TEST_ASSERT_TRUE(monitor.sensorImplausible());
    TEST_ASSERT_EQUAL_UINT16(0, monitor.batteryMv());

    // Recovery re-seeds EXACTLY. An EMA that had eaten the fault (or simply
    // kept running) would report a blend here (~8274 for one step from 8399
    // toward 7400 at emaShift 3), which would be a number nobody measured.
    sensor.pinMillivolts = 2000; // 7400 mV
    monitor.sample(400);
    TEST_ASSERT_FALSE(monitor.sensorImplausible());
    TEST_ASSERT_EQUAL_UINT16(7400, monitor.batteryMv());
}

void test_battery_implausible_clears_a_latched_warning_and_does_not_relatch() {
    FakeVoltageSensor sensor;
    BatteryMonitor monitor(sensor);

    sensor.pinMillivolts = 1865; // 6901 mV: genuinely low, and plausible
    monitor.sample(0);
    monitor.sample(3000); // sustained past warnDelayMs
    TEST_ASSERT_TRUE(monitor.lowVoltageWarning());

    sensor.pinMillivolts = 0; // now the sensor breaks
    monitor.sample(3100);
    TEST_ASSERT_TRUE(monitor.sensorImplausible());
    TEST_ASSERT_FALSE(monitor.lowVoltageWarning()); // no latched lie survives
    TEST_ASSERT_EQUAL_UINT16(0, monitor.batteryMv());

    // Sensor comes back on a still-low pack: the warning must re-qualify from
    // scratch (delay restarts) rather than reappearing instantly from a latch.
    sensor.pinMillivolts = 1865;
    monitor.sample(3200);
    TEST_ASSERT_FALSE(monitor.lowVoltageWarning());
    monitor.sample(6200); // warnDelayMs after the first plausible low sample
    TEST_ASSERT_TRUE(monitor.lowVoltageWarning());
}

void test_battery_6900mv_still_warns_above_the_floor() {
    // The floor must not swallow a genuinely low pack: 6900 mV is 3.45 V/cell,
    // deep in warning territory and far above implausibleBelowMv.
    FakeVoltageSensor sensor;
    BatteryMonitor monitor(sensor);

    sensor.pinMillivolts = 1865; // 6901 mV
    monitor.sample(0);
    TEST_ASSERT_FALSE(monitor.sensorImplausible());
    monitor.sample(3000);
    TEST_ASSERT_TRUE(monitor.lowVoltageWarning());
}

void test_battery_config_valid_rejects_bad_plausibility_bounds() {
    BatteryConfig floorAboveWarn;
    floorAboveWarn.implausibleBelowMv = 7000; // == warnMv: could never warn
    TEST_ASSERT_FALSE(floorAboveWarn.valid());

    BatteryConfig ceilingInsideHysteresis;
    ceilingInsideHysteresis.implausibleAboveMv = 7400; // == warnMv + hysteresis
    TEST_ASSERT_FALSE(ceilingInsideHysteresis.valid()); // could never clear
}

// --- WheelSpeed ---

void test_wheel_first_update_seeds_without_spike() {
    FakeWheelPulseSensor sensor;
    WheelSpeed wheel(sensor);

    // Wheel spun before construction: counter already nonzero.
    sensor.snapshot = {500, 20000};
    wheel.update(0);
    TEST_ASSERT_EQUAL_UINT16(0, wheel.rpm());
}

void test_wheel_rpm_from_pulse_period() {
    FakeWheelPulseSensor sensor;
    WheelSpeed wheel(sensor);
    wheel.update(0); // seed

    sensor.snapshot = {1, 20000}; // 20ms between edges = 3000 rpm at 1 magnet
    wheel.update(100);
    TEST_ASSERT_EQUAL_UINT16(3000, wheel.rpm());
    // 3000 rpm * 201mm / 60 = 10050 mm/s
    TEST_ASSERT_EQUAL_UINT16(10050, wheel.speedMmPerSec());
}

void test_wheel_first_ever_edge_has_no_period() {
    FakeWheelPulseSensor sensor;
    WheelSpeed wheel(sensor);
    wheel.update(0);

    sensor.snapshot = {1, 0}; // one edge seen, no period yet
    wheel.update(100);
    TEST_ASSERT_EQUAL_UINT16(0, wheel.rpm());
}

void test_wheel_implausible_period_is_clamped() {
    FakeWheelPulseSensor sensor;
    WheelSpeed wheel(sensor);
    wheel.update(0);

    sensor.snapshot = {1, 6000}; // 10000 rpm claimed: EMI, not motion
    wheel.update(100);
    TEST_ASSERT_EQUAL_UINT16(5000, wheel.rpm()); // maxPlausibleRpm
}

void test_wheel_zero_speed_after_timeout() {
    FakeWheelPulseSensor sensor;
    WheelSpeed wheel(sensor);
    wheel.update(0);

    sensor.snapshot = {1, 20000};
    wheel.update(100); // 3000 rpm

    wheel.update(100 + 1500); // zeroSpeedTimeoutMs with no new pulse
    TEST_ASSERT_EQUAL_UINT16(0, wheel.rpm());
}

void test_wheel_decays_gracefully_while_silent() {
    FakeWheelPulseSensor sensor;
    WheelSpeed wheel(sensor);
    wheel.update(0);

    sensor.snapshot = {1, 100000}; // 100ms period = 600 rpm
    wheel.update(100);
    TEST_ASSERT_EQUAL_UINT16(600, wheel.rpm());

    // No new pulses: the report is capped by "if it were still that fast,
    // we'd have seen a pulse by now" -- 60000/elapsedMs.
    wheel.update(100 + 200);
    TEST_ASSERT_EQUAL_UINT16(300, wheel.rpm());
    wheel.update(100 + 500);
    TEST_ASSERT_EQUAL_UINT16(120, wheel.rpm());
    wheel.update(100 + 1000);
    TEST_ASSERT_EQUAL_UINT16(60, wheel.rpm());
    wheel.update(100 + 1500); // timeout truncates the tail
    TEST_ASSERT_EQUAL_UINT16(0, wheel.rpm());
}

void test_wheel_new_pulse_after_decay_reports_true_period() {
    FakeWheelPulseSensor sensor;
    WheelSpeed wheel(sensor);
    wheel.update(0);

    sensor.snapshot = {1, 100000}; // 600 rpm
    wheel.update(100);
    wheel.update(400); // decayed to 200 by the ceiling rule

    // The late pulse arrives with its true (long) period: 300ms = 200 rpm.
    sensor.snapshot = {2, 300000};
    wheel.update(400);
    TEST_ASSERT_EQUAL_UINT16(200, wheel.rpm());
}

void test_wheel_config_valid_rejects_bad_values() {
    TEST_ASSERT_TRUE(WheelSpeedConfig{}.valid());

    WheelSpeedConfig zeroMagnets;
    zeroMagnets.magnetsPerRev = 0;
    TEST_ASSERT_FALSE(zeroMagnets.valid());

    WheelSpeedConfig zeroCircumference;
    zeroCircumference.wheelCircumferenceMm = 0;
    TEST_ASSERT_FALSE(zeroCircumference.valid());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_battery_divider_conversion);
    RUN_TEST(test_battery_calibration_trim);
    RUN_TEST(test_battery_ema_seeds_from_first_sample);
    RUN_TEST(test_battery_ema_converges_exactly_upward);
    RUN_TEST(test_battery_warning_requires_sustained_low);
    RUN_TEST(test_battery_brief_sag_does_not_warn);
    RUN_TEST(test_battery_warning_clears_only_above_hysteresis);
    RUN_TEST(test_battery_config_valid_rejects_bad_values);
    RUN_TEST(test_battery_open_upper_leg_reads_implausible_not_flat);
    RUN_TEST(test_battery_open_lower_leg_saturation_is_implausible_not_full);
    RUN_TEST(test_battery_plausibility_band_boundaries);
    RUN_TEST(test_battery_implausible_sample_never_enters_the_ema);
    RUN_TEST(test_battery_implausible_clears_a_latched_warning_and_does_not_relatch);
    RUN_TEST(test_battery_6900mv_still_warns_above_the_floor);
    RUN_TEST(test_battery_config_valid_rejects_bad_plausibility_bounds);
    RUN_TEST(test_wheel_first_update_seeds_without_spike);
    RUN_TEST(test_wheel_rpm_from_pulse_period);
    RUN_TEST(test_wheel_first_ever_edge_has_no_period);
    RUN_TEST(test_wheel_implausible_period_is_clamped);
    RUN_TEST(test_wheel_zero_speed_after_timeout);
    RUN_TEST(test_wheel_decays_gracefully_while_silent);
    RUN_TEST(test_wheel_new_pulse_after_decay_reports_true_period);
    RUN_TEST(test_wheel_config_valid_rejects_bad_values);
    return UNITY_END();
}
