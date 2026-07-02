#pragma once

#include <cstdint>

namespace hal {

// Abstract voltage input, reporting CALIBRATED millivolts at the ADC pin.
// The seam sits above ADC nonlinearity on purpose: the ESP32 transfer curve
// is a chip property handled below (esp_adc_cal / analogReadMilliVolts in
// the esp32 impl); the resistor divider is a board property handled above
// (BatteryMonitor). Deviation from the roadmap's "IAdc raw counts" sketch,
// recorded in docs/ROADMAP.md D5.
class IVoltageSensor {
public:
    virtual ~IVoltageSensor() = default;

    virtual uint16_t readPinMillivolts() = 0;
};

} // namespace hal
