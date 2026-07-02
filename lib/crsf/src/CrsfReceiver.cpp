#include "crsf/CrsfReceiver.hpp"
#include "crsf/CrsfParser.hpp"

namespace crsf {

CrsfReceiver::ByteResult CrsfReceiver::feedByte(uint8_t b, uint32_t nowMs) {
    const CrsfFrameAssembler::FeedResult result = assembler_.feedByte(b);
    if (result == CrsfFrameAssembler::FeedResult::Incomplete) {
        return ByteResult::None;
    }
    if (result == CrsfFrameAssembler::FeedResult::FrameInvalid) {
        return ByteResult::CorruptFrame;
    }

    // FrameReady: CRC-valid frame of some type. Known types are decoded only
    // when the payload length matches their spec exactly; a CRC-valid frame
    // with the wrong length for its claimed type is ignored, not trusted.
    switch (assembler_.lastFrameType()) {
        case kFrameTypeRcChannelsPacked:
            if (assembler_.lastPayloadLen() != kRcChannelsPayloadLen) {
                return ByteResult::None;
            }
            unpackChannels(assembler_.lastPayload(), channels_.channels);
            everRcFrame_ = true;
            lastRcFrameMs_ = nowMs;
            return ByteResult::NewRcFrame;

        case kFrameTypeLinkStatistics:
            if (assembler_.lastPayloadLen() != kLinkStatisticsPayloadLen) {
                return ByteResult::None;
            }
            decodeLinkStatistics(assembler_.lastPayload(), linkStats_);
            everLinkStats_ = true;
            return ByteResult::NewLinkStats;

        default:
            // Other telemetry types (GPS, battery, ...) are valid CRSF
            // traffic; CRC-checked and silently ignored here.
            return ByteResult::None;
    }
}

} // namespace crsf
