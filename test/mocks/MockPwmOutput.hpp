#pragma once

#include "hal/IPwmOutput.hpp"

namespace test_mocks {

class MockPwmOutput : public hal::IPwmOutput {
public:
    void setPulseMicroseconds(uint16_t microseconds) override {
        lastMicroseconds = microseconds;
        callCount += 1;
    }

    uint16_t lastMicroseconds = 0;
    unsigned callCount = 0;
};

} // namespace test_mocks
