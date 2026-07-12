#include <unity.h>

#include <cstring>

#include "console/Console.hpp"
#include "console/ConsoleRunner.hpp"
#include "settings/Settings.hpp"

// Module setConfig behaviors are part of this feature, tested here.
#include "gearbox/Gearbox.hpp"
#include "outputs/ServoOutput.hpp"
#include "telemetry/BatteryMonitor.hpp"

#include "../mocks/FakeClock.hpp"
#include "../mocks/FakeVoltageSensor.hpp"
#include "../mocks/MockCharIO.hpp"
#include "../mocks/MockPwmOutput.hpp"
#include "../mocks/MockSettingsStore.hpp"

using console::Console;
using console::ConsoleRunner;
using console::Result;
using settings::kDefaults;
using settings::Settings;

void setUp() {}
void tearDown() {}

// Field-wise Settings compare (memcmp is unreliable across struct padding).
// Test-support equality only; range rules stay in Settings::valid(). Compares
// EVERY field of the whole Settings object so "rejected leaves settings
// unchanged" proofs cannot miss a mutated field. All GearboxConfig::kMaxGears
// gear slots are compared, not just the active numGears, so an edit to an
// inactive slot is caught too.
static bool settingsEqual(const Settings& a, const Settings& b) {
    if (a.steering.minMicros != b.steering.minMicros ||
        a.steering.maxMicros != b.steering.maxMicros ||
        a.steering.centerMicros != b.steering.centerMicros ||
        a.steering.trimMicros != b.steering.trimMicros) {
        return false;
    }
    if (a.gearbox.numGears != b.gearbox.numGears ||
        a.gearbox.initialGear != b.gearbox.initialGear) {
        return false;
    }
    for (size_t i = 0; i < gearbox::GearboxConfig::kMaxGears; ++i) {
        if (a.gearbox.gears[i].maxOutput != b.gearbox.gears[i].maxOutput ||
            a.gearbox.gears[i].expoPercent != b.gearbox.gears[i].expoPercent) {
            return false;
        }
    }
    if (a.battery.dividerNum != b.battery.dividerNum ||
        a.battery.dividerDen != b.battery.dividerDen ||
        a.battery.calibrationPpt != b.battery.calibrationPpt ||
        a.battery.emaShift != b.battery.emaShift ||
        a.battery.warnMv != b.battery.warnMv ||
        a.battery.warnDelayMs != b.battery.warnDelayMs ||
        a.battery.warnClearHysteresisMv != b.battery.warnClearHysteresisMv) {
        return false;
    }
    return true;
}

// --- Console::handleLine ---

void test_get_and_set_disarmed() {
    Console c;
    Settings s = kDefaults;

    Result g = c.handleLine("get steer.trim", s, /*armed=*/false);
    TEST_ASSERT_TRUE(std::strstr(g.text, "steer.trim=0") != nullptr);

    Result r = c.handleLine("set steer.trim 40", s, false);
    TEST_ASSERT_TRUE(r.settingsChanged);
    TEST_ASSERT_EQUAL_INT16(40, s.steering.trimMicros);
}

void test_set_refused_while_armed() {
    Console c;
    Settings s = kDefaults;
    Result r = c.handleLine("set steer.trim 40", s, /*armed=*/true);
    TEST_ASSERT_FALSE(r.settingsChanged);
    TEST_ASSERT_EQUAL_INT16(0, s.steering.trimMicros); // unchanged
    TEST_ASSERT_TRUE(std::strstr(r.text, "refused") != nullptr);
}

void test_get_allowed_while_armed() {
    Console c;
    Settings s = kDefaults;
    Result r = c.handleLine("get batt.ppt", s, /*armed=*/true);
    TEST_ASSERT_TRUE(std::strstr(r.text, "batt.ppt=1000") != nullptr);
}

void test_out_of_range_rejected() {
    Console c;
    Settings s = kDefaults;
    // trim that pushes center(1500)+trim past max(2500) -> ServoConfig::valid() fails.
    Result r = c.handleLine("set steer.trim 2000", s, false);
    TEST_ASSERT_FALSE(r.settingsChanged);
    TEST_ASSERT_EQUAL_INT16(0, s.steering.trimMicros);
    TEST_ASSERT_TRUE(std::strstr(r.text, "rejected") != nullptr);
}

void test_gear_monotonicity_enforced_at_set() {
    Console c;
    Settings s = kDefaults; // gears: 400,600,800,1000
    // Set gear2 (idx1) below gear1 -> non-decreasing rule in GearboxConfig::valid() fails.
    Result r = c.handleLine("set gear.2.max 300", s, false);
    TEST_ASSERT_FALSE(r.settingsChanged);
    TEST_ASSERT_TRUE(std::strstr(r.text, "rejected") != nullptr);

    // A valid monotone edit is accepted.
    Result ok = c.handleLine("set gear.1.max 350", s, false);
    TEST_ASSERT_TRUE(ok.settingsChanged);
    TEST_ASSERT_EQUAL_INT16(350, s.gearbox.gears[0].maxOutput);
}

void test_gear_index_out_of_range() {
    Console c;
    Settings s = kDefaults; // numGears=4
    Result r = c.handleLine("set gear.9.max 500", s, false);
    TEST_ASSERT_FALSE(r.settingsChanged);
    TEST_ASSERT_TRUE(std::strstr(r.text, "range") != nullptr);
}

void test_unknown_command_and_key() {
    Console c;
    Settings s = kDefaults;
    TEST_ASSERT_TRUE(std::strstr(c.handleLine("wibble", s, false).text, "unknown") != nullptr);
    TEST_ASSERT_TRUE(std::strstr(c.handleLine("get no.such.key", s, false).text, "unknown") != nullptr);
}

void test_reset_reverts_ram_only() {
    Console c;
    Settings s = kDefaults;
    s.steering.trimMicros = 55;
    Result r = c.handleLine("reset", s, false);
    TEST_ASSERT_TRUE(r.settingsChanged);
    TEST_ASSERT_FALSE(r.saveRequested); // RAM only
    TEST_ASSERT_EQUAL_INT16(0, s.steering.trimMicros);
}

// --- steer.min / steer.max endpoint commands (CF-2) ---

void test_steer_min_valid_accepted() {
    Console c;
    Settings s = kDefaults;
    Result r = c.handleLine("set steer.min 900", s, false);
    TEST_ASSERT_TRUE(r.settingsChanged);
    TEST_ASSERT_EQUAL_UINT16(900, s.steering.minMicros);
    TEST_ASSERT_TRUE(std::strstr(r.text, "ok") != nullptr);
}

void test_steer_max_valid_accepted() {
    Console c;
    Settings s = kDefaults;
    Result r = c.handleLine("set steer.max 2100", s, false);
    TEST_ASSERT_TRUE(r.settingsChanged);
    TEST_ASSERT_EQUAL_UINT16(2100, s.steering.maxMicros);
    TEST_ASSERT_TRUE(std::strstr(r.text, "ok") != nullptr);
}

void test_steer_min_equal_or_above_max_rejected() {
    Console c;
    Settings s = kDefaults;
    TEST_ASSERT_TRUE(c.handleLine("set steer.max 2000", s, false).settingsChanged);

    // min == max
    Result eq = c.handleLine("set steer.min 2000", s, false);
    TEST_ASSERT_FALSE(eq.settingsChanged);
    TEST_ASSERT_TRUE(std::strstr(eq.text, "rejected") != nullptr);
    TEST_ASSERT_EQUAL_UINT16(500, s.steering.minMicros);

    // min > max
    Result gt = c.handleLine("set steer.min 2100", s, false);
    TEST_ASSERT_FALSE(gt.settingsChanged);
    TEST_ASSERT_TRUE(std::strstr(gt.text, "rejected") != nullptr);
    TEST_ASSERT_EQUAL_UINT16(500, s.steering.minMicros);
}

void test_steer_max_below_min_rejected() {
    Console c;
    Settings s = kDefaults;
    TEST_ASSERT_TRUE(c.handleLine("set steer.min 1000", s, false).settingsChanged);

    Result r = c.handleLine("set steer.max 900", s, false);
    TEST_ASSERT_FALSE(r.settingsChanged);
    TEST_ASSERT_TRUE(std::strstr(r.text, "rejected") != nullptr);
    TEST_ASSERT_EQUAL_UINT16(2500, s.steering.maxMicros);
}

void test_steer_min_excluding_center_rejected() {
    Console c;
    Settings s = kDefaults; // center 1500
    Result above = c.handleLine("set steer.min 1600", s, false);
    TEST_ASSERT_FALSE(above.settingsChanged);
    TEST_ASSERT_TRUE(std::strstr(above.text, "rejected") != nullptr);

    // center must be STRICTLY inside: min == center also rejected.
    Result eq = c.handleLine("set steer.min 1500", s, false);
    TEST_ASSERT_FALSE(eq.settingsChanged);
    TEST_ASSERT_EQUAL_UINT16(500, s.steering.minMicros);
}

void test_steer_max_excluding_center_rejected() {
    Console c;
    Settings s = kDefaults; // center 1500
    Result below = c.handleLine("set steer.max 1400", s, false);
    TEST_ASSERT_FALSE(below.settingsChanged);
    TEST_ASSERT_TRUE(std::strstr(below.text, "rejected") != nullptr);

    Result eq = c.handleLine("set steer.max 1500", s, false);
    TEST_ASSERT_FALSE(eq.settingsChanged);
    TEST_ASSERT_EQUAL_UINT16(2500, s.steering.maxMicros);
}

void test_steer_endpoint_invalidating_trimmed_center_rejected() {
    Console c;
    Settings s = kDefaults;
    TEST_ASSERT_TRUE(c.handleLine("set steer.trim 400", s, false).settingsChanged);

    // center (1500) would still fit under 1800, but center+trim (1900) would not.
    Result r = c.handleLine("set steer.max 1800", s, false);
    TEST_ASSERT_FALSE(r.settingsChanged);
    TEST_ASSERT_TRUE(std::strstr(r.text, "rejected") != nullptr);
    TEST_ASSERT_EQUAL_UINT16(2500, s.steering.maxMicros);
}

void test_steer_endpoints_outside_absolute_bounds_rejected() {
    Console c;
    Settings s = kDefaults;
    TEST_ASSERT_FALSE(c.handleLine("set steer.min 400", s, false).settingsChanged);
    TEST_ASSERT_FALSE(c.handleLine("set steer.max 2600", s, false).settingsChanged);
    TEST_ASSERT_FALSE(c.handleLine("set steer.min -5", s, false).settingsChanged);
    // 66036 would wrap to 500 through a bare uint16 cast -- must be rejected,
    // not silently accepted as a "valid" value.
    TEST_ASSERT_FALSE(c.handleLine("set steer.min 66036", s, false).settingsChanged);
    TEST_ASSERT_EQUAL_UINT16(500, s.steering.minMicros);
    TEST_ASSERT_EQUAL_UINT16(2500, s.steering.maxMicros);
}

void test_steer_endpoints_at_absolute_bounds_accepted() {
    Console c;
    Settings s = kDefaults;
    // Move both endpoints inward first so setting them back to the absolute
    // floor/ceil is a genuine change, then prove the exact bounds are accepted.
    TEST_ASSERT_TRUE(c.handleLine("set steer.min 1000", s, false).settingsChanged);
    TEST_ASSERT_TRUE(c.handleLine("set steer.max 2000", s, false).settingsChanged);

    Result lo = c.handleLine("set steer.min 500", s, false); // == kServoPulseFloorMicros
    TEST_ASSERT_TRUE(lo.settingsChanged);
    TEST_ASSERT_EQUAL_UINT16(500, s.steering.minMicros);

    Result hi = c.handleLine("set steer.max 2500", s, false); // == kServoPulseCeilMicros
    TEST_ASSERT_TRUE(hi.settingsChanged);
    TEST_ASSERT_EQUAL_UINT16(2500, s.steering.maxMicros);
}

void test_steer_endpoints_one_past_absolute_bounds_rejected() {
    Console c;
    Settings s = kDefaults;
    const Settings before = s;

    Result lo = c.handleLine("set steer.min 499", s, false); // one below the floor
    TEST_ASSERT_FALSE(lo.settingsChanged);
    TEST_ASSERT_TRUE(std::strstr(lo.text, "rejected") != nullptr);
    TEST_ASSERT_TRUE(settingsEqual(before, s));

    Result hi = c.handleLine("set steer.max 2501", s, false); // one above the ceil
    TEST_ASSERT_FALSE(hi.settingsChanged);
    TEST_ASSERT_TRUE(std::strstr(hi.text, "rejected") != nullptr);
    TEST_ASSERT_TRUE(settingsEqual(before, s));
}

void test_rejected_endpoint_leaves_whole_settings_unchanged() {
    Console c;
    Settings s = kDefaults;
    // Non-default but valid starting state.
    TEST_ASSERT_TRUE(c.handleLine("set steer.min 1000", s, false).settingsChanged);
    TEST_ASSERT_TRUE(c.handleLine("set steer.trim 50", s, false).settingsChanged);
    TEST_ASSERT_TRUE(c.handleLine("set gear.1.max 350", s, false).settingsChanged);
    const Settings before = s;

    Result r = c.handleLine("set steer.max 1200", s, false); // excludes center -> rejected
    TEST_ASSERT_FALSE(r.settingsChanged);
    TEST_ASSERT_TRUE(settingsEqual(before, s));
}

void test_reset_restores_default_endpoints() {
    Console c;
    Settings s = kDefaults;
    TEST_ASSERT_TRUE(c.handleLine("set steer.min 1100", s, false).settingsChanged);
    TEST_ASSERT_TRUE(c.handleLine("set steer.max 1900", s, false).settingsChanged);

    Result r = c.handleLine("reset", s, false);
    TEST_ASSERT_TRUE(r.settingsChanged);
    TEST_ASSERT_EQUAL_UINT16(kDefaults.steering.minMicros, s.steering.minMicros);
    TEST_ASSERT_EQUAL_UINT16(kDefaults.steering.maxMicros, s.steering.maxMicros);
}

void test_get_and_status_report_endpoints() {
    Console c;
    Settings s = kDefaults;
    TEST_ASSERT_TRUE(std::strstr(c.handleLine("get steer.min", s, false).text,
                                 "steer.min=500") != nullptr);
    TEST_ASSERT_TRUE(std::strstr(c.handleLine("get steer.max", s, false).text,
                                 "steer.max=2500") != nullptr);

    TEST_ASSERT_TRUE(c.handleLine("set steer.min 1100", s, false).settingsChanged);
    TEST_ASSERT_TRUE(c.handleLine("set steer.max 1900", s, false).settingsChanged);
    Result st = c.handleLine("status", s, false);
    TEST_ASSERT_TRUE(std::strstr(st.text, "[1100..1900]") != nullptr);
    TEST_ASSERT_TRUE(std::strstr(st.text, "steer.center=1500") != nullptr);
}

void test_center_trim_commands_still_correct_with_narrowed_endpoints() {
    Console c;
    Settings s = kDefaults;
    TEST_ASSERT_TRUE(c.handleLine("set steer.min 1000", s, false).settingsChanged);
    TEST_ASSERT_TRUE(c.handleLine("set steer.max 2000", s, false).settingsChanged);

    // Center moves within the narrowed window.
    TEST_ASSERT_TRUE(c.handleLine("set steer.center 1450", s, false).settingsChanged);
    TEST_ASSERT_EQUAL_UINT16(1450, s.steering.centerMicros);

    // Trim that fit the old 2500 endpoint is now rejected against 2000.
    Result r = c.handleLine("set steer.trim 600", s, false); // 1450+600=2050 > 2000
    TEST_ASSERT_FALSE(r.settingsChanged);
    TEST_ASSERT_TRUE(std::strstr(r.text, "rejected") != nullptr);
    TEST_ASSERT_EQUAL_INT16(0, s.steering.trimMicros);

    TEST_ASSERT_TRUE(c.handleLine("set steer.trim 100", s, false).settingsChanged);
    TEST_ASSERT_TRUE(std::strstr(c.handleLine("get steer.trim", s, false).text,
                                 "steer.trim=100") != nullptr);
}

void test_runner_endpoints_survive_save_and_load() {
    test_mocks::MockCharIO io;
    test_mocks::MockSettingsStore store;
    ConsoleRunner runner(io, store);
    runner.loadAtBoot();

    io.feed("set steer.min 1100\nset steer.max 1900\nsave\n");
    TEST_ASSERT_TRUE(runner.poll(/*armed=*/false));
    TEST_ASSERT_EQUAL_UINT32(1, store.saveCount);

    // The persisted blob carries the endpoints.
    Settings back;
    TEST_ASSERT_TRUE(settings::deserialize(store.stored, store.storedLen, back));
    TEST_ASSERT_EQUAL_UINT16(1100, back.steering.minMicros);
    TEST_ASSERT_EQUAL_UINT16(1900, back.steering.maxMicros);

    // A fresh boot from the same store restores them.
    test_mocks::MockCharIO io2;
    ConsoleRunner runner2(io2, store);
    runner2.loadAtBoot();
    TEST_ASSERT_EQUAL_UINT16(1100, runner2.settings().steering.minMicros);
    TEST_ASSERT_EQUAL_UINT16(1900, runner2.settings().steering.maxMicros);
}

// --- ConsoleRunner (with mocks) ---

void test_runner_set_then_save_persists() {
    test_mocks::MockCharIO io;
    test_mocks::MockSettingsStore store;
    ConsoleRunner runner(io, store);
    runner.loadAtBoot(); // empty store -> defaults

    io.feed("set steer.trim 30\nsave\n");
    const bool changed = runner.poll(/*armed=*/false);
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_EQUAL_UINT16(30, runner.settings().steering.trimMicros);
    TEST_ASSERT_EQUAL_UINT32(1, store.saveCount);

    // The persisted blob round-trips to trim=30.
    Settings back;
    TEST_ASSERT_TRUE(settings::deserialize(store.stored, store.storedLen, back));
    TEST_ASSERT_EQUAL_INT16(30, back.steering.trimMicros);
}

void test_runner_armed_blocks_and_does_not_persist() {
    test_mocks::MockCharIO io;
    test_mocks::MockSettingsStore store;
    ConsoleRunner runner(io, store);
    runner.loadAtBoot();

    io.feed("set steer.trim 30\n");
    const bool changed = runner.poll(/*armed=*/true);
    TEST_ASSERT_FALSE(changed);
    TEST_ASSERT_TRUE(io.outputContains("refused"));
    TEST_ASSERT_EQUAL_INT16(0, runner.settings().steering.trimMicros);
}

void test_runner_overlong_line_discarded() {
    test_mocks::MockCharIO io;
    test_mocks::MockSettingsStore store;
    ConsoleRunner runner(io, store);
    char longline[console::kMaxLine + 20];
    std::memset(longline, 'x', sizeof(longline) - 2);
    longline[sizeof(longline) - 2] = '\n';
    longline[sizeof(longline) - 1] = '\0';
    io.feed(longline);
    runner.poll(false);
    TEST_ASSERT_TRUE(io.outputContains("too long"));
}

void test_runner_boot_loads_saved() {
    test_mocks::MockSettingsStore store;
    Settings saved = kDefaults;
    saved.battery.calibrationPpt = 1050;
    uint8_t blob[settings::kBlobLen];
    store.setStored(blob, settings::serialize(saved, blob));

    test_mocks::MockCharIO io;
    ConsoleRunner runner(io, store);
    runner.loadAtBoot();
    TEST_ASSERT_EQUAL_UINT16(1050, runner.settings().battery.calibrationPpt);
    TEST_ASSERT_TRUE(io.outputContains("loaded"));
}

// --- Module setConfig() ---

void test_servo_setconfig_applies() {
    test_mocks::MockPwmOutput pwm;
    outputs::ServoOutput servo(pwm);
    outputs::ServoConfig cfg;
    cfg.trimMicros = 100;
    servo.setConfig(cfg);
    servo.setPosition(0); // center + trim
    TEST_ASSERT_EQUAL_UINT16(1600, pwm.lastMicroseconds);
}

void test_gearbox_setconfig_clamps_current_not_reset() {
    gearbox::Gearbox box; // 4 gears, start gear 0
    box.shiftUp();
    box.shiftUp();
    box.shiftUp(); // now gear 3 (top)
    TEST_ASSERT_EQUAL_UINT8(3, box.currentGear());

    gearbox::GearboxConfig cfg; // still valid; shrink to 2 gears
    cfg.numGears = 2;
    box.setConfig(cfg);
    // Clamped to numGears-1 = 1, NOT reset to initialGear (0).
    TEST_ASSERT_EQUAL_UINT8(1, box.currentGear());
}

void test_battery_setconfig_changes_calibration() {
    test_mocks::FakeVoltageSensor sensor;
    telemetry::BatteryMonitor mon(sensor);
    sensor.pinMillivolts = 2000;
    mon.sample(0);
    const uint16_t before = mon.batteryMv();

    telemetry::BatteryConfig cfg;
    cfg.calibrationPpt = 1100; // +10%
    mon.setConfig(cfg);
    sensor.pinMillivolts = 2000;
    mon.sample(100);
    TEST_ASSERT_TRUE(mon.batteryMv() > before);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_get_and_set_disarmed);
    RUN_TEST(test_set_refused_while_armed);
    RUN_TEST(test_get_allowed_while_armed);
    RUN_TEST(test_out_of_range_rejected);
    RUN_TEST(test_gear_monotonicity_enforced_at_set);
    RUN_TEST(test_gear_index_out_of_range);
    RUN_TEST(test_unknown_command_and_key);
    RUN_TEST(test_reset_reverts_ram_only);
    RUN_TEST(test_steer_min_valid_accepted);
    RUN_TEST(test_steer_max_valid_accepted);
    RUN_TEST(test_steer_min_equal_or_above_max_rejected);
    RUN_TEST(test_steer_max_below_min_rejected);
    RUN_TEST(test_steer_min_excluding_center_rejected);
    RUN_TEST(test_steer_max_excluding_center_rejected);
    RUN_TEST(test_steer_endpoint_invalidating_trimmed_center_rejected);
    RUN_TEST(test_steer_endpoints_outside_absolute_bounds_rejected);
    RUN_TEST(test_steer_endpoints_at_absolute_bounds_accepted);
    RUN_TEST(test_steer_endpoints_one_past_absolute_bounds_rejected);
    RUN_TEST(test_rejected_endpoint_leaves_whole_settings_unchanged);
    RUN_TEST(test_reset_restores_default_endpoints);
    RUN_TEST(test_get_and_status_report_endpoints);
    RUN_TEST(test_center_trim_commands_still_correct_with_narrowed_endpoints);
    RUN_TEST(test_runner_endpoints_survive_save_and_load);
    RUN_TEST(test_runner_set_then_save_persists);
    RUN_TEST(test_runner_armed_blocks_and_does_not_persist);
    RUN_TEST(test_runner_overlong_line_discarded);
    RUN_TEST(test_runner_boot_loads_saved);
    RUN_TEST(test_servo_setconfig_applies);
    RUN_TEST(test_gearbox_setconfig_clamps_current_not_reset);
    RUN_TEST(test_battery_setconfig_changes_calibration);
    return UNITY_END();
}
