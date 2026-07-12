#include <unity.h>

#include <cstring>

#include "crsf/CrsfParser.hpp" // for the CRC cross-check
#include "settings/Settings.hpp"
#include "settings/SettingsLoader.hpp"

#include "../mocks/MockSettingsStore.hpp"

using settings::deserialize;
using settings::kBlobLen;
using settings::kDefaults;
using settings::loadOrDefault;
using settings::LoadStatus;
using settings::serialize;
using settings::Settings;

void setUp() {}
void tearDown() {}

// Field-wise "wholly default" check over every tunable field (a raw memcmp
// would be unreliable across struct padding). Used to prove the loader never
// leaves a partial/mixed object after a rejected blob.
static bool isWhollyDefault(const Settings& s) {
    if (s.steering.minMicros != kDefaults.steering.minMicros ||
        s.steering.maxMicros != kDefaults.steering.maxMicros ||
        s.steering.centerMicros != kDefaults.steering.centerMicros ||
        s.steering.trimMicros != kDefaults.steering.trimMicros ||
        s.battery.calibrationPpt != kDefaults.battery.calibrationPpt ||
        s.gearbox.numGears != kDefaults.gearbox.numGears) {
        return false;
    }
    for (uint8_t i = 0; i < kDefaults.gearbox.numGears; ++i) {
        if (s.gearbox.gears[i].maxOutput != kDefaults.gearbox.gears[i].maxOutput ||
            s.gearbox.gears[i].expoPercent != kDefaults.gearbox.gears[i].expoPercent) {
            return false;
        }
    }
    return true;
}

void test_defaults_are_valid() {
    TEST_ASSERT_TRUE(kDefaults.valid());
}

void test_roundtrip() {
    Settings s = kDefaults;
    s.steering.trimMicros = 42;
    s.battery.calibrationPpt = 1015;
    s.gearbox.gears[0].maxOutput = 450;

    uint8_t blob[kBlobLen];
    TEST_ASSERT_EQUAL_UINT32(kBlobLen, serialize(s, blob));

    Settings out;
    TEST_ASSERT_TRUE(deserialize(blob, kBlobLen, out));
    TEST_ASSERT_EQUAL_INT16(42, out.steering.trimMicros);
    TEST_ASSERT_EQUAL_UINT16(1015, out.battery.calibrationPpt);
    TEST_ASSERT_EQUAL_INT16(450, out.gearbox.gears[0].maxOutput);
}

void test_corrupt_blob_rejected() {
    uint8_t blob[kBlobLen];
    serialize(kDefaults, blob);
    blob[5] ^= 0xFF; // flip a struct byte -> CRC mismatch

    Settings out = kDefaults;
    out.steering.trimMicros = 999; // sentinel: must be left untouched on failure
    TEST_ASSERT_FALSE(deserialize(blob, kBlobLen, out));
    TEST_ASSERT_EQUAL_INT16(999, out.steering.trimMicros);
}

void test_wrong_version_rejected() {
    uint8_t blob[kBlobLen];
    serialize(kDefaults, blob);
    blob[0] = 0x77; // bump version...
    blob[kBlobLen - 1] = settings::computeCrc8(blob, 1 + sizeof(Settings)); // ...and fix CRC

    Settings out;
    TEST_ASSERT_FALSE(deserialize(blob, kBlobLen, out)); // still rejected on version
}

void test_empty_and_truncated_rejected() {
    Settings out;
    TEST_ASSERT_FALSE(deserialize(nullptr, 0, out)); // first boot: no data
    uint8_t blob[kBlobLen];
    serialize(kDefaults, blob);
    TEST_ASSERT_FALSE(deserialize(blob, kBlobLen - 1, out)); // truncated
}

void test_crc_valid_but_out_of_range_rejected() {
    Settings s = kDefaults;
    // Force an invalid value (trim pushes center past the max endpoint).
    s.steering.trimMicros = 30000;
    // Build a blob with a CORRECT crc for these (invalid) bytes.
    uint8_t blob[kBlobLen];
    serialize(s, blob);
    TEST_ASSERT_FALSE(s.valid()); // precondition

    Settings out;
    // CRC + version pass, but Settings::valid() must reject -> never applied.
    TEST_ASSERT_FALSE(deserialize(blob, kBlobLen, out));
}

void test_crc_matches_crsf_implementation() {
    const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX8(crsf::computeCrc8(data, sizeof(data)),
                           settings::computeCrc8(data, sizeof(data)));
}

// --- Shared boot loader (settings::loadOrDefault) ---
// These exercise the SAME code path the delivery esp32dev boot uses. They lean
// on deserialize() (tested above) rather than duplicating its logic; each case
// asserts the observable load result: status + a wholly-valid output object.

void test_loader_valid_store_loads_whole_object() {
    Settings saved = kDefaults;
    saved.steering.trimMicros = 42;
    saved.battery.calibrationPpt = 1015;
    saved.gearbox.gears[0].maxOutput = 450;
    uint8_t blob[kBlobLen];
    test_mocks::MockSettingsStore store;
    store.setStored(blob, serialize(saved, blob));

    const settings::LoadResult r = loadOrDefault(store);
    TEST_ASSERT_TRUE(r.status == LoadStatus::Loaded);
    TEST_ASSERT_TRUE(r.loadedFromStore());
    // The WHOLE valid object is applied, not just one field.
    TEST_ASSERT_EQUAL_INT16(42, r.settings.steering.trimMicros);
    TEST_ASSERT_EQUAL_UINT16(1015, r.settings.battery.calibrationPpt);
    TEST_ASSERT_EQUAL_INT16(450, r.settings.gearbox.gears[0].maxOutput);
}

void test_loader_empty_store_returns_defaults() {
    test_mocks::MockSettingsStore store; // fresh: hasData == false (first boot)
    const settings::LoadResult r = loadOrDefault(store);
    TEST_ASSERT_TRUE(r.status == LoadStatus::DefaultsNoStore);
    TEST_ASSERT_FALSE(r.loadedFromStore());
    TEST_ASSERT_TRUE(isWhollyDefault(r.settings));
}

void test_loader_truncated_returns_defaults() {
    uint8_t blob[kBlobLen];
    serialize(kDefaults, blob);
    test_mocks::MockSettingsStore store;
    store.setStored(blob, kBlobLen - 1); // wrong stored length

    const settings::LoadResult r = loadOrDefault(store);
    TEST_ASSERT_TRUE(r.status == LoadStatus::DefaultsInvalid);
    TEST_ASSERT_TRUE(isWhollyDefault(r.settings));
}

void test_loader_crc_corrupt_returns_defaults() {
    uint8_t blob[kBlobLen];
    serialize(kDefaults, blob);
    blob[5] ^= 0xFF; // flip a struct byte -> CRC mismatch
    test_mocks::MockSettingsStore store;
    store.setStored(blob, kBlobLen);

    const settings::LoadResult r = loadOrDefault(store);
    TEST_ASSERT_TRUE(r.status == LoadStatus::DefaultsInvalid);
    TEST_ASSERT_TRUE(isWhollyDefault(r.settings));
}

void test_loader_unsupported_version_returns_defaults() {
    uint8_t blob[kBlobLen];
    serialize(kDefaults, blob);
    blob[0] = 0x77;                                                          // bump version...
    blob[kBlobLen - 1] = settings::computeCrc8(blob, 1 + sizeof(Settings)); // ...and fix CRC
    test_mocks::MockSettingsStore store;
    store.setStored(blob, kBlobLen);

    const settings::LoadResult r = loadOrDefault(store);
    TEST_ASSERT_TRUE(r.status == LoadStatus::DefaultsInvalid);
    TEST_ASSERT_TRUE(isWhollyDefault(r.settings));
}

void test_loader_crc_valid_but_invalid_settings_returns_defaults() {
    Settings s = kDefaults;
    s.steering.trimMicros = 30000; // CRC-valid blob but Settings::valid() fails
    uint8_t blob[kBlobLen];
    serialize(s, blob);
    TEST_ASSERT_FALSE(s.valid()); // precondition
    test_mocks::MockSettingsStore store;
    store.setStored(blob, kBlobLen);

    const settings::LoadResult r = loadOrDefault(store);
    TEST_ASSERT_TRUE(r.status == LoadStatus::DefaultsInvalid);
    // Wholly default: no partial mix, no corrupt value clamped into validity.
    TEST_ASSERT_TRUE(isWhollyDefault(r.settings));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid);
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_corrupt_blob_rejected);
    RUN_TEST(test_wrong_version_rejected);
    RUN_TEST(test_empty_and_truncated_rejected);
    RUN_TEST(test_crc_valid_but_out_of_range_rejected);
    RUN_TEST(test_crc_matches_crsf_implementation);
    RUN_TEST(test_loader_valid_store_loads_whole_object);
    RUN_TEST(test_loader_empty_store_returns_defaults);
    RUN_TEST(test_loader_truncated_returns_defaults);
    RUN_TEST(test_loader_crc_corrupt_returns_defaults);
    RUN_TEST(test_loader_unsupported_version_returns_defaults);
    RUN_TEST(test_loader_crc_valid_but_invalid_settings_returns_defaults);
    return UNITY_END();
}
