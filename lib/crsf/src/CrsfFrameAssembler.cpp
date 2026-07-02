#include "crsf/CrsfFrameAssembler.hpp"
#include "crsf/CrsfParser.hpp"

namespace crsf {

CrsfFrameAssembler::FeedResult CrsfFrameAssembler::feedByte(uint8_t b) {
    switch (state_) {
        case State::WaitingForSync:
            if (b != kSyncByte) {
                return FeedResult::Incomplete;
            }
            buffer_[0] = b;
            bufferLen_ = 1;
            state_ = State::ReadingLength;
            return FeedResult::Incomplete;

        case State::ReadingLength: {
            buffer_[1] = b;
            bufferLen_ = 2;
            expectedLength_ = b;
            const size_t totalFrameLen = 2 + static_cast<size_t>(expectedLength_);
            if (expectedLength_ < 2 || totalFrameLen > kMaxFrameLen) {
                reset();
                return FeedResult::FrameInvalid;
            }
            state_ = State::ReadingPayload;
            return FeedResult::Incomplete;
        }

        case State::ReadingPayload: {
            buffer_[bufferLen_++] = b;
            const size_t totalFrameLen = 2 + static_cast<size_t>(expectedLength_);
            if (bufferLen_ < totalFrameLen) {
                return FeedResult::Incomplete;
            }
            const DecodeResult result = decodeFrame(buffer_, bufferLen_, lastFrame_);
            reset();
            return result == DecodeResult::Ok ? FeedResult::FrameReady : FeedResult::FrameInvalid;
        }
    }
    return FeedResult::Incomplete; // unreachable
}

void CrsfFrameAssembler::reset() {
    state_ = State::WaitingForSync;
    bufferLen_ = 0;
    expectedLength_ = 0;
}

} // namespace crsf
