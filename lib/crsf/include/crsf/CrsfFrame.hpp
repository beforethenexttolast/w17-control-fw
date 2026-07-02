#pragma once

#include <cstddef>
#include <cstdint>

// CRSF (Crossfire) protocol constants and the RC_CHANNELS_PACKED frame layout,
// as used by ExpressLRS / TBS Crossfire receivers (e.g. RadioMaster RP1).
//
// Frame layout: [sync 0xC8][length][type][payload...][crc8]
//   - `length` counts everything after itself: type + payload + crc.
//   - CRC8 (poly 0xD5) is computed over [type + payload] only -- not the sync
//     byte, not the length byte, not the crc byte itself.
//
// Source: CRSF / ExpressLRS protocol documentation; see CLAUDE.md section 2.1.

namespace crsf {

// Frame sync/address byte used by CRSF receivers when talking to a flight
// controller / companion MCU.
inline constexpr uint8_t kSyncByte = 0xC8;

// RC_CHANNELS_PACKED frame type: 16 x 11-bit channels.
inline constexpr uint8_t kFrameTypeRcChannelsPacked = 0x16;

// CRSF CRC8 polynomial (DVB-S2 style), per CRSF spec.
inline constexpr uint8_t kCrc8Poly = 0xD5;

// 16 channels * 11 bits = 176 bits = 22 bytes.
inline constexpr size_t kRcChannelsPayloadLen = 22;
inline constexpr size_t kNumChannels = 16;

// `length` byte value for an RC_CHANNELS_PACKED frame: type(1) + payload(22) + crc(1).
inline constexpr uint8_t kRcChannelsLengthByte =
    1 + static_cast<uint8_t>(kRcChannelsPayloadLen) + 1;

// Total on-wire frame size for RC_CHANNELS_PACKED: sync(1) + length(1) + length byte's count.
inline constexpr size_t kRcChannelsFrameLen = 2 + kRcChannelsLengthByte;

// Raw 11-bit channel value range, per CRSF spec ("172 = -100%", "992 = 0%", "1811 = +100%").
inline constexpr uint16_t kChannelRawMin = 172;
inline constexpr uint16_t kChannelRawCenter = 992;
inline constexpr uint16_t kChannelRawMax = 1811;

// CRSF UART rate for ELRS receivers, 8N1, not inverted. See CLAUDE.md section 1.
inline constexpr uint32_t kCrsfBaud = 420000;

struct RcChannelsFrame {
    uint16_t channels[kNumChannels];
};

enum class DecodeResult : uint8_t {
    Ok,
    BadSync,         // frame[0] != kSyncByte
    BadLength,       // length byte/frame size inconsistent with RC_CHANNELS_PACKED
    UnsupportedType, // type byte != kFrameTypeRcChannelsPacked
    CrcMismatch,     // computed CRC8 != received CRC8 byte
};

} // namespace crsf
