#pragma once

#include <cstdint>

#include "hal/IVoltageSensor.hpp"

namespace telemetry {

struct BatteryConfig {
    // Divider: Vbat = Vpin * (27k + 10k) / 10k, per CLAUDE.md section 7.
    uint16_t dividerNum = 37;
    uint16_t dividerDen = 10;

    // Multimeter calibration trim, parts-per-thousand (1000 = no trim).
    // Trimmed on the bench per CLAUDE.md section 2.6.
    uint16_t calibrationPpt = 1000;

    // EMA smoothing time constant as a power of two: larger = smoother/slower.
    // At 100 ms sampling, shift 3 gives tau ~ 0.8 s.
    uint8_t emaShift = 3;

    // Low-voltage warning, MONITORING ONLY (CLAUDE.md 6.4: warn, never cut).
    // 7000 mV = 3.5 V/cell on 2S, sane as a loaded threshold. The warning
    // latches only after the smoothed voltage stays below warnMv continuously
    // for warnDelayMs -- throttle sag on a healthy pack dips through the
    // threshold for a second or two and must not flap the warning.
    uint16_t warnMv = 7000;
    uint32_t warnDelayMs = 3000;
    // Clears only above warnMv + this (sag recovery is often > 200 mV).
    uint16_t warnClearHysteresisMv = 400;

    constexpr bool valid() const {
        return dividerDen != 0 &&
               calibrationPpt >= 900 && calibrationPpt <= 1100 &&
               emaShift >= 1 && emaShift <= 6 &&
               warnClearHysteresisMv > 0 && warnDelayMs > 0 &&
               // Overflow bounds for the conversion in sample():
               // 3300 (max pin mV) * dividerNum * calibrationPpt must fit uint32,
               // and the max battery result must fit uint16.
               3300ull * dividerNum * calibrationPpt <= 0xFFFFFFFFull &&
               (3300ull * dividerNum * calibrationPpt) / (dividerDen * 1000ull) <= 0xFFFFull;
    }
};

// Smoothed battery voltage + latching low-voltage warning. Pure logic over an
// injected hal::IVoltageSensor; time is caller-supplied (house pattern).
class BatteryMonitor {
public:
    explicit BatteryMonitor(hal::IVoltageSensor& sensor, BatteryConfig config = BatteryConfig{});

    // Call periodically (e.g. every 100 ms). nowMs drives the warn delay.
    void sample(uint32_t nowMs);

    // Smoothed battery-side millivolts; 0 until the first sample.
    uint16_t batteryMv() const;

    // Latching with hysteresis + time qualification; never true before the
    // first sample (the EMA is seeded from it, so there is no climb-from-zero
    // boot artifact).
    bool lowVoltageWarning() const { return warning_; }

    // Runtime reconfiguration (bench tuning console; only calibrationPpt is
    // exposed today). If emaShift changes the accumulator scale is stale, so
    // force a re-seed on the next sample; a calibrationPpt change just
    // converges over the EMA. Caller validates the config.
    void setConfig(const BatteryConfig& config) {
        if (config.emaShift != config_.emaShift) {
            seeded_ = false;
        }
        config_ = config;
    }
    const BatteryConfig& config() const { return config_; }

private:
    uint16_t convertPinToBatteryMv(uint16_t pinMv) const;

    hal::IVoltageSensor& sensor_;
    BatteryConfig config_;
    bool seeded_ = false;
    uint32_t emaAccumulator_ = 0; // battery mV scaled by 2^emaShift
    bool warning_ = false;
    bool belowSince_ = false;
    uint32_t belowSinceMs_ = 0;
};

} // namespace telemetry
