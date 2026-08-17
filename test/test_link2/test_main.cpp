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
    s.ersDeploying = true;
    s.gear = 3;
    s.rpm = 1500;
    s.batteryMv = 7900;
    s.ersPercent = 60;
    s.driveMode = 2;
    s.soundProfile = link2::kSoundProfileV6Hybrid; // non-default: pins the byte on the wire
    s.volume = 80;                                 // == kDefaultVolume, spelled as a value
    return s;
}

// The exact on-wire bytes for makeGoldenState(), mirrored in
// docs/link2_protocol.md's worked example. If this test breaks, the protocol
// changed and the doc + board #2 must change with it.
const uint8_t kGoldenFrame[link2::kFrameLen] = {
    0xA5,             // start
    0x0E,             // length 14
    0x02,             // version
    0x2A,             // throttlePercent = +42
    0xE7,             // steeringPercent = -25
    0x4C,             // flags: drsOpen | armed | ersDeploying
    0x03,             // gear 3
    0xDC, 0x05,       // rpm = 1500 LE
    0xDC, 0x1E,       // batteryMv = 7900 LE
    0x3C,             // ersPercent = 60
    0x02,             // driveMode = Gearbox+ERS
    0x01,             // soundProfile = V6 turbo-hybrid
    0x50,             // volume = 80
    0x00,             // modeFlags: both reserved bits 0 (always, today)
    0x5A,             // crc8 over [length + payload]
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
    const uint8_t data[] = {0x0E, 0x02, 0x2A, 0xE7, 0x4C, 0x03, 0xDC, 0x05,
                            0xDC, 0x1E, 0x3C, 0x02, 0x01, 0x50, 0x00};
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
    in.ersDeploying = true;
    in.gear = 6;
    in.rpm = 65535;
    in.batteryMv = 8400;
    in.ersPercent = 0;
    in.driveMode = 0;
    in.soundProfile = link2::kSoundProfileV6Hybrid;
    in.volume = 0; // the true-silence extreme must survive the round trip
    in.showcase = true;           // reserved bits must still round-trip so the
    in.awaitingController = true; // future modes inherit a proven wire path

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
    TEST_ASSERT_EQUAL(in.ersDeploying, out.ersDeploying);
    TEST_ASSERT_EQUAL_UINT8(in.gear, out.gear);
    TEST_ASSERT_EQUAL_UINT16(in.rpm, out.rpm);
    TEST_ASSERT_EQUAL_UINT16(in.batteryMv, out.batteryMv);
    TEST_ASSERT_EQUAL_UINT8(in.ersPercent, out.ersPercent);
    TEST_ASSERT_EQUAL_UINT8(in.driveMode, out.driveMode);
    TEST_ASSERT_EQUAL_UINT8(in.soundProfile, out.soundProfile);
    TEST_ASSERT_EQUAL_UINT8(in.volume, out.volume);
    TEST_ASSERT_EQUAL(in.showcase, out.showcase);
    TEST_ASSERT_EQUAL(in.awaitingController, out.awaitingController);
}

// A default-constructed VehicleState must put the DOCUMENTED defaults on the
// wire: soundProfile 0 (V10) and volume kDefaultVolume (80, loud-but-not-max).
// Pinning the bytes here keeps the code defaults, the doc, and board #2's
// expectations from drifting apart.
void test_default_sound_fields_on_wire() {
    const VehicleState defaults;
    uint8_t frame[link2::kFrameLen];
    link2::encodeFrame(defaults, frame);

    TEST_ASSERT_EQUAL_HEX8(link2::kSoundProfileV10, frame[13]);
    TEST_ASSERT_EQUAL_HEX8(link2::kDefaultVolume, frame[14]);
    TEST_ASSERT_EQUAL_HEX8(0x50, frame[14]); // 80, spelled as the wire byte
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[15]); // modeFlags: all-zero by default

    VehicleState out;
    out.soundProfile = 0xEE; // sentinels: decode must overwrite both
    out.volume = 0xEE;
    TEST_ASSERT_EQUAL(DecodeResult::Ok, link2::decodeFrame(frame, sizeof(frame), out));
    TEST_ASSERT_EQUAL_UINT8(link2::kSoundProfileV10, out.soundProfile);
    TEST_ASSERT_EQUAL_UINT8(link2::kDefaultVolume, out.volume);
}

// modeFlags bit positions pinned, and the NORMAL-MODE invariant that must
// outlive the showcase wave: a DRIVE-boot sender (snapshot.showcase false,
// which is the field's default) transmits modeFlags 0x00 in EVERY frame --
// disarmed, driving, or failsafe. Showcase lights bit0 through the sender
// (its own test below); awaitingController still has NO production path at
// all (ControlSnapshot carries no such field), so bit1 cannot be emitted by
// this firmware until the BT mode ships.
void test_mode_flags_bits_pinned_and_normal_mode_always_zero() {
    TEST_ASSERT_EQUAL_HEX8(0x01, link2::kModeFlagShowcase);
    TEST_ASSERT_EQUAL_HEX8(0x02, link2::kModeFlagAwaitingController);

    uint8_t frame[link2::kFrameLen];
    VehicleState s = makeGoldenState(); // showcase/awaitingController untouched
    link2::encodeFrame(s, frame);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[15]);

    s.showcase = true;
    link2::encodeFrame(s, frame);
    TEST_ASSERT_EQUAL_HEX8(0x01, frame[15]);
    s.awaitingController = true;
    link2::encodeFrame(s, frame);
    TEST_ASSERT_EQUAL_HEX8(0x03, frame[15]);

    // The PRODUCTION normal-mode path: a default (DRIVE) snapshot emits
    // modeFlags 0x00 -- this pin predates showcase and MUST hold forever.
    MockByteSink sink;
    Link2Sender sender(sink);
    sender.send(ControlSnapshot{});
    TEST_ASSERT_EQUAL_HEX8(0x00, sink.lastWrite[15]);

    // Same in a DRIVE failsafe frame (the Safe-branch snapshot shape).
    ControlSnapshot safe;
    safe.failsafe = true;
    safe.armed = false;
    safe.commandedThrottle = 0;
    sender.send(safe);
    TEST_ASSERT_EQUAL_HEX8(0x00, sink.lastWrite[15]);

    // And across a full DRIVE drive cycle: disarmed -> armed+throttle ->
    // failsafe -> recovery. No normal-mode snapshot can light any mode bit.
    ControlSnapshot drive;
    drive.failsafe = false;
    for (int step = 0; step < 4; ++step) {
        drive.armed = (step == 1);
        drive.commandedThrottle = (step == 1) ? 700 : 0;
        drive.failsafe = (step == 2);
        sender.send(drive);
        TEST_ASSERT_EQUAL_HEX8(0x00, sink.lastWrite[15]);
    }
}

// ADDITIONAL golden pin for the showcase wave (the normal-mode golden frame
// above is deliberately untouched): the same golden state in a SHOWCASE
// boot differs in EXACTLY two bytes -- modeFlags 0x01 and the CRC. Mirrored
// in docs/link2_protocol.md's worked-example note.
const uint8_t kShowcaseGoldenFrame[link2::kFrameLen] = {
    0xA5, 0x0E, 0x02, 0x2A, 0xE7, 0x4C, 0x03, 0xDC, 0x05,
    0xDC, 0x1E, 0x3C, 0x02, 0x01, 0x50, 0x01, 0x8F,
};

void test_showcase_golden_frame_bytes() {
    VehicleState s = makeGoldenState();
    s.showcase = true;
    uint8_t frame[link2::kFrameLen];
    TEST_ASSERT_EQUAL_UINT32(link2::kFrameLen, link2::encodeFrame(s, frame));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kShowcaseGoldenFrame, frame, link2::kFrameLen);

    // Two-byte delta, pinned byte-for-byte against the normal golden frame.
    for (size_t i = 0; i < link2::kFrameLen; ++i) {
        if (i == 15 || i == 16) continue;
        TEST_ASSERT_EQUAL_HEX8(kGoldenFrame[i], kShowcaseGoldenFrame[i]);
    }
}

// The production SHOWCASE path: snapshot.showcase (set once at boot from
// the resolved mode) lights bit0 -- and ONLY bit0 -- in every frame,
// including failsafe frames (boot state rides the wire under all
// conditions), while armed/throttle stay truthful (a showcase frame can
// never read as an armed car).
void test_sender_stamps_showcase_bit() {
    MockByteSink sink;
    Link2Sender sender(sink);

    ControlSnapshot snapshot; // disarmed idle, showcase boot
    snapshot.showcase = true;
    snapshot.failsafe = false;
    sender.send(snapshot);
    TEST_ASSERT_EQUAL_HEX8(0x01, sink.lastWrite[15]); // bit0 only, exactly
    TEST_ASSERT_EQUAL_HEX8(0x00, sink.lastWrite[3]);  // throttle truthfully 0
    TEST_ASSERT_FALSE((sink.lastWrite[5] & link2::kFlagArmed) != 0);

    // Failsafe frame in a showcase boot: bit0 still set (D4 decides the
    // failsafe FLAG upstream in main.cpp; the sender just reports both).
    snapshot.failsafe = true;
    sender.send(snapshot);
    TEST_ASSERT_EQUAL_HEX8(0x01, sink.lastWrite[15]);
    TEST_ASSERT_TRUE((sink.lastWrite[5] & link2::kFlagFailsafe) != 0);
}

// Receiver rule for the spare bits 2-7: mask and IGNORE, never reject -- a
// frame from a future sender that uses a spare bit still decodes cleanly on
// this firmware, and the unknown bits simply vanish (exactly the flags-byte
// bit7 discipline).
void test_mode_flags_spare_bits_ignored_never_rejected() {
    uint8_t frame[link2::kFrameLen];
    link2::encodeFrame(makeGoldenState(), frame);

    frame[15] = 0xFC; // ONLY spare bits set
    frame[link2::kFrameLen - 1] = link2::computeCrc8(frame + 1, 1 + link2::kPayloadLen);
    VehicleState out;
    TEST_ASSERT_EQUAL(DecodeResult::Ok, link2::decodeFrame(frame, sizeof(frame), out));
    TEST_ASSERT_FALSE(out.showcase);
    TEST_ASSERT_FALSE(out.awaitingController);

    frame[15] = 0xFF; // spares AND both named bits
    frame[link2::kFrameLen - 1] = link2::computeCrc8(frame + 1, 1 + link2::kPayloadLen);
    TEST_ASSERT_EQUAL(DecodeResult::Ok, link2::decodeFrame(frame, sizeof(frame), out));
    TEST_ASSERT_TRUE(out.showcase);
    TEST_ASSERT_TRUE(out.awaitingController);
}

// The decoder is a RAW pass-through for soundProfile/volume (like driveMode):
// reserved profile values and volumes past kVolumeMax must arrive intact so
// the CONSUMER can apply the documented fallback/clamp -- a codec that
// silently rewrote them would hide sender bugs from board #2's tests.
void test_reserved_sound_values_pass_through_raw() {
    VehicleState in = makeGoldenState();
    in.soundProfile = link2::kSoundProfileCount; // first reserved value
    in.volume = 101;                             // just past kVolumeMax

    uint8_t frame[link2::kFrameLen];
    link2::encodeFrame(in, frame);
    VehicleState out;
    TEST_ASSERT_EQUAL(DecodeResult::Ok, link2::decodeFrame(frame, sizeof(frame), out));
    TEST_ASSERT_EQUAL_UINT8(link2::kSoundProfileCount, out.soundProfile);
    TEST_ASSERT_EQUAL_UINT8(101, out.volume);
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
        {&VehicleState::ersDeploying, link2::kFlagErsDeploying},
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
    frame[2] = 3;
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

// The coordinated-flash compatibility statement, pinned from the v2 side: a
// well-formed v1 frame (length 0x0B, version 1, correct CRC) must be HARD-
// REJECTED -- by decodeFrame on length, and by the assembler the moment the
// length byte arrives. A v1/v2-mismatched pair of boards therefore never
// exchanges a frame: the receiver sits in its 500 ms staleness failsafe
// until BOTH boards are flashed together (docs/link2_protocol.md, v1 -> v2).
void test_v1_frame_hard_rejected() {
    // A byte-faithful v1 frame: the v1 golden example from the old doc.
    const uint8_t v1Frame[14] = {0xA5, 0x0B, 0x01, 0x2A, 0xE7, 0x4C, 0x03,
                                 0xDC, 0x05, 0xDC, 0x1E, 0x3C, 0x02, 0xCE};
    // Its CRC really is valid (this is a well-formed v1 frame, not garbage):
    TEST_ASSERT_EQUAL_HEX8(v1Frame[13], link2::computeCrc8(v1Frame + 1, 12));

    VehicleState out;
    TEST_ASSERT_EQUAL(DecodeResult::BadLength, link2::decodeFrame(v1Frame, sizeof(v1Frame), out));

    Link2FrameAssembler assembler;
    TEST_ASSERT_EQUAL(Link2FrameAssembler::FeedResult::Incomplete,
                      assembler.feedByte(v1Frame[0]));
    // Rejected AT the length byte -- no v1 body byte is ever buffered.
    TEST_ASSERT_EQUAL(Link2FrameAssembler::FeedResult::FrameInvalid,
                      assembler.feedByte(v1Frame[1]));

    // The 13-byte-payload v2 DRAFT (pre-modeFlags, never flashed) dies the
    // same way: the shipped v2 is the 14-byte-payload form and nothing else.
    TEST_ASSERT_EQUAL(Link2FrameAssembler::FeedResult::Incomplete,
                      assembler.feedByte(link2::kStartByte));
    TEST_ASSERT_EQUAL(Link2FrameAssembler::FeedResult::FrameInvalid, assembler.feedByte(0x0D));
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
    snapshot.ersDeploying = true;
    snapshot.displayGear = 3;
    snapshot.rpm = 1500;
    snapshot.batteryMv = 7900;
    snapshot.ersPercent = 60;
    snapshot.driveMode = 2;
    sender.send(snapshot);

    TEST_ASSERT_EQUAL_UINT32(1, sink.writeCount);
    TEST_ASSERT_EQUAL_UINT32(link2::kFrameLen, sink.lastWriteLen);
    // 420/10 = 42, -250/10 = -25: the golden state, except the sender was
    // given no sound config, so the wire carries the DEFAULTS (V10, volume
    // 80) rather than the golden frame's V6 byte.
    VehicleState expected = makeGoldenState();
    expected.soundProfile = link2::kSoundProfileV10;
    expected.volume = link2::kDefaultVolume;
    uint8_t expectedFrame[link2::kFrameLen];
    link2::encodeFrame(expected, expectedFrame);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedFrame, sink.lastWrite, link2::kFrameLen);
}

// setSoundConfig() closes the settings -> wire loop: with the golden frame's
// sound pair applied (V6, volume 80), the same snapshot as
// test_sender_writes_one_frame now produces the golden bytes EXACTLY.
void test_sender_sound_config_stamped() {
    MockByteSink sink;
    Link2Sender sender(sink);

    link2::SoundConfig sound;
    sound.profile = link2::kSoundProfileV6Hybrid;
    sound.volume = 80;
    TEST_ASSERT_TRUE(sound.valid());
    sender.setSoundConfig(sound);

    ControlSnapshot snapshot;
    snapshot.commandedThrottle = 420;
    snapshot.steering = -250;
    snapshot.drsOpen = true;
    snapshot.armed = true;
    snapshot.failsafe = false;
    snapshot.ersDeploying = true;
    snapshot.displayGear = 3;
    snapshot.rpm = 1500;
    snapshot.batteryMv = 7900;
    snapshot.ersPercent = 60;
    snapshot.driveMode = 2;
    sender.send(snapshot);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(kGoldenFrame, sink.lastWrite, link2::kFrameLen);

    // Configuration, not state: the same sound pair rides a FAILSAFE frame
    // unchanged (board #2's own failsafe silencing wins over volume there).
    snapshot.failsafe = true;
    snapshot.armed = false;
    snapshot.commandedThrottle = 0;
    sender.send(snapshot);
    TEST_ASSERT_EQUAL_HEX8(0x01, sink.lastWrite[13]); // soundProfile still V6
    TEST_ASSERT_EQUAL_HEX8(0x50, sink.lastWrite[14]); // volume still 80
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
    RUN_TEST(test_default_sound_fields_on_wire);
    RUN_TEST(test_mode_flags_bits_pinned_and_normal_mode_always_zero);
    RUN_TEST(test_showcase_golden_frame_bytes);
    RUN_TEST(test_sender_stamps_showcase_bit);
    RUN_TEST(test_mode_flags_spare_bits_ignored_never_rejected);
    RUN_TEST(test_reserved_sound_values_pass_through_raw);
    RUN_TEST(test_each_flag_bit_pinned);
    RUN_TEST(test_decode_rejects_bad_start);
    RUN_TEST(test_decode_rejects_bad_length_and_short_buffer);
    RUN_TEST(test_decode_validation_order_crc_before_version);
    RUN_TEST(test_decode_leaves_out_untouched_on_failure);
    RUN_TEST(test_assembler_frame_byte_by_byte);
    RUN_TEST(test_v1_frame_hard_rejected);
    RUN_TEST(test_assembler_hard_rejects_bad_length_byte_immediately);
    RUN_TEST(test_assembler_resyncs_after_corruption_with_start_byte_in_payload);
    RUN_TEST(test_sender_writes_one_frame);
    RUN_TEST(test_sender_sound_config_stamped);
    RUN_TEST(test_sender_braking_hysteresis);
    return UNITY_END();
}
