#include <unity.h>

#include <cstring>

#include "crsf/CrsfFrameAssembler.hpp"
#include "crsf/CrsfFrameBuilder.hpp"
#include "crsf/CrsfParser.hpp"
#include "crsf/CrsfReceiver.hpp"

using crsf::buildFrame;
using crsf::CrsfFrameAssembler;
using crsf::CrsfLinkStatistics;
using crsf::CrsfReceiver;
using crsf::DecodeResult;
using crsf::RcChannelsFrame;

namespace {

// Frame construction lives in crsf/CrsfFrameBuilder.hpp (shared with the
// Wokwi sim feeder); these are thin canned-value wrappers for the tests.

// Builds a complete, CRC-valid RC_CHANNELS_PACKED frame (26 bytes).
void buildValidFrame(const uint16_t channels[crsf::kNumChannels], uint8_t* outFrame) {
    crsf::buildRcChannelsFrame(channels, outFrame);
}

// Builds a CRC-valid LINK_STATISTICS frame (14 bytes) with the given uplink LQ.
size_t buildLinkStatsFrame(uint8_t uplinkLq, uint8_t* outFrame) {
    CrsfLinkStatistics stats;
    stats.uplinkRssiAnt1 = 75;  // -75 dBm
    stats.uplinkRssiAnt2 = 108; // -108 dBm
    stats.uplinkLinkQuality = uplinkLq;
    stats.uplinkSnr = -10; // encodes as 0xF6: pins signed-byte handling
    stats.activeAntenna = 1;
    stats.rfMode = 4;        // ELRS packet-rate index, raw
    stats.uplinkTxPower = 3; // enum index, raw
    stats.downlinkRssi = 80;
    stats.downlinkLinkQuality = 99;
    stats.downlinkSnr = 5;
    return crsf::buildLinkStatisticsFrame(stats, outFrame);
}

void fillChannels(uint16_t channels[crsf::kNumChannels], uint16_t value) {
    for (size_t i = 0; i < crsf::kNumChannels; ++i) {
        channels[i] = value;
    }
}

// Feeds a whole buffer into a receiver; returns the LAST non-None result
// (tests feed one frame at a time, so this is that frame's result).
CrsfReceiver::ByteResult feedAll(CrsfReceiver& receiver, const uint8_t* data, size_t len,
                                  uint32_t nowMs) {
    CrsfReceiver::ByteResult last = CrsfReceiver::ByteResult::None;
    for (size_t i = 0; i < len; ++i) {
        const CrsfReceiver::ByteResult r = receiver.feedByte(data[i], nowMs);
        if (r != CrsfReceiver::ByteResult::None) {
            last = r;
        }
    }
    return last;
}

} // namespace

void setUp() {}
void tearDown() {}

// --- decodeFrame / unpackChannels / computeCrc8 (pure functions) ---

void test_decode_valid_frame_roundtrips_channels() {
    uint16_t expected[crsf::kNumChannels];
    for (size_t i = 0; i < crsf::kNumChannels; ++i) {
        expected[i] = static_cast<uint16_t>(crsf::kChannelRawMin + i * 50);
    }
    uint8_t frame[crsf::kRcChannelsFrameLen];
    buildValidFrame(expected, frame);

    RcChannelsFrame out{};
    TEST_ASSERT_EQUAL(DecodeResult::Ok, crsf::decodeFrame(frame, sizeof(frame), out));
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
    TEST_ASSERT_EQUAL(DecodeResult::Ok, crsf::decodeFrame(frame, sizeof(frame), out));
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
    TEST_ASSERT_EQUAL(DecodeResult::Ok, crsf::decodeFrame(frame, sizeof(frame), out));
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
    TEST_ASSERT_EQUAL(DecodeResult::CrcMismatch, crsf::decodeFrame(frame, sizeof(frame), out));
    TEST_ASSERT_EQUAL_UINT16(0xBEEF, out.channels[0]);
}

void test_decode_rejects_bad_sync() {
    uint16_t channels[crsf::kNumChannels];
    fillChannels(channels, crsf::kChannelRawCenter);
    uint8_t frame[crsf::kRcChannelsFrameLen];
    buildValidFrame(channels, frame);
    frame[0] = 0x00;

    RcChannelsFrame out{};
    TEST_ASSERT_EQUAL(DecodeResult::BadSync, crsf::decodeFrame(frame, sizeof(frame), out));
}

void test_decode_rejects_wrong_type() {
    uint16_t channels[crsf::kNumChannels];
    fillChannels(channels, crsf::kChannelRawCenter);
    uint8_t frame[crsf::kRcChannelsFrameLen];
    buildValidFrame(channels, frame);
    frame[2] = 0x99; // not RC_CHANNELS_PACKED

    RcChannelsFrame out{};
    TEST_ASSERT_EQUAL(DecodeResult::UnsupportedType, crsf::decodeFrame(frame, sizeof(frame), out));
}

void test_decode_rejects_too_short_buffer() {
    const uint8_t shortFrame[3] = {crsf::kSyncByte, crsf::kRcChannelsLengthByte,
                                    crsf::kFrameTypeRcChannelsPacked};
    RcChannelsFrame out{};
    TEST_ASSERT_EQUAL(DecodeResult::BadLength, crsf::decodeFrame(shortFrame, sizeof(shortFrame), out));
}

void test_crc8_known_answer_test_vector() {
    // CRC-8/DVB-S2 catalog "check" value for ASCII "123456789" is 0xBC -- an
    // independent cross-check of the CRC implementation.
    const uint8_t input[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX8(0xBC, crsf::computeCrc8(input, sizeof(input)));
}

void test_decode_link_statistics_field_mapping() {
    uint8_t frame[16];
    buildLinkStatsFrame(87, frame);

    CrsfLinkStatistics stats{};
    crsf::decodeLinkStatistics(frame + 3, stats);

    TEST_ASSERT_EQUAL_UINT8(75, stats.uplinkRssiAnt1);
    TEST_ASSERT_EQUAL_UINT8(108, stats.uplinkRssiAnt2);
    TEST_ASSERT_EQUAL_UINT8(87, stats.uplinkLinkQuality);
    TEST_ASSERT_EQUAL_INT8(-10, stats.uplinkSnr); // 0xF6 must decode as signed
    TEST_ASSERT_EQUAL_UINT8(1, stats.activeAntenna);
    TEST_ASSERT_EQUAL_UINT8(4, stats.rfMode);
    TEST_ASSERT_EQUAL_UINT8(3, stats.uplinkTxPower);
    TEST_ASSERT_EQUAL_UINT8(80, stats.downlinkRssi);
    TEST_ASSERT_EQUAL_UINT8(99, stats.downlinkLinkQuality);
    TEST_ASSERT_EQUAL_INT8(5, stats.downlinkSnr);
}

// --- CrsfFrameAssembler (framing + CRC, type-agnostic) ---

void test_assembler_frames_rc_frame_fed_byte_by_byte() {
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
    TEST_ASSERT_EQUAL_HEX8(crsf::kFrameTypeRcChannelsPacked, assembler.lastFrameType());
    TEST_ASSERT_EQUAL_UINT8(crsf::kRcChannelsPayloadLen, assembler.lastPayloadLen());

    uint16_t unpacked[crsf::kNumChannels];
    crsf::unpackChannels(assembler.lastPayload(), unpacked);
    for (size_t i = 0; i < crsf::kNumChannels; ++i) {
        TEST_ASSERT_EQUAL_UINT16(channels[i], unpacked[i]);
    }
}

void test_assembler_ignores_garbage_without_sync_byte() {
    CrsfFrameAssembler assembler;
    const uint8_t garbage[] = {0x00, 0xFF, 0x12, 0x34, 0x56, 0x78, 0x9A};
    for (uint8_t b : garbage) {
        TEST_ASSERT_EQUAL(CrsfFrameAssembler::FeedResult::Incomplete, assembler.feedByte(b));
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
    TEST_ASSERT_EQUAL_HEX8(crsf::kFrameTypeRcChannelsPacked, assembler.lastFrameType());
}

void test_assembler_accepts_crc_valid_link_stats() {
    // Regression for review finding A7: interleaved telemetry used to be
    // reported as FrameInvalid; a CRC-valid frame of any type is FrameReady.
    uint8_t frame[16];
    const size_t len = buildLinkStatsFrame(100, frame);

    CrsfFrameAssembler assembler;
    CrsfFrameAssembler::FeedResult result = CrsfFrameAssembler::FeedResult::Incomplete;
    for (size_t i = 0; i < len; ++i) {
        result = assembler.feedByte(frame[i]);
    }

    TEST_ASSERT_EQUAL(CrsfFrameAssembler::FeedResult::FrameReady, result);
    TEST_ASSERT_EQUAL_HEX8(crsf::kFrameTypeLinkStatistics, assembler.lastFrameType());
    TEST_ASSERT_EQUAL_UINT8(crsf::kLinkStatisticsPayloadLen, assembler.lastPayloadLen());
}

void test_assembler_rejects_corrupted_link_stats() {
    uint8_t frame[16];
    const size_t len = buildLinkStatsFrame(100, frame);
    frame[5] ^= 0xFF; // corrupt a payload byte

    CrsfFrameAssembler assembler;
    CrsfFrameAssembler::FeedResult result = CrsfFrameAssembler::FeedResult::Incomplete;
    for (size_t i = 0; i < len; ++i) {
        result = assembler.feedByte(frame[i]);
    }
    TEST_ASSERT_EQUAL(CrsfFrameAssembler::FeedResult::FrameInvalid, result);
}

void test_assembler_accepts_unknown_type_with_valid_crc() {
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t frame[10];
    const size_t len = buildFrame(0x08 /*battery telemetry*/, payload, sizeof(payload), frame);

    CrsfFrameAssembler assembler;
    CrsfFrameAssembler::FeedResult result = CrsfFrameAssembler::FeedResult::Incomplete;
    for (size_t i = 0; i < len; ++i) {
        result = assembler.feedByte(frame[i]);
    }
    TEST_ASSERT_EQUAL(CrsfFrameAssembler::FeedResult::FrameReady, result);
    TEST_ASSERT_EQUAL_HEX8(0x08, assembler.lastFrameType());
}

// --- CrsfReceiver facade ---

void test_receiver_dispatches_rc_and_stats_interleaved() {
    uint16_t channels[crsf::kNumChannels];
    fillChannels(channels, crsf::kChannelRawMax);
    uint8_t rcFrame[crsf::kRcChannelsFrameLen];
    buildValidFrame(channels, rcFrame);
    uint8_t statsFrame[16];
    const size_t statsLen = buildLinkStatsFrame(95, statsFrame);

    CrsfReceiver receiver;
    TEST_ASSERT_EQUAL(CrsfReceiver::ByteResult::NewRcFrame,
                      feedAll(receiver, rcFrame, sizeof(rcFrame), 100));
    TEST_ASSERT_EQUAL(CrsfReceiver::ByteResult::NewLinkStats,
                      feedAll(receiver, statsFrame, statsLen, 110));

    // Channels are an owned copy: intact after the stats frame passed through
    // the shared assembler buffer (regression guard for the one-shot
    // lastFrame() contract class of bug, finding A6).
    for (size_t i = 0; i < crsf::kNumChannels; ++i) {
        TEST_ASSERT_EQUAL_UINT16(crsf::kChannelRawMax, receiver.channels().channels[i]);
    }
    TEST_ASSERT_EQUAL_UINT8(95, receiver.linkStats().uplinkLinkQuality);
}

void test_receiver_stats_do_not_bump_rc_frame_time() {
    uint16_t channels[crsf::kNumChannels];
    fillChannels(channels, crsf::kChannelRawCenter);
    uint8_t rcFrame[crsf::kRcChannelsFrameLen];
    buildValidFrame(channels, rcFrame);
    uint8_t statsFrame[16];
    const size_t statsLen = buildLinkStatsFrame(100, statsFrame);

    CrsfReceiver receiver;
    feedAll(receiver, rcFrame, sizeof(rcFrame), 100);
    feedAll(receiver, statsFrame, statsLen, 500);

    // If stats counted as "RC frames", a LQ=0 burst during an outage would
    // extend the failsafe timeout -- they must not.
    TEST_ASSERT_EQUAL_UINT32(100, receiver.lastRcFrameMs());
}

void test_receiver_failsafe_flag_latches_on_zero_lq() {
    uint8_t statsFrame[16];
    CrsfReceiver receiver;

    TEST_ASSERT_FALSE(receiver.rxSignalsFailsafe()); // no stats ever: no signal

    const size_t len = buildLinkStatsFrame(0, statsFrame); // ELRS loss burst
    feedAll(receiver, statsFrame, len, 100);
    TEST_ASSERT_TRUE(receiver.rxSignalsFailsafe());
}

void test_receiver_failsafe_flag_survives_rc_frames_without_fresh_stats() {
    // THE Set-Position hazard pin (design review point 5): after a LQ=0
    // burst, hold-position RC frames keep flowing but no fresh stats do.
    // RC frames must NOT clear the latch -- only a LQ>0 stats frame may.
    uint8_t statsFrame[16];
    const size_t statsLen = buildLinkStatsFrame(0, statsFrame);
    uint16_t channels[crsf::kNumChannels];
    fillChannels(channels, crsf::kChannelRawMax); // "held" failsafe positions
    uint8_t rcFrame[crsf::kRcChannelsFrameLen];
    buildValidFrame(channels, rcFrame);

    CrsfReceiver receiver;
    feedAll(receiver, statsFrame, statsLen, 100);
    TEST_ASSERT_TRUE(receiver.rxSignalsFailsafe());

    for (uint32_t t = 110; t < 5000; t += 20) { // ~5s of bogus RC frames
        feedAll(receiver, rcFrame, sizeof(rcFrame), t);
    }
    TEST_ASSERT_TRUE(receiver.rxSignalsFailsafe()); // still latched
}

void test_receiver_failsafe_flag_clears_on_good_stats() {
    uint8_t lossFrame[16];
    const size_t lossLen = buildLinkStatsFrame(0, lossFrame);
    uint8_t goodFrame[16];
    const size_t goodLen = buildLinkStatsFrame(70, goodFrame);

    CrsfReceiver receiver;
    feedAll(receiver, lossFrame, lossLen, 100);
    TEST_ASSERT_TRUE(receiver.rxSignalsFailsafe());

    feedAll(receiver, goodFrame, goodLen, 200);
    TEST_ASSERT_FALSE(receiver.rxSignalsFailsafe());
}

void test_receiver_ignores_known_type_with_wrong_payload_length() {
    // A CRC-valid frame claiming to be RC_CHANNELS_PACKED but with the wrong
    // payload size must be ignored, not decoded (per-type length validation
    // moved here when the assembler went type-agnostic).
    const uint8_t shortPayload[11] = {};
    uint8_t frame[16];
    const size_t len = buildFrame(crsf::kFrameTypeRcChannelsPacked, shortPayload, 11, frame);

    CrsfReceiver receiver;
    const CrsfReceiver::ByteResult result = feedAll(receiver, frame, len, 100);

    TEST_ASSERT_EQUAL(CrsfReceiver::ByteResult::None, result);
    TEST_ASSERT_FALSE(receiver.hasEverReceivedRcFrame());
}

void test_receiver_unknown_type_changes_nothing() {
    uint16_t channels[crsf::kNumChannels];
    fillChannels(channels, crsf::kChannelRawCenter);
    uint8_t rcFrame[crsf::kRcChannelsFrameLen];
    buildValidFrame(channels, rcFrame);

    CrsfReceiver receiver;
    feedAll(receiver, rcFrame, sizeof(rcFrame), 100);

    const uint8_t payload[] = {0xAA, 0xBB};
    uint8_t frame[8];
    const size_t len = buildFrame(0x02 /*GPS*/, payload, sizeof(payload), frame);
    const CrsfReceiver::ByteResult result = feedAll(receiver, frame, len, 200);

    TEST_ASSERT_EQUAL(CrsfReceiver::ByteResult::None, result);
    TEST_ASSERT_EQUAL_UINT32(100, receiver.lastRcFrameMs());
    TEST_ASSERT_EQUAL_UINT16(crsf::kChannelRawCenter, receiver.channels().channels[0]);
    TEST_ASSERT_FALSE(receiver.rxSignalsFailsafe());
}

void test_receiver_reports_corrupt_frames() {
    uint8_t statsFrame[16];
    const size_t len = buildLinkStatsFrame(100, statsFrame);
    statsFrame[len - 1] ^= 0xFF; // corrupt CRC

    CrsfReceiver receiver;
    TEST_ASSERT_EQUAL(CrsfReceiver::ByteResult::CorruptFrame,
                      feedAll(receiver, statsFrame, len, 100));
}

void test_receiver_link_up_summary() {
    uint16_t channels[crsf::kNumChannels];
    fillChannels(channels, crsf::kChannelRawCenter);
    uint8_t rcFrame[crsf::kRcChannelsFrameLen];
    buildValidFrame(channels, rcFrame);

    CrsfReceiver receiver;
    TEST_ASSERT_FALSE(receiver.linkUp(0)); // nothing received yet

    feedAll(receiver, rcFrame, sizeof(rcFrame), 100);
    TEST_ASSERT_TRUE(receiver.linkUp(200));   // fresh RC frames
    TEST_ASSERT_FALSE(receiver.linkUp(700));  // stale past the 500ms default

    // Latched LQ=0 makes linkUp false even with fresh RC frames.
    uint8_t lossFrame[16];
    const size_t lossLen = buildLinkStatsFrame(0, lossFrame);
    feedAll(receiver, lossFrame, lossLen, 210);
    feedAll(receiver, rcFrame, sizeof(rcFrame), 220);
    TEST_ASSERT_FALSE(receiver.linkUp(230));
}

void test_build_battery_frame_bytes() {
    // 7.9V = 79 dV = 0x004F; current 0; capacity 0; 72%. Big-endian payload,
    // matching the ground station's decodeBattery.
    uint8_t frame[4 + crsf::kBatteryPayloadLen];
    const size_t n = crsf::buildBatteryFrame(79, 0, 0, 72, frame);

    TEST_ASSERT_EQUAL_UINT32(4 + crsf::kBatteryPayloadLen, n);
    TEST_ASSERT_EQUAL_HEX8(crsf::kSyncByte, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(crsf::kBatteryPayloadLen + 2, frame[1]); // length
    TEST_ASSERT_EQUAL_HEX8(crsf::kFrameTypeBattery, frame[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[3]); // voltage hi
    TEST_ASSERT_EQUAL_HEX8(0x4F, frame[4]); // voltage lo
    TEST_ASSERT_EQUAL_UINT8(72, frame[10]); // remaining %
    // CRC over [type + payload], verifiable via the same computeCrc8.
    TEST_ASSERT_EQUAL_HEX8(crsf::computeCrc8(frame + 2, 1 + crsf::kBatteryPayloadLen), frame[11]);
}

void test_build_battery_frame_capacity_is_24bit_be() {
    uint8_t frame[4 + crsf::kBatteryPayloadLen];
    crsf::buildBatteryFrame(80, 15, 0x0004D2, 55, frame); // capacity 1234 = 0x0004D2
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[7]);
    TEST_ASSERT_EQUAL_HEX8(0x04, frame[8]);
    TEST_ASSERT_EQUAL_HEX8(0xD2, frame[9]);
}

void test_build_gps_frame_groundspeed_be() {
    // 36.1 km/h -> 361 = 0x0169 in the 0.1-km/h groundspeed field (bytes 8-9).
    // Altitude baseline 1000 = 0x03E8 (bytes 12-13). Big-endian, CRC over type+payload.
    uint8_t frame[4 + crsf::kGpsPayloadLen];
    const size_t n = crsf::buildGpsFrame(0, 0, 361, 0, 1000, 0, frame);

    TEST_ASSERT_EQUAL_UINT32(4 + crsf::kGpsPayloadLen, n);
    TEST_ASSERT_EQUAL_HEX8(crsf::kSyncByte, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(crsf::kGpsPayloadLen + 2, frame[1]); // length
    TEST_ASSERT_EQUAL_HEX8(crsf::kFrameTypeGps, frame[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, frame[11]); // groundspeed hi (payload offset 8 -> frame 11)
    TEST_ASSERT_EQUAL_HEX8(0x69, frame[12]); // groundspeed lo
    TEST_ASSERT_EQUAL_HEX8(0x03, frame[15]); // altitude hi
    TEST_ASSERT_EQUAL_HEX8(0xE8, frame[16]); // altitude lo
    // payload is 15 bytes (frame[3..17]); CRC follows at frame[18].
    TEST_ASSERT_EQUAL_HEX8(crsf::computeCrc8(frame + 2, 1 + crsf::kGpsPayloadLen), frame[18]);
}

void test_build_flight_mode_frame_string_nul_terminated() {
    // "G3 M2 E55" -> payload is the 9 chars + a NUL, so payloadLen 10.
    uint8_t frame[4 + crsf::kFlightModeMaxLen];
    const size_t n = crsf::buildFlightModeFrame("G3 M2 E55", frame);

    const uint8_t payloadLen = 10; // 9 chars + NUL
    TEST_ASSERT_EQUAL_UINT32(4u + payloadLen, n);
    TEST_ASSERT_EQUAL_HEX8(crsf::kSyncByte, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(payloadLen + 2, frame[1]); // length = type + payload + crc
    TEST_ASSERT_EQUAL_HEX8(crsf::kFrameTypeFlightMode, frame[2]);
    TEST_ASSERT_EQUAL_HEX8('G', frame[3]);
    TEST_ASSERT_EQUAL_HEX8('5', frame[11]); // last char
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[12]); // NUL terminator in payload
    TEST_ASSERT_EQUAL_HEX8(crsf::computeCrc8(frame + 2, 1 + payloadLen), frame[13]);
}

void test_build_flight_mode_frame_truncates_overlong() {
    // 20 chars -> truncated to kFlightModeMaxLen-1 (15) + NUL.
    uint8_t frame[4 + crsf::kFlightModeMaxLen];
    const size_t n = crsf::buildFlightModeFrame("ABCDEFGHIJKLMNOPQRST", frame);
    TEST_ASSERT_EQUAL_UINT32(4u + crsf::kFlightModeMaxLen, n); // 15 chars + NUL = 16 payload
    TEST_ASSERT_EQUAL_HEX8('O', frame[3 + 14]);                // 15th char kept
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[3 + 15]);               // NUL at the end
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_build_battery_frame_bytes);
    RUN_TEST(test_build_battery_frame_capacity_is_24bit_be);
    RUN_TEST(test_build_gps_frame_groundspeed_be);
    RUN_TEST(test_build_flight_mode_frame_string_nul_terminated);
    RUN_TEST(test_build_flight_mode_frame_truncates_overlong);
    RUN_TEST(test_decode_valid_frame_roundtrips_channels);
    RUN_TEST(test_decode_all_channels_at_center);
    RUN_TEST(test_decode_endpoint_values);
    RUN_TEST(test_decode_rejects_bad_crc);
    RUN_TEST(test_decode_rejects_bad_sync);
    RUN_TEST(test_decode_rejects_wrong_type);
    RUN_TEST(test_decode_rejects_too_short_buffer);
    RUN_TEST(test_crc8_known_answer_test_vector);
    RUN_TEST(test_decode_link_statistics_field_mapping);
    RUN_TEST(test_assembler_frames_rc_frame_fed_byte_by_byte);
    RUN_TEST(test_assembler_ignores_garbage_without_sync_byte);
    RUN_TEST(test_assembler_resyncs_after_corrupted_frame);
    RUN_TEST(test_assembler_accepts_crc_valid_link_stats);
    RUN_TEST(test_assembler_rejects_corrupted_link_stats);
    RUN_TEST(test_assembler_accepts_unknown_type_with_valid_crc);
    RUN_TEST(test_receiver_dispatches_rc_and_stats_interleaved);
    RUN_TEST(test_receiver_stats_do_not_bump_rc_frame_time);
    RUN_TEST(test_receiver_failsafe_flag_latches_on_zero_lq);
    RUN_TEST(test_receiver_failsafe_flag_survives_rc_frames_without_fresh_stats);
    RUN_TEST(test_receiver_failsafe_flag_clears_on_good_stats);
    RUN_TEST(test_receiver_ignores_known_type_with_wrong_payload_length);
    RUN_TEST(test_receiver_unknown_type_changes_nothing);
    RUN_TEST(test_receiver_reports_corrupt_frames);
    RUN_TEST(test_receiver_link_up_summary);
    return UNITY_END();
}
