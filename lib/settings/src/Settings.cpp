#include "settings/Settings.hpp"

#include <cstring>

namespace settings {

uint8_t computeCrc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0xD5)
                                : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}

size_t serialize(const Settings& s, uint8_t out[kBlobLen]) {
    out[0] = kBlobVersion;
    // Both writer and reader are the same firmware build, so a raw struct copy
    // with natural alignment is deterministic; the version byte guards against
    // cross-build layout drift.
    std::memcpy(out + 1, &s, sizeof(Settings));
    out[kBlobLen - 1] = computeCrc8(out, 1 + sizeof(Settings)); // version + struct
    return kBlobLen;
}

bool deserialize(const uint8_t* data, size_t len, Settings& out) {
    if (len != kBlobLen) {
        return false; // first boot (empty), truncated, or wrong build's size
    }
    const uint8_t receivedCrc = data[kBlobLen - 1];
    if (computeCrc8(data, 1 + sizeof(Settings)) != receivedCrc) {
        return false; // corrupt
    }
    if (data[0] != kBlobVersion) {
        return false; // older/newer layout
    }
    Settings candidate;
    std::memcpy(&candidate, data + 1, sizeof(Settings));
    if (!candidate.valid()) {
        return false; // CRC-valid but out-of-range -> never apply
    }
    out = candidate;
    return true;
}

} // namespace settings
