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
    // Also the dwell in the other direction: how long the sensor must read
    // implausible before a latched warning is dropped (BatteryMonitor::sample).
    uint32_t warnDelayMs = 3000;
    // Clears only above warnMv + this (sag recovery is often > 200 mV).
    uint16_t warnClearHysteresisMv = 400;

    // Sensor-implausibility bounds (grand review 2026-09-03, fault-injection-3;
    // owner ruling OD-10(a)). These describe the SENSOR, not the battery: a
    // reading outside them cannot come from a 2S pack that is powering this
    // ESP32 at all, so it means the 27k/10k divider itself is broken.
    //
    //   below: an open UPPER leg (or a shorted lower one) floats GPIO34 near
    //     ground and converts to ~0 mV. 4000 mV is 2.0 V/cell -- far under any
    //     2S voltage the UBEC could still be running this board from, so the
    //     floor cannot mask a genuinely flat pack (that path warns at 7000 mV
    //     long before, and a real pack this low has already browned us out).
    //   above: an open LOWER leg pulls the pin toward Vbat through the 27k and
    //     saturates the ADC (~3.1-3.3 V x 3.7 = ~11.5-12.2 V converted). 9000 mV
    //     is above a fully charged 2S (8.4 V) with margin for calibration trim,
    //     so nothing a real 2S pack can do reaches it.
    //
    // This is the case that made the finding gift-blocking: without the ceiling,
    // an open lower leg reports ~12 V, estimate2sPercent() says 100 %, and the
    // warning NEVER fires on a flat pack.
    uint16_t implausibleBelowMv = 4000;
    uint16_t implausibleAboveMv = 9000;

    constexpr bool valid() const {
        return dividerDen != 0 &&
               calibrationPpt >= 900 && calibrationPpt <= 1100 &&
               emaShift >= 1 && emaShift <= 6 &&
               warnClearHysteresisMv > 0 && warnDelayMs > 0 &&
               // The plausible band must strictly contain the whole warning
               // hysteresis window, or the monitor could never warn (floor too
               // high) or never clear (ceiling too low).
               implausibleBelowMv < warnMv &&
               implausibleAboveMv >
                   static_cast<uint32_t>(warnMv) + warnClearHysteresisMv &&
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

    // Smoothed battery-side millivolts; 0 until the first sample, and 0 again
    // for as long as the sensor reads implausible -- 0 means "no reading",
    // never "0 volts", and the callers that publish it (CRSF battery frame,
    // link2 vehicle-state frame) treat it that way.
    uint16_t batteryMv() const;

    // Latching with hysteresis + time qualification; never true before the
    // first sample (the EMA is seeded from it, so there is no climb-from-zero
    // boot artifact). The time qualification is SYMMETRIC: warnDelayMs of
    // sustained low to latch, and warnDelayMs of sustained sensor
    // implausibility to unlatch (see sample()). So a latched warning SURVIVES a
    // short dropout -- a flapping sense lead does not change what the pack is
    // doing -- and only a sustained "I have no reading at all" clears it.
    bool lowVoltageWarning() const { return warning_; }

    // True while the last sample was outside BatteryConfig's plausibility
    // band, i.e. the divider is broken rather than the pack being flat
    // (OD-10(a), 2026-09-03). It is DELIBERATELY not a warning: warning_ says
    // "your pack is low", this says "I have no idea what your pack is", and
    // the honest UI for the second one is a blank, not a number. Clears on the
    // first plausible sample, which also re-seeds the EMA exactly (same
    // reasoning as the boot seed: no climb-from-zero artifact). Note this flag
    // is instantaneous while lowVoltageWarning() is dwelled: a brief dropout
    // blanks the reading without dropping a latched warning.
    bool sensorImplausible() const { return implausible_; }

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
    bool implausible_ = false;
    uint32_t implausibleSinceMs_ = 0; // start of the current implausible run
};

} // namespace telemetry
