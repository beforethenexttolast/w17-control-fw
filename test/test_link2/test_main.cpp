#include <unity.h>

#include <cstring>

#include "link2/Link2Codec.hpp"
#include "link2/Link2Sender.hpp"

// Test-only cross-check: link2 duplicates the CRSF CRC8 algorithm on purpose
// (self-contained lib); prove the two implementations agree.
#include "crsf/CrsfParser.hpp"

#include "../mocks/MockByteSink.hpp"

using link2::ControlSnapshot;
using link2::DecodeResult;
using link2::Link2FrameAssembler;
using link2::Link2Sender;
using link2::VehicleState;
using test_mocks::MockByteSink;

namespace {

VehicleState makeGoldenState() {
    VehicleState s;
    s.throttlePercent = 42;
    s.steeringPercent = -25;
    s.braking = false;
    s.drsOpen = true;
    s.armed = true;
    s.failsafe = false;
    s.lowBattery = false;
    s.gear = 3;
    s.rpm = 1500;
    s.batteryMv = 7900;
    return s;
}

// The exact on-wire bytes for makeGoldenState(), mirrored in
// docs/link2_protocol.md's worked example. If this test breaks, the protocol
// changed and the doc + board #2 must change with it.
const uint8_t kGoldenFrame[link2::kFrameLen] = {
    0xA5,             // start
    0x09,             // length
    0x01,             // version
    0x2A,             // throttlePercent = +42
    0xE7,             // steeringPercent = -25
    0x0C,             // flags: drsOpen | armed
    0x03,             // gear 3
    0xDC, 0x05,       // rpm = 1500 LE
    0xDC, 0x1E,       // batteryMv = 7900 LE
    0xF9,             // crc8 over [length + payload]
};

} // namespace

void setUp() {}
void tearDown() {}

void test_golden_frame_bytes() {
    uint8_t frame[link2::kFrameLen];
    const size_t written = link2::encodeFrame(makeGoldenState(), frame);

    TEST_ASSERT_EQUAL_UINT32(link2::kFrameLen, written);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kGoldenFrame, frame, link2::kFrameLen);
}

void test_crc_matches_crsf_implementation() {
    const uint8_t data[] = {0x09, 0x01, 0x2A, 0xE7, 0x0C, 0x03, 0xDC, 0x05, 0xDC, 0x1E};
    TEST_ASSERT_EQUAL_HEX8(crsf::computeCrc8(data, sizeof(data)),
                           link2::computeCrc8(data, sizeof(data)));
}

void test_encode_decode_roundtrip() {
    VehicleState in;
    in.throttlePercent = -100;
    in.steeringPercent = 100;
    in.braking = true;
    in.drsOpen = false;
    in.armed = true;
    in.failsafe = false;
    in.lowBattery = true;
    in.gear = 6;
    in.rpm = 65535;
    in.batteryMv = 8400;

    uint8_t frame[link2::kFrameLen];
    link2::encodeFrame(in, frame);

    VehicleState out;
    TEST_ASSERT_EQUAL(DecodeResult::Ok, link2::decodeFrame(frame, sizeof(frame), out));
    TEST_ASSERT_EQUAL_INT8(in.throttlePercent, out.throttlePercent);
    TEST_ASSERT_EQUAL_INT8(in.steeringPercent, out.steeringPercent);
    TEST_ASSERT_EQUAL(in.braking, out.braking);
    TEST_ASSERT_EQUAL(in.drsOpen, out.drsOpen);
    TEST_ASSERT_EQUAL(in.armed, out.armed);
    TEST_ASSERT_EQUAL(in.failsafe, out.failsafe);
    TEST_ASSERT_EQUAL(in.lowBattery, out.lowBattery);
    TEST_ASSERT_EQUAL_UINT8(in.gear, out.gear);
    TEST_ASSERT_EQUAL_UINT16(in.rpm, out.rpm);
    TEST_ASSERT_EQUAL_UINT16(in.batteryMv, out.batteryMv);
}

void test_each_flag_bit_pinned() {
    struct Case {
        bool VehicleState::*field;
        uint8_t expectedBit;
    };
    const Case cases[] = {
        {&VehicleState::braking, link2::kFlagBraking},
        {&VehicleState::reverse, link2::kFlagReverse},
        {&VehicleState::drsOpen, link2::kFlagDrsOpen},
        {&VehicleState::armed, link2::kFlagArmed},
        {&VehicleState::failsafe, link2::kFlagFailsafe},
        {&VehicleState::lowBattery, link2::kFlagLowBattery},
    };
    for (const Case& c : cases) {
        VehicleState s;
        s.failsafe = false; // clear the default so only the tested bit is set
        s.*(c.field) = true;
        uint8_t frame[link2::kFrameLen];
        link2::encodeFrame(s, frame);
        TEST_ASSERT_EQUAL_HEX8(c.expectedBit, frame[5]);
    }
}

void test_decode_rejects_bad_start() {
    uint8_t frame[link2::kFrameLen];
    link2::encodeFrame(makeGoldenState(), frame);
    frame[0] = 0x00;
    VehicleState out;
    TEST_ASSERT_EQUAL(DecodeResult::BadStart, link2::decodeFrame(frame, sizeof(frame), out));
}

void test_decode_rejects_bad_length_and_short_buffer() {
    uint8_t frame[link2::kFrameLen];
    link2::encodeFrame(makeGoldenState(), frame);
    VehicleState out;

    TEST_ASSERT_EQUAL(DecodeResult::BadLength, link2::decodeFrame(frame, 5, out));

    frame[1] = 0x0A; // unsupported payload length
    TEST_ASSERT_EQUAL(DecodeResult::BadLength, link2::decodeFrame(frame, sizeof(frame), out));
}

void test_decode_validation_order_crc_before_version() {
    uint8_t frame[link2::kFrameLen];
    link2::encodeFrame(makeGoldenState(), frame);
    VehicleState out;

    // Corrupt version WITHOUT fixing the CRC: reports CrcMismatch (corruption).
    frame[2] = 2;
    TEST_ASSERT_EQUAL(DecodeResult::CrcMismatch, link2::decodeFrame(frame, sizeof(frame), out));

    // Corrupt version WITH a recomputed CRC: a well-formed frame from a newer
    // sender -> BadVersion.
    frame[link2::kFrameLen - 1] = link2::computeCrc8(frame + 1, 1 + link2::kPayloadLen);
    TEST_ASSERT_EQUAL(DecodeResult::BadVersion, link2::decodeFrame(frame, sizeof(frame), out));
}

void test_decode_leaves_out_untouched_on_failure() {
    uint8_t frame[link2::kFrameLen];
    link2::encodeFrame(makeGoldenState(), frame);
    frame[link2::kFrameLen - 1] ^= 0xFF;

    VehicleState out;
    out.gear = 42; // sentinel
    TEST_ASSERT_EQUAL(DecodeResult::CrcMismatch, link2::decodeFrame(frame, sizeof(frame), out));
    TEST_ASSERT_EQUAL_UINT8(42, out.gear);
}

void test_assembler_frame_byte_by_byte() {
    uint8_t frame[link2::kFrameLen];
    link2::encodeFrame(makeGoldenState(), frame);

    Link2FrameAssembler assembler;
    Link2FrameAssembler::FeedResult result = Link2FrameAssembler::FeedResult::Incomplete;
    for (size_t i = 0; i < sizeof(frame); ++i) {
        result = assembler.feedByte(frame[i]);
        if (i + 1 < sizeof(frame)) {
            TEST_ASSERT_EQUAL(Link2FrameAssembler::FeedResult::Incomplete, result);
        }
    }
    TEST_ASSERT_EQUAL(Link2FrameAssembler::FeedResult::FrameReady, result);
    TEST_ASSERT_EQUAL_INT8(42, assembler.lastState().throttlePercent);
    TEST_ASSERT_EQUAL_UINT16(1500, assembler.lastState().rpm);
}

void test_assembler_hard_rejects_bad_length_byte_immediately() {
    Link2FrameAssembler assembler;
    TEST_ASSERT_EQUAL(Link2FrameAssembler::FeedResult::Incomplete,
                      assembler.feedByte(link2::kStartByte));
    // A corrupt 0xFF length must be rejected NOW, not after swallowing 255
    // bytes (~1s of frames) waiting for a body that never checks out.
    TEST_ASSERT_EQUAL(Link2FrameAssembler::FeedResult::FrameInvalid, assembler.feedByte(0xFF));

    // And a valid frame right after still decodes (resync).
    uint8_t frame[link2::kFrameLen];
    link2::encodeFrame(makeGoldenState(), frame);
    Link2FrameAssembler::FeedResult result = Link2FrameAssembler::FeedResult::Incomplete;
    for (uint8_t b : frame) {
        result = assembler.feedByte(b);
    }
    TEST_ASSERT_EQUAL(Link2FrameAssembler::FeedResult::FrameReady, result);
}

void test_assembler_resyncs_after_corruption_with_start_byte_in_payload() {
    // throttlePercent = -91 encodes as 0xA5 -- the start byte legally appears
    // INSIDE a payload. Corrupt one such frame, then confirm the next valid
    // frame still gets through (false syncs fail CRC and resync).
    VehicleState s = makeGoldenState();
    s.throttlePercent = -91;
    uint8_t corrupt[link2::kFrameLen];
    link2::encodeFrame(s, corrupt);
    corrupt[link2::kFrameLen - 1] ^= 0xFF;

    uint8_t good[link2::kFrameLen];
    link2::encodeFrame(makeGoldenState(), good);

    Link2FrameAssembler assembler;
    for (uint8_t b : corrupt) {
        assembler.feedByte(b);
    }
    Link2FrameAssembler::FeedResult result = Link2FrameAssembler::FeedResult::Incomplete;
    for (uint8_t b : good) {
        result = assembler.feedByte(b);
    }
    // The 0xA5 inside the corrupt payload may cost a false-sync attempt, but
    // the good frame following it must still decode within this stream.
    if (result != Link2FrameAssembler::FeedResult::FrameReady) {
        // Feed one more copy: a false sync can consume the first good frame's
        // prefix; the stream must recover by the next frame at the latest.
        for (uint8_t b : good) {
            result = assembler.feedByte(b);
        }
    }
    TEST_ASSERT_EQUAL(Link2FrameAssembler::FeedResult::FrameReady, result);
    TEST_ASSERT_EQUAL_INT8(42, assembler.lastState().throttlePercent);
}

void test_sender_writes_one_frame() {
    MockByteSink sink;
    Link2Sender sender(sink);

    ControlSnapshot snapshot;
    snapshot.commandedThrottle = 420;
    snapshot.steering = -250;
    snapshot.drsOpen = true;
    snapshot.armed = true;
    snapshot.failsafe = false;
    snapshot.displayGear = 3;
    snapshot.rpm = 1500;
    snapshot.batteryMv = 7900;
    sender.send(snapshot);

    TEST_ASSERT_EQUAL_UINT32(1, sink.writeCount);
    TEST_ASSERT_EQUAL_UINT32(link2::kFrameLen, sink.lastWriteLen);
    // 420/10 = 42, -250/10 = -25: identical to the golden frame.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kGoldenFrame, sink.lastWrite, link2::kFrameLen);
}

void test_sender_braking_hysteresis() {
    MockByteSink sink;
    Link2Sender sender(sink);
    ControlSnapshot snapshot;
    snapshot.failsafe = false;

    auto brakingBit = [&]() { return (sink.lastWrite[5] & link2::kFlagBraking) != 0; };

    snapshot.commandedThrottle = -30; // between thresholds, initial state off
    sender.send(snapshot);
    TEST_ASSERT_FALSE(brakingBit());

    snapshot.commandedThrottle = -41; // below brakeOnBelow: on
    sender.send(snapshot);
    TEST_ASSERT_TRUE(brakingBit());

    snapshot.commandedThrottle = -30; // in the band: holds on
    sender.send(snapshot);
    TEST_ASSERT_TRUE(brakingBit());

    snapshot.commandedThrottle = -20; // at brakeOffAbove: off
    sender.send(snapshot);
    TEST_ASSERT_FALSE(brakingBit());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_golden_frame_bytes);
    RUN_TEST(test_crc_matches_crsf_implementation);
    RUN_TEST(test_encode_decode_roundtrip);
    RUN_TEST(test_each_flag_bit_pinned);
    RUN_TEST(test_decode_rejects_bad_start);
    RUN_TEST(test_decode_rejects_bad_length_and_short_buffer);
    RUN_TEST(test_decode_validation_order_crc_before_version);
    RUN_TEST(test_decode_leaves_out_untouched_on_failure);
    RUN_TEST(test_assembler_frame_byte_by_byte);
    RUN_TEST(test_assembler_hard_rejects_bad_length_byte_immediately);
    RUN_TEST(test_assembler_resyncs_after_corruption_with_start_byte_in_payload);
    RUN_TEST(test_sender_writes_one_frame);
    RUN_TEST(test_sender_braking_hysteresis);
    return UNITY_END();
}
