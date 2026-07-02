#pragma once

#include <cstdint>

#include "hal/IVoltageSensor.hpp"

namespace telemetry_hal_esp32 {

// Battery-divider ADC input via analogReadMilliVolts() (esp_adc_cal,
// eFuse-calibrated), so ADC nonlinearity stays below the hal seam. Averages
// a small burst of reads per call: the 27k||10k divider's ~7.3k source
// impedance makes single ESP32 ADC reads noisy.
class Esp32BatteryAdc : public hal::IVoltageSensor {
public:
    explicit Esp32BatteryAdc(uint8_t pin);

    // Sets 11dB attenuation (0..~2450mV usable; 8.4V reads ~2270mV at the pin).
    void begin();

    uint16_t readPinMillivolts() override;

private:
    static constexpr int kReadsPerSample = 4;

    uint8_t pin_;
};

} // namespace telemetry_hal_esp32
