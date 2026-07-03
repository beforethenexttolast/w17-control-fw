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
    RUN_TEST(test_runner_set_then_save_persists);
    RUN_TEST(test_runner_armed_blocks_and_does_not_persist);
    RUN_TEST(test_runner_overlong_line_discarded);
    RUN_TEST(test_runner_boot_loads_saved);
    RUN_TEST(test_servo_setconfig_applies);
    RUN_TEST(test_gearbox_setconfig_clamps_current_not_reset);
    RUN_TEST(test_battery_setconfig_changes_calibration);
    return UNITY_END();
}
