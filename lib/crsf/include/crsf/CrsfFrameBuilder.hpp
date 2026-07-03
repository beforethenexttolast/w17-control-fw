#pragma once

#include <cstring>

#include "crsf/CrsfFrame.hpp"
#include "crsf/CrsfParser.hpp"

// Test/simulation support: builds valid CRSF frames (the inverse of the
// parser). Used by the native unit tests and the Wokwi sim feeder; the
// production firmware only ever parses. Header-only and pure.

namespace crsf {

// Inverse of unpackChannels: packs 16 raw 11-bit channel values LSB-first
// into the 22-byte RC_CHANNELS_PACKED payload.
inline void packChannels(const uint16_t channels[kNumChannels],
                          uint8_t payload[kRcChannelsPayloadLen]) {
    std::memset(payload, 0, kRcChannelsPayloadLen);
    for (size_t ch = 0; ch < kNumChannels; ++ch) {
        const size_t bitPos = ch * 11;
        const uint16_t value = channels[ch] & 0x07FF;
        for (int bit = 0; bit < 11; ++bit) {
            if ((value & (1u << bit)) == 0) {
                continue;
            }
            const size_t overallBit = bitPos + static_cast<size_t>(bit);
            payload[overallBit / 8] |= static_cast<uint8_t>(1u << (overallBit % 8));
        }
    }
}

// Builds a complete, CRC-valid CRSF frame of any type. `outFrame` must hold
// at least 4 + payloadLen bytes. Returns the total frame length.
inline size_t buildFrame(uint8_t type, const uint8_t* payload, uint8_t payloadLen,
                          uint8_t* outFrame) {
    outFrame[0] = kSyncByte;
    outFrame[1] = static_cast<uint8_t>(payloadLen + 2); // type + payload + crc
    outFrame[2] = type;
    std::memcpy(outFrame + 3, payload, payloadLen);
    outFrame[3 + payloadLen] = computeCrc8(outFrame + 2, 1 + payloadLen);
    return 4 + static_cast<size_t>(payloadLen);
}

// Builds a complete RC_CHANNELS_PACKED frame (kRcChannelsFrameLen bytes).
inline size_t buildRcChannelsFrame(const uint16_t channels[kNumChannels], uint8_t* outFrame) {
    uint8_t payload[kRcChannelsPayloadLen];
    packChannels(channels, payload);
    return buildFrame(kFrameTypeRcChannelsPacked, payload, kRcChannelsPayloadLen, outFrame);
}

// Builds a complete LINK_STATISTICS frame (14 bytes).
inline size_t buildLinkStatisticsFrame(const CrsfLinkStatistics& stats, uint8_t* outFrame) {
    const uint8_t payload[kLinkStatisticsPayloadLen] = {
        stats.uplinkRssiAnt1,
        stats.uplinkRssiAnt2,
        stats.uplinkLinkQuality,
        static_cast<uint8_t>(stats.uplinkSnr),
        stats.activeAntenna,
        stats.rfMode,
        stats.uplinkTxPower,
        stats.downlinkRssi,
        stats.downlinkLinkQuality,
        static_cast<uint8_t>(stats.downlinkSnr),
    };
    return buildFrame(kFrameTypeLinkStatistics, payload, kLinkStatisticsPayloadLen, outFrame);
}

// Builds a complete CRSF BATTERY_SENSOR frame (0x08). Payload is big-endian
// per the CRSF spec: voltage (decivolts), current (deciamps), capacity used
// (mAh, 24-bit), remaining percent. `outFrame` needs >= 4 + 8 bytes.
inline size_t buildBatteryFrame(uint16_t voltageDeciVolt, uint16_t currentDeciAmp,
                                 uint32_t capacityMah, uint8_t remainingPct, uint8_t* outFrame) {
    const uint8_t payload[kBatteryPayloadLen] = {
        static_cast<uint8_t>(voltageDeciVolt >> 8), static_cast<uint8_t>(voltageDeciVolt & 0xFF),
        static_cast<uint8_t>(currentDeciAmp >> 8), static_cast<uint8_t>(currentDeciAmp & 0xFF),
        static_cast<uint8_t>((capacityMah >> 16) & 0xFF), static_cast<uint8_t>((capacityMah >> 8) & 0xFF),
        static_cast<uint8_t>(capacityMah & 0xFF), remainingPct,
    };
    return buildFrame(kFrameTypeBattery, payload, kBatteryPayloadLen, outFrame);
}

// Builds a standard CRSF GPS frame (0x02). All fields big-endian. We only
// populate groundspeed (0.1 km/h units) with real wheel speed; lat/lon/heading/
// sats are 0 and altitude is the 1000 = 0 m baseline. `outFrame` needs >= 4+15.
inline size_t buildGpsFrame(int32_t latitude, int32_t longitude, uint16_t groundspeedKmhX10,
                            uint16_t headingDegX100, uint16_t altitudeMPlus1000,
                            uint8_t satellites, uint8_t* outFrame) {
    const uint32_t lat = static_cast<uint32_t>(latitude);
    const uint32_t lon = static_cast<uint32_t>(longitude);
    const uint8_t payload[kGpsPayloadLen] = {
        static_cast<uint8_t>((lat >> 24) & 0xFF), static_cast<uint8_t>((lat >> 16) & 0xFF),
        static_cast<uint8_t>((lat >> 8) & 0xFF),  static_cast<uint8_t>(lat & 0xFF),
        static_cast<uint8_t>((lon >> 24) & 0xFF), static_cast<uint8_t>((lon >> 16) & 0xFF),
        static_cast<uint8_t>((lon >> 8) & 0xFF),  static_cast<uint8_t>(lon & 0xFF),
        static_cast<uint8_t>(groundspeedKmhX10 >> 8), static_cast<uint8_t>(groundspeedKmhX10 & 0xFF),
        static_cast<uint8_t>(headingDegX100 >> 8), static_cast<uint8_t>(headingDegX100 & 0xFF),
        static_cast<uint8_t>(altitudeMPlus1000 >> 8), static_cast<uint8_t>(altitudeMPlus1000 & 0xFF),
        satellites,
    };
    return buildFrame(kFrameTypeGps, payload, kGpsPayloadLen, outFrame);
}

// Builds a CRSF FLIGHTMODE frame (0x21) from a NUL-terminated ASCII string.
// The NUL terminator is included in the payload (matching CRSF/Betaflight).
// `text` is truncated to kFlightModeMaxLen-1 chars. `outFrame` needs
// >= 4 + strlen(text) + 1 bytes.
inline size_t buildFlightModeFrame(const char* text, uint8_t* outFrame) {
    uint8_t payload[kFlightModeMaxLen];
    size_t len = 0;
    while (text[len] != '\0' && len < kFlightModeMaxLen - 1) {
        payload[len] = static_cast<uint8_t>(text[len]);
        ++len;
    }
    payload[len++] = 0; // NUL terminator, counted in the payload
    return buildFrame(kFrameTypeFlightMode, payload, static_cast<uint8_t>(len), outFrame);
}

} // namespace crsf
