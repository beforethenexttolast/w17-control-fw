#pragma once

#include "hal/IClock.hpp"

namespace test_mocks {

class FakeClock : public hal::IClock {
public:
    uint32_t nowMs() const override { return nowMs_; }

    void setNowMs(uint32_t nowMs) { nowMs_ = nowMs; }

private:
    uint32_t nowMs_ = 0;
};

} // namespace test_mocks
