#pragma once

#include <cstring>

#include "hal/IByteSink.hpp"

namespace test_mocks {

class MockByteSink : public hal::IByteSink {
public:
    void write(const uint8_t* data, size_t len) override {
        if (len > sizeof(lastWrite)) {
            len = sizeof(lastWrite);
        }
        std::memcpy(lastWrite, data, len);
        lastWriteLen = len;
        writeCount += 1;
    }

    uint8_t lastWrite[64] = {};
    size_t lastWriteLen = 0;
    unsigned writeCount = 0;
};

} // namespace test_mocks
