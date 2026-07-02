#pragma once

#include "crsf/CrsfFrame.hpp"

namespace crsf {

// Finds CRSF frame boundaries in a raw byte stream and CRC-validates them.
// Type-agnostic: FrameReady means "a CRC-valid frame of ANY type" -- ELRS
// receivers interleave LINK_STATISTICS (and other telemetry) with RC frames,
// and those are valid traffic, not corruption. Typed payload decoding is the
// caller's job (see CrsfReceiver).
//
// Pure C++, no hardware dependency: the caller reads bytes off the UART and
// feeds them here; this class only frames + CRC-checks them.
//
// On any framing/CRC failure the assembler resets and resynchronizes on the
// next sync byte it sees, rather than getting stuck.
class CrsfFrameAssembler {
public:
    enum class FeedResult : uint8_t {
        Incomplete,  // frame not complete yet, keep feeding bytes
        FrameReady,  // a complete, CRC-valid frame is available via the accessors below
        FrameInvalid // a frame boundary was found but failed length/CRC checks
    };

    // Feed one raw byte as it arrives from the UART.
    FeedResult feedByte(uint8_t b);

    // The three accessors below are valid only immediately after feedByte()
    // returns FrameReady, and only until the next feedByte() call --
    // lastPayload() points into the internal buffer, which the next byte
    // starts overwriting. Copy out anything you need to keep.
    uint8_t lastFrameType() const { return lastFrameType_; }
    const uint8_t* lastPayload() const { return buffer_ + 3; }
    uint8_t lastPayloadLen() const { return lastPayloadLen_; }

private:
    enum class State : uint8_t { WaitingForSync, ReadingLength, ReadingPayload };

    // CRSF spec caps a single frame at 64 bytes on the wire.
    static constexpr size_t kMaxFrameLen = 64;

    void reset();

    State state_ = State::WaitingForSync;
    uint8_t buffer_[kMaxFrameLen] = {};
    size_t bufferLen_ = 0;
    uint8_t expectedLength_ = 0; // frame[1]; total frame size = 2 + expectedLength_
    // Snapshotted before reset() so they survive until the next feedByte().
    uint8_t lastFrameType_ = 0;
    uint8_t lastPayloadLen_ = 0;
};

} // namespace crsf
