#pragma once

#include "hal/IWheelPulseSensor.hpp"

namespace test_mocks {

class FakeWheelPulseSensor : public hal::IWheelPulseSensor {
public:
    hal::WheelPulseSnapshot read() const override { return snapshot; }

    hal::WheelPulseSnapshot snapshot{0, 0};
};

} // namespace test_mocks
