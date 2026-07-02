#pragma once

#include "hal/IVoltageSensor.hpp"

namespace test_mocks {

class FakeVoltageSensor : public hal::IVoltageSensor {
public:
    uint16_t readPinMillivolts() override { return pinMillivolts; }

    uint16_t pinMillivolts = 0;
};

} // namespace test_mocks
