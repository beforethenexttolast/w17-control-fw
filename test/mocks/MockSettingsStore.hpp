#pragma once

#include <cstring>

#include "hal/ISettingsStore.hpp"

namespace test_mocks {

// In-memory ISettingsStore. Starts empty (first-boot); save() records the
// blob; corrupt()/setStored() let tests inject specific stored states.
class MockSettingsStore : public hal::ISettingsStore {
public:
    bool load(uint8_t* buf, size_t cap, size_t& len) override {
        if (!hasData || storedLen > cap) {
            return false;
        }
        std::memcpy(buf, stored, storedLen);
        len = storedLen;
        return true;
    }

    bool save(const uint8_t* buf, size_t len) override {
        if (len > sizeof(stored)) {
            return false;
        }
        std::memcpy(stored, buf, len);
        storedLen = len;
        hasData = true;
        saveCount += 1;
        return true;
    }

    void setStored(const uint8_t* buf, size_t len) {
        std::memcpy(stored, buf, len);
        storedLen = len;
        hasData = true;
    }

    uint8_t stored[256] = {0};
    size_t storedLen = 0;
    bool hasData = false;
    unsigned saveCount = 0;
};

} // namespace test_mocks
