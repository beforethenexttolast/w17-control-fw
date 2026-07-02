#include "telemetry_hal_esp32/Esp32BatteryAdc.hpp"

#include <Arduino.h>

namespace telemetry_hal_esp32 {

Esp32BatteryAdc::Esp32BatteryAdc(uint8_t pin) : pin_(pin) {}

void Esp32BatteryAdc::begin() {
    analogSetPinAttenuation(pin_, ADC_11db);
}

uint16_t Esp32BatteryAdc::readPinMillivolts() {
    uint32_t sumMv = 0;
    for (int i = 0; i < kReadsPerSample; ++i) {
        sumMv += analogReadMilliVolts(pin_);
    }
    return static_cast<uint16_t>(sumMv / kReadsPerSample);
}

} // namespace telemetry_hal_esp32
