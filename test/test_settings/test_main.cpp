#include <unity.h>

#include <cstring>

#include "crsf/CrsfParser.hpp" // for the CRC cross-check
#include "settings/Settings.hpp"

using settings::deserialize;
using settings::kBlobLen;
using settings::kDefaults;
using settings::serialize;
using settings::Settings;

void setUp() {}
void tearDown() {}

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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid);
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_corrupt_blob_rejected);
    RUN_TEST(test_wrong_version_rejected);
    RUN_TEST(test_empty_and_truncated_rejected);
    RUN_TEST(test_crc_valid_but_out_of_range_rejected);
    RUN_TEST(test_crc_matches_crsf_implementation);
    return UNITY_END();
}
