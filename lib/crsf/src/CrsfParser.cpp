#include "crsf/CrsfParser.hpp"

namespace crsf {

uint8_t computeCrc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ kCrc8Poly)
                                : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}

void unpackChannels(const uint8_t payload[kRcChannelsPayloadLen],
                     uint16_t outChannels[kNumChannels]) {
    for (size_t ch = 0; ch < kNumChannels; ++ch) {
        const size_t bitPos = ch * 11;
        const size_t byteIdx = bitPos / 8;
        const size_t bitOffset = bitPos % 8;

        // Build up to a 3-byte little-endian window starting at byteIdx; the
        // top byte is only fetched when in bounds (channel 15's 11 bits fit
        // entirely within 2 bytes, so byteIdx+2 would be out of range there).
        uint32_t chunk = static_cast<uint32_t>(payload[byteIdx]);
        if (byteIdx + 1 < kRcChannelsPayloadLen) {
            chunk |= static_cast<uint32_t>(payload[byteIdx + 1]) << 8;
        }
        if (byteIdx + 2 < kRcChannelsPayloadLen) {
            chunk |= static_cast<uint32_t>(payload[byteIdx + 2]) << 16;
        }

        outChannels[ch] = static_cast<uint16_t>((chunk >> bitOffset) & 0x07FFu);
    }
}

void decodeLinkStatistics(const uint8_t payload[kLinkStatisticsPayloadLen],
                           CrsfLinkStatistics& out) {
    out.uplinkRssiAnt1 = payload[0];
    out.uplinkRssiAnt2 = payload[1];
    out.uplinkLinkQuality = payload[2];
    out.uplinkSnr = static_cast<int8_t>(payload[3]);
    out.activeAntenna = payload[4];
    out.rfMode = payload[5];
    out.uplinkTxPower = payload[6];
    out.downlinkRssi = payload[7];
    out.downlinkLinkQuality = payload[8];
    out.downlinkSnr = static_cast<int8_t>(payload[9]);
}

DecodeResult decodeFrame(const uint8_t* frame, size_t frameLen, RcChannelsFrame& out) {
    if (frameLen < 4) {
        return DecodeResult::BadLength;
    }
    if (frame[0] != kSyncByte) {
        return DecodeResult::BadSync;
    }

    const uint8_t length = frame[1];
    if (length < 2) {
        return DecodeResult::BadLength;
    }
    if (frameLen != 2 + static_cast<size_t>(length)) {
        return DecodeResult::BadLength;
    }

    const uint8_t type = frame[2];
    if (type != kFrameTypeRcChannelsPacked) {
        return DecodeResult::UnsupportedType;
    }
    if (length != kRcChannelsLengthByte) {
        return DecodeResult::BadLength;
    }

    const uint8_t* payload = frame + 3;
    const uint8_t receivedCrc = frame[3 + kRcChannelsPayloadLen];
    const uint8_t computedCrc = computeCrc8(frame + 2, 1 + kRcChannelsPayloadLen);
    if (computedCrc != receivedCrc) {
        return DecodeResult::CrcMismatch;
    }

    unpackChannels(payload, out.channels);
    return DecodeResult::Ok;
}

} // namespace crsf
