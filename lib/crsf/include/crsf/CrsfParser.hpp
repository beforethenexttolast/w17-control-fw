#pragma once

#include "crsf/CrsfFrame.hpp"

namespace crsf {

// Computes the CRSF CRC8 (poly kCrc8Poly, bit-by-bit MSB-first, no lookup
// table -- favors readability over speed here) over `data[0..len)`.
uint8_t computeCrc8(const uint8_t* data, size_t len);

// Decodes a single, complete CRSF frame already isolated from the byte stream
// (the caller -- CrsfFrameAssembler in production, or a canned array in
// tests -- is responsible for locating sync..crc in the buffer).
//
// Expected layout: frame[0]=sync, frame[1]=length, frame[2]=type,
// frame[3..3+payloadLen)=payload, frame[3+payloadLen]=crc8.
//
// Pure function: no I/O, no global state, deterministic given its inputs.
// On any failure, `out` is left untouched.
DecodeResult decodeFrame(const uint8_t* frame, size_t frameLen, RcChannelsFrame& out);

// Unpacks the 22-byte RC_CHANNELS_PACKED payload into 16 raw 11-bit channel
// values. The payload is a little-endian bitstream: channel 0 occupies bits
// [0..10], channel 1 bits [11..21], etc. (payload[0] bit 0 is overall bit 0).
void unpackChannels(const uint8_t payload[kRcChannelsPayloadLen],
                     uint16_t outChannels[kNumChannels]);

} // namespace crsf
