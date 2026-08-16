#include <unity.h>

#include <cstring>

#include "crsf/CrsfParser.hpp" // for the CRC cross-check
#include "link2/Link2Frame.hpp" // sound profile/volume wire constants
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
        s.gimbalDecay.fullToCenterMs != kDefaults.gimbalDecay.fullToCenterMs ||
        s.gearbox.numGears != kDefaults.gearbox.numGears ||
        s.sound.profile != kDefaults.sound.profile ||
        s.sound.volume != kDefaults.sound.volume) {
        return false;
    }
    for (uint8_t i = 0; i < kDefaults.gearbox.numGears; ++i) {
        if (s.gearbox.gears[i].maxOutput != kDefaults.gearbox.gears[i].maxOutput ||
            s.gearbox.gears[i].expoPercent != kDefaults.gearbox.gears[i].expoPercent) {
            return false;
        }
    }
    if (s.btpad.maxOutput != kDefaults.btpad.maxOutput ||
        s.btpad.expoPercent != kDefaults.btpad.expoPercent ||
        s.btpad.steerDeadzone != kDefaults.btpad.steerDeadzone ||
        s.btpad.invertSteering != kDefaults.btpad.invertSteering ||
        s.btpad.armHoldMs != kDefaults.btpad.armHoldMs ||
        s.btpad.pairWindowMs != kDefaults.btpad.pairWindowMs) {
        return false;
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
    s.gimbalDecay.fullToCenterMs = 5000;
    s.sound.profile = link2::kSoundProfileV6Hybrid;
    s.sound.volume = 25; // quiet-indoor level, well off the default

    uint8_t blob[kBlobLen];
    TEST_ASSERT_EQUAL_UINT32(kBlobLen, serialize(s, blob));

    Settings out;
    TEST_ASSERT_TRUE(deserialize(blob, kBlobLen, out));
    TEST_ASSERT_EQUAL_INT16(42, out.steering.trimMicros);
    TEST_ASSERT_EQUAL_UINT16(1015, out.battery.calibrationPpt);
    TEST_ASSERT_EQUAL_INT16(450, out.gearbox.gears[0].maxOutput);
    TEST_ASSERT_EQUAL_UINT16(5000, out.gimbalDecay.fullToCenterMs);
    TEST_ASSERT_EQUAL_UINT8(link2::kSoundProfileV6Hybrid, out.sound.profile);
    TEST_ASSERT_EQUAL_UINT8(25, out.sound.volume);
}

// v2 sound fields obey the same guard chain as every other tunable: a
// CRC-valid blob carrying a reserved profile or an over-max volume fails
// Settings::valid() and must never be applied (the receiver-side fallback
// rules exist for WIRE robustness; persisted settings are held to the
// stricter sender bar).
void test_crc_valid_but_bad_sound_values_rejected() {
    Settings s = kDefaults;
    s.sound.profile = link2::kSoundProfileCount; // first reserved value
    uint8_t blob[kBlobLen];
    serialize(s, blob);
    TEST_ASSERT_FALSE(s.valid()); // precondition
    Settings out;
    TEST_ASSERT_FALSE(deserialize(blob, kBlobLen, out));

    Settings v = kDefaults;
    v.sound.volume = static_cast<uint8_t>(link2::kVolumeMax + 1);
    serialize(v, blob);
    TEST_ASSERT_FALSE(v.valid()); // precondition
    TEST_ASSERT_FALSE(deserialize(blob, kBlobLen, out));
}

// Migration pin for the 2026-08-17 v1 -> unified-v2 transition: a v1-SHAPED
// blob -- the old three-sub-config struct was SHORTER than the unified v2
// layout, and its version byte said 1 -- must reject to complete compiled
// defaults through both trap doors of the guard chain: the length gate (a
// stored v1 blob is not kBlobLen bytes) and, even if a hypothetical
// same-length v1 existed, the version gate. Exercised through deserialize()
// AND the boot loader so the delivery path is what's proven.
void test_v1_shaped_blob_rejected_to_defaults() {
    // Undersized "v1" blob: version byte 1, plausible old payload, valid CRC
    // over its own bytes. Rejected on length before anything else is read.
    uint8_t v1Blob[kBlobLen - 8]; // any length != kBlobLen models the v1 size
    for (size_t i = 0; i < sizeof(v1Blob); ++i) v1Blob[i] = static_cast<uint8_t>(i);
    v1Blob[0] = 1; // v1 version byte
    v1Blob[sizeof(v1Blob) - 1] = settings::computeCrc8(v1Blob, sizeof(v1Blob) - 1);
    Settings out = kDefaults;
    out.steering.trimMicros = 999; // sentinel
    TEST_ASSERT_FALSE(deserialize(v1Blob, sizeof(v1Blob), out));
    TEST_ASSERT_EQUAL_INT16(999, out.steering.trimMicros); // untouched

    // Same-length blob claiming version 1 with a CORRECT CRC: version gate.
    uint8_t blob[kBlobLen];
    serialize(kDefaults, blob);
    blob[0] = 1;
    blob[kBlobLen - 1] = settings::computeCrc8(blob, 1 + sizeof(Settings));
    TEST_ASSERT_FALSE(deserialize(blob, kBlobLen, out));

    // Boot-loader view of both: complete defaults, never a partial object.
    test_mocks::MockSettingsStore store;
    store.setStored(v1Blob, sizeof(v1Blob));
    settings::LoadResult r = loadOrDefault(store);
    TEST_ASSERT_TRUE(r.status == LoadStatus::DefaultsInvalid);
    TEST_ASSERT_TRUE(isWhollyDefault(r.settings));
    store.setStored(blob, kBlobLen);
    r = loadOrDefault(store);
    TEST_ASSERT_TRUE(r.status == LoadStatus::DefaultsInvalid);
    TEST_ASSERT_TRUE(isWhollyDefault(r.settings));
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

// --- blob v2: the btpad sub-config (docs/bt_showoff_design.md §3.3) ---
// (Version-pin and v1-header-rejection coverage lives in the migration tests
// below -- test_blob_version_is_2_after_gimbal_decay_addition and
// test_migration_v1_version_byte_on_v2_sized_blob_rejected apply to the whole
// unified six-group v2, btpad included; a btpad-specific duplicate was
// dropped at the 2026-08-17 reconciliation.)

void test_btpad_fields_roundtrip() {
    Settings s = kDefaults;
    s.btpad.maxOutput = 320;
    s.btpad.expoPercent = 30;
    s.btpad.steerDeadzone = 55;
    s.btpad.invertSteering = 1;
    s.btpad.armHoldMs = 1500;
    s.btpad.pairWindowMs = 12000;

    uint8_t blob[kBlobLen];
    TEST_ASSERT_EQUAL_UINT32(kBlobLen, serialize(s, blob));
    Settings out;
    TEST_ASSERT_TRUE(deserialize(blob, kBlobLen, out));
    TEST_ASSERT_EQUAL_INT16(320, out.btpad.maxOutput);
    TEST_ASSERT_EQUAL_UINT8(30, out.btpad.expoPercent);
    TEST_ASSERT_EQUAL_INT16(55, out.btpad.steerDeadzone);
    TEST_ASSERT_EQUAL_UINT8(1, out.btpad.invertSteering);
    TEST_ASSERT_EQUAL_UINT16(1500, out.btpad.armHoldMs);
    TEST_ASSERT_EQUAL_UINT16(12000, out.btpad.pairWindowMs);
}

void test_crc_valid_but_btpad_out_of_range_rejected() {
    Settings s = kDefaults;
    s.btpad.armHoldMs = 50; // below the 100 ms accidental-instant-arm floor
    uint8_t blob[kBlobLen];
    serialize(s, blob); // CORRECT CRC over invalid bytes
    TEST_ASSERT_FALSE(s.valid()); // precondition: Settings::valid() composes btpad.valid()

    Settings out;
    TEST_ASSERT_FALSE(deserialize(blob, kBlobLen, out));
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
    saved.sound.profile = link2::kSoundProfileV6Hybrid;
    saved.sound.volume = 25;
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
    TEST_ASSERT_EQUAL_UINT8(link2::kSoundProfileV6Hybrid, r.settings.sound.profile);
    TEST_ASSERT_EQUAL_UINT8(25, r.settings.sound.volume);
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

// ---------------------------------------------------------------------------
// Blob v2 (2026-08-16, gimbal link-loss decay added -- vision decision 11):
// version pin, v1 migration behavior, and the new field through every gate.
// ---------------------------------------------------------------------------

void test_blob_version_is_2_after_gimbal_decay_addition() {
    // Layout-change pin: adding Settings::gimbalDecay REQUIRED this bump.
    // If this fails, someone changed the layout or reverted the version --
    // either way the persisted-blob compatibility story must be re-checked.
    TEST_ASSERT_EQUAL_UINT8(2, settings::kBlobVersion);
}

// The v1 struct layout, recreated member-for-member (same types, same order,
// same ABI) so the migration test feeds loadOrDefault an AUTHENTIC v1-era
// blob: version byte 1, v1 struct size, CRC valid over its own bytes.
struct V1Settings {
    outputs::ServoConfig steering{};
    gearbox::GearboxConfig gearbox{};
    telemetry::BatteryConfig battery{};
};

void test_migration_authentic_v1_blob_yields_complete_defaults() {
    constexpr size_t kV1BlobLen = 1 + sizeof(V1Settings) + 1;
    static_assert(kV1BlobLen != settings::kBlobLen,
                  "v2 must have changed the blob length; if not, this test no "
                  "longer simulates the stored-v1 reality");

    const V1Settings v1{};
    uint8_t blob[kV1BlobLen];
    blob[0] = 1; // v1 version byte, as a real pre-upgrade device stored it
    std::memcpy(blob + 1, &v1, sizeof(V1Settings));
    blob[kV1BlobLen - 1] = settings::computeCrc8(blob, 1 + sizeof(V1Settings));

    test_mocks::MockSettingsStore store;
    store.setStored(blob, kV1BlobLen);

    // First boot on v2 firmware over a v1 blob: the guard chain rejects it
    // (length gate first) and the result is the COMPLETE compiled defaults --
    // never a partial adoption of the v1 fields.
    const settings::LoadResult r = loadOrDefault(store);
    TEST_ASSERT_TRUE(r.status == LoadStatus::DefaultsInvalid);
    TEST_ASSERT_TRUE(isWhollyDefault(r.settings));
}

void test_migration_v1_version_byte_on_v2_sized_blob_rejected() {
    // Defense in depth: even a right-SIZED blob claiming version 1 (CRC made
    // valid) must fall at the version gate -- the two gates reject a v1 blob
    // independently.
    uint8_t blob[kBlobLen];
    serialize(kDefaults, blob);
    blob[0] = 1;
    blob[kBlobLen - 1] = settings::computeCrc8(blob, 1 + sizeof(Settings));

    Settings out = kDefaults;
    out.steering.trimMicros = 999; // sentinel: must be left untouched
    TEST_ASSERT_FALSE(deserialize(blob, kBlobLen, out));
    TEST_ASSERT_EQUAL_INT16(999, out.steering.trimMicros);

    test_mocks::MockSettingsStore store;
    store.setStored(blob, kBlobLen);
    const settings::LoadResult r = loadOrDefault(store);
    TEST_ASSERT_TRUE(r.status == LoadStatus::DefaultsInvalid);
    TEST_ASSERT_TRUE(isWhollyDefault(r.settings));
}

void test_out_of_range_gimbal_decay_blob_rejected_wholesale() {
    // CRC-valid blob whose only flaw is a gimbal decay outside
    // GimbalDecayConfig::valid() -> the WHOLE blob is rejected (all-or-
    // nothing), exactly like the existing steering-trim case.
    Settings s = kDefaults;
    s.gimbalDecay.fullToCenterMs = 50; // below the 100 ms floor
    TEST_ASSERT_FALSE(s.valid());      // precondition

    uint8_t blob[kBlobLen];
    serialize(s, blob);
    test_mocks::MockSettingsStore store;
    store.setStored(blob, kBlobLen);

    const settings::LoadResult r = loadOrDefault(store);
    TEST_ASSERT_TRUE(r.status == LoadStatus::DefaultsInvalid);
    TEST_ASSERT_TRUE(isWhollyDefault(r.settings));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid);
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_crc_valid_but_bad_sound_values_rejected);
    RUN_TEST(test_v1_shaped_blob_rejected_to_defaults);
    RUN_TEST(test_corrupt_blob_rejected);
    RUN_TEST(test_wrong_version_rejected);
    RUN_TEST(test_empty_and_truncated_rejected);
    RUN_TEST(test_btpad_fields_roundtrip);
    RUN_TEST(test_crc_valid_but_btpad_out_of_range_rejected);
    RUN_TEST(test_crc_valid_but_out_of_range_rejected);
    RUN_TEST(test_crc_matches_crsf_implementation);
    RUN_TEST(test_loader_valid_store_loads_whole_object);
    RUN_TEST(test_loader_empty_store_returns_defaults);
    RUN_TEST(test_loader_truncated_returns_defaults);
    RUN_TEST(test_loader_crc_corrupt_returns_defaults);
    RUN_TEST(test_loader_unsupported_version_returns_defaults);
    RUN_TEST(test_loader_crc_valid_but_invalid_settings_returns_defaults);
    RUN_TEST(test_blob_version_is_2_after_gimbal_decay_addition);
    RUN_TEST(test_migration_authentic_v1_blob_yields_complete_defaults);
    RUN_TEST(test_migration_v1_version_byte_on_v2_sized_blob_rejected);
    RUN_TEST(test_out_of_range_gimbal_decay_blob_rejected_wholesale);
    return UNITY_END();
}
