#pragma once

#include <cstddef>
#include <cstdint>

namespace hal {

// Abstract persistent blob store (one opaque byte blob). The esp32 impl backs
// it with NVS/Preferences; the native test impl is in-memory. The settings
// blob format (version + CRC) lives above this seam, so the store is dumb.
class ISettingsStore {
public:
    virtual ~ISettingsStore() = default;

    // Reads the stored blob into buf (capacity `cap`); sets `len` to the
    // number of bytes read. Returns false if nothing is stored (first boot)
    // or on error -- caller then keeps compile-time defaults.
    virtual bool load(uint8_t* buf, size_t cap, size_t& len) = 0;

    // Persists `len` bytes. Returns false on error. Called only on an explicit
    // `save` (NVS wear is a non-issue at that cadence).
    virtual bool save(const uint8_t* buf, size_t len) = 0;
};

} // namespace hal
