#pragma once

#include "crsf/CrsfFrame.hpp"

namespace crsf {

// Finds CRSF frame boundaries in a raw byte stream and CRC-validates them.
// Pure C++ (no Arduino/hardware dependency) -- the caller is responsible for
// actually reading bytes off the UART; this class only assembles + decodes.
//
// On any framing/CRC failure, the assembler resets and resynchronizes on the
// next sync byte it sees, rather than getting stuck.
class CrsfFrameAssembler {
public:
    enum class FeedResult : uint8_t {
        Incomplete,  // frame not complete yet, keep feeding bytes
        FrameReady,  // a complete, CRC-valid frame is available via lastFrame()
        FrameInvalid // a frame boundary was found but failed CRC/type/length checks
    };

    // Feed one raw byte as it arrives from the UART.
    FeedResult feedByte(uint8_t b);

    // Valid only immediately after feedByte() returns FrameReady.
    const RcChannelsFrame& lastFrame() const { return lastFrame_; }

private:
    enum class State : uint8_t { WaitingForSync, ReadingLength, ReadingPayload };

    // CRSF spec caps a single frame at 64 bytes; comfortably covers
    // RC_CHANNELS_PACKED's 26-byte frame with headroom for other frame types.
    static constexpr size_t kMaxFrameLen = 64;

    void reset();

    State state_ = State::WaitingForSync;
    uint8_t buffer_[kMaxFrameLen] = {};
    size_t bufferLen_ = 0;
    uint8_t expectedLength_ = 0; // frame[1]; total frame size = 2 + expectedLength_
    RcChannelsFrame lastFrame_{};
};

} // namespace crsf
