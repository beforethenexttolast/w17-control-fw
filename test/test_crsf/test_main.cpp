#include <unity.h>

#include <cstring>

#include "crsf/CrsfFrameAssembler.hpp"
#include "crsf/CrsfParser.hpp"

using crsf::CrsfFrameAssembler;
using crsf::DecodeResult;
using crsf::RcChannelsFrame;

namespace {

// Inverse of crsf::unpackChannels -- packs 16 raw 11-bit channel values into
// the 22-byte RC_CHANNELS_PACKED payload, for building canned test frames.
void packChannels(const uint16_t channels[crsf::kNumChannels],
                   uint8_t payload[crsf::kRcChannelsPayloadLen]) {
    std::memset(payload, 0, crsf::kRcChannelsPayloadLen);
    for (size_t ch = 0; ch < crsf::kNumChannels; ++ch) {
        const size_t bitPos = ch * 11;
        const uint16_t value = channels[ch] & 0x07FF;
        for (int bit = 0; bit < 11; ++bit) {
            if ((value & (1u << bit)) == 0) {
                continue;
            }
            const size_t overallBit = bitPos + static_cast<size_t>(bit);
            const size_t byteIdx = overallBit / 8;
            const size_t bitOffset = overallBit % 8;
            payload[byteIdx] |= static_cast<uint8_t>(1u << bitOffset);
        }
    }
}

// Builds a complete, CRC-valid RC_CHANNELS_PACKED frame into `outFrame`
// (must be at least crsf::kRcChannelsFrameLen bytes).
void buildValidFrame(const uint16_t channels[crsf::kNumChannels], uint8_t* outFrame) {
    uint8_t payload[crsf::kRcChannelsPayloadLen];
    packChannels(channels, payload);

    outFrame[0] = crsf::kSyncByte;
    outFrame[1] = crsf::kRcChannelsLengthByte;
    outFrame[2] = crsf::kFrameTypeRcChannelsPacked;
    std::memcpy(outFrame + 3, payload, crsf::kRcChannelsPayloadLen);
    outFrame[3 + crsf::kRcChannelsPayloadLen] =
        crsf::computeCrc8(outFrame + 2, 1 + crsf::kRcChannelsPayloadLen);
}

void fillChannels(uint16_t channels[crsf::kNumChannels], uint16_t value) {
    for (size_t i = 0; i < crsf::kNumChannels; ++i) {
        channels[i] = value;
    }
}

} // namespace

void setUp() {}
void tearDown() {}

void test_decode_valid_frame_roundtrips_channels() {
    uint16_t expected[crsf::kNumChannels];
    for (size_t i = 0; i < crsf::kNumChannels; ++i) {
        expected[i] = static_cast<uint16_t>(crsf::kChannelRawMin + i * 50);
    }
    uint8_t frame[crsf::kRcChannelsFrameLen];
    buildValidFrame(expected, frame);

    RcChannelsFrame out{};
    const DecodeResult result = crsf::decodeFrame(frame, sizeof(frame), out);

    TEST_ASSERT_EQUAL(DecodeResult::Ok, result);
    for (size_t i = 0; i < crsf::kNumChannels; ++i) {
        TEST_ASSERT_EQUAL_UINT16(expected[i], out.channels[i]);
    }
}

void test_decode_all_channels_at_center() {
    uint16_t channels[crsf::kNumChannels];
    fillChannels(channels, crsf::kChannelRawCenter);
    uint8_t frame[crsf::kRcChannelsFrameLen];
    buildValidFrame(channels, frame);

    RcChannelsFrame out{};
    const DecodeResult result = crsf::decodeFrame(frame, sizeof(frame), out);

    TEST_ASSERT_EQUAL(DecodeResult::Ok, result);
    for (size_t i = 0; i < crsf::kNumChannels; ++i) {
        TEST_ASSERT_EQUAL_UINT16(crsf::kChannelRawCenter, out.channels[i]);
    }
}

void test_decode_endpoint_values() {
    uint16_t channels[crsf::kNumChannels];
    for (size_t i = 0; i < crsf::kNumChannels; ++i) {
        channels[i] = (i % 2 == 0) ? crsf::kChannelRawMin : crsf::kChannelRawMax;
    }
    uint8_t frame[crsf::kRcChannelsFrameLen];
    buildValidFrame(channels, frame);

    RcChannelsFrame out{};
    const DecodeResult result = crsf::decodeFrame(frame, sizeof(frame), out);

    TEST_ASSERT_EQUAL(DecodeResult::Ok, result);
    for (size_t i = 0; i < crsf::kNumChannels; ++i) {
        TEST_ASSERT_EQUAL_UINT16(channels[i], out.channels[i]);
    }
}

void test_decode_rejects_bad_crc() {
    uint16_t channels[crsf::kNumChannels];
    fillChannels(channels, crsf::kChannelRawCenter);
    uint8_t frame[crsf::kRcChannelsFrameLen];
    buildValidFrame(channels, frame);
    frame[3 + crsf::kRcChannelsPayloadLen] ^= 0xFF; // corrupt the CRC byte

    RcChannelsFrame out{};
    out.channels[0] = 0xBEEF; // sentinel: must not be overwritten on failure
    const DecodeResult result = crsf::decodeFrame(frame, sizeof(frame), out);

    TEST_ASSERT_EQUAL(DecodeResult::CrcMismatch, result);
    TEST_ASSERT_EQUAL_UINT16(0xBEEF, out.channels[0]);
}

void test_decode_rejects_bad_sync() {
    uint16_t channels[crsf::kNumChannels];
    fillChannels(channels, crsf::kChannelRawCenter);
    uint8_t frame[crsf::kRcChannelsFrameLen];
    buildValidFrame(channels, frame);
    frame[0] = 0x00;

    RcChannelsFrame out{};
    const DecodeResult result = crsf::decodeFrame(frame, sizeof(frame), out);

    TEST_ASSERT_EQUAL(DecodeResult::BadSync, result);
}

void test_decode_rejects_wrong_type() {
    uint16_t channels[crsf::kNumChannels];
    fillChannels(channels, crsf::kChannelRawCenter);
    uint8_t frame[crsf::kRcChannelsFrameLen];
    buildValidFrame(channels, frame);
    frame[2] = 0x99; // not RC_CHANNELS_PACKED

    RcChannelsFrame out{};
    const DecodeResult result = crsf::decodeFrame(frame, sizeof(frame), out);

    TEST_ASSERT_EQUAL(DecodeResult::UnsupportedType, result);
}

void test_decode_rejects_too_short_buffer() {
    const uint8_t shortFrame[3] = {crsf::kSyncByte, crsf::kRcChannelsLengthByte,
                                    crsf::kFrameTypeRcChannelsPacked};
    RcChannelsFrame out{};
    const DecodeResult result = crsf::decodeFrame(shortFrame, sizeof(shortFrame), out);

    TEST_ASSERT_EQUAL(DecodeResult::BadLength, result);
}

void test_crc8_known_answer_test_vector() {
    // CRSF's CRC8 matches the standard CRC-8/DVB-S2 algorithm (poly 0xD5, init
    // 0x00, no reflect). The catalog "check" value for input "123456789"
    // (ASCII bytes) is 0xBC -- a stable, independent cross-check of the
    // implementation that doesn't rely on decodeFrame() at all.
    const uint8_t input[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX8(0xBC, crsf::computeCrc8(input, sizeof(input)));
}

void test_assembler_decodes_frame_fed_byte_by_byte() {
    uint16_t channels[crsf::kNumChannels];
    for (size_t i = 0; i < crsf::kNumChannels; ++i) {
        channels[i] = static_cast<uint16_t>(crsf::kChannelRawMin + i * 30);
    }
    uint8_t frame[crsf::kRcChannelsFrameLen];
    buildValidFrame(channels, frame);

    CrsfFrameAssembler assembler;
    CrsfFrameAssembler::FeedResult result = CrsfFrameAssembler::FeedResult::Incomplete;
    for (size_t i = 0; i < sizeof(frame); ++i) {
        result = assembler.feedByte(frame[i]);
        if (i + 1 < sizeof(frame)) {
            TEST_ASSERT_EQUAL(CrsfFrameAssembler::FeedResult::Incomplete, result);
        }
    }

    TEST_ASSERT_EQUAL(CrsfFrameAssembler::FeedResult::FrameReady, result);
    for (size_t i = 0; i < crsf::kNumChannels; ++i) {
        TEST_ASSERT_EQUAL_UINT16(channels[i], assembler.lastFrame().channels[i]);
    }
}

void test_assembler_ignores_garbage_without_sync_byte() {
    CrsfFrameAssembler assembler;
    const uint8_t garbage[] = {0x00, 0xFF, 0x12, 0x34, 0x56, 0x78, 0x9A};
    for (uint8_t b : garbage) {
        const CrsfFrameAssembler::FeedResult result = assembler.feedByte(b);
        TEST_ASSERT_EQUAL(CrsfFrameAssembler::FeedResult::Incomplete, result);
    }
}

void test_assembler_resyncs_after_corrupted_frame() {
    uint16_t channels[crsf::kNumChannels];
    fillChannels(channels, crsf::kChannelRawCenter);
    uint8_t goodFrame[crsf::kRcChannelsFrameLen];
    buildValidFrame(channels, goodFrame);

    uint8_t corruptFrame[crsf::kRcChannelsFrameLen];
    std::memcpy(corruptFrame, goodFrame, sizeof(corruptFrame));
    corruptFrame[3 + crsf::kRcChannelsPayloadLen] ^= 0xFF; // corrupt CRC

    CrsfFrameAssembler assembler;
    CrsfFrameAssembler::FeedResult result = CrsfFrameAssembler::FeedResult::Incomplete;
    for (uint8_t b : corruptFrame) {
        result = assembler.feedByte(b);
    }
    TEST_ASSERT_EQUAL(CrsfFrameAssembler::FeedResult::FrameInvalid, result);

    for (size_t i = 0; i < sizeof(goodFrame); ++i) {
        result = assembler.feedByte(goodFrame[i]);
    }
    TEST_ASSERT_EQUAL(CrsfFrameAssembler::FeedResult::FrameReady, result);
    for (size_t i = 0; i < crsf::kNumChannels; ++i) {
        TEST_ASSERT_EQUAL_UINT16(channels[i], assembler.lastFrame().channels[i]);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_decode_valid_frame_roundtrips_channels);
    RUN_TEST(test_decode_all_channels_at_center);
    RUN_TEST(test_decode_endpoint_values);
    RUN_TEST(test_decode_rejects_bad_crc);
    RUN_TEST(test_decode_rejects_bad_sync);
    RUN_TEST(test_decode_rejects_wrong_type);
    RUN_TEST(test_decode_rejects_too_short_buffer);
    RUN_TEST(test_crc8_known_answer_test_vector);
    RUN_TEST(test_assembler_decodes_frame_fed_byte_by_byte);
    RUN_TEST(test_assembler_ignores_garbage_without_sync_byte);
    RUN_TEST(test_assembler_resyncs_after_corrupted_frame);
    return UNITY_END();
}
