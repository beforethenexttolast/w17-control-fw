#pragma once

#include <cstdint>

#include "hal/IWheelPulseSensor.hpp"

namespace telemetry {

struct WheelSpeedConfig {
    // One axle magnet to start, per CLAUDE.md section 7.
    uint8_t magnetsPerRev = 1;
    // 64 mm-OD F104 tyre -> ~201 mm rolling circumference.
    uint16_t wheelCircumferenceMm = 201;
    // No pulse for this long => report 0 (the graceful-decay rule below
    // already brings the reading down; this truncates the asymptotic tail).
    uint16_t zeroSpeedTimeoutMs = 1500;
    // Plausibility bound: ~55 rev/s at the car's real top speed, so anything
    // near this is EMI/glitch, not motion. A period implying more than this is
    // REJECTED (reported speed 0), not clamped to it -- see WheelSpeed.cpp.
    uint16_t maxPlausibleRpm = 5000;

    constexpr bool valid() const {
        return magnetsPerRev >= 1 && wheelCircumferenceMm > 0 && zeroSpeedTimeoutMs > 0 &&
               maxPlausibleRpm > 0;
    }
};

// Wheel/axle speed from ISR-timestamped Hall pulse periods. Pure logic over
// an injected hal::IWheelPulseSensor; time is caller-supplied.
//
// RPM comes from the measured period between edges (exact at any update
// cadence), not from counts-per-window. A slowing wheel decays gracefully:
// while no new pulse arrives, the reported value is capped by the speed the
// wheel COULD still have given the silence so far ("if it were still that
// fast, we'd have seen a pulse by now"), instead of holding the last value
// and then stepping to zero.
class WheelSpeed {
public:
    explicit WheelSpeed(hal::IWheelPulseSensor& sensor, WheelSpeedConfig config = WheelSpeedConfig{});

    // Call every control tick. All time deltas use unsigned subtraction
    // (millis()-wraparound safe).
    void update(uint32_t nowMs);

    uint16_t rpm() const { return reportedRpm_; }

    // How many pulse periods have been rejected as implausible since boot.
    // Diagnostic only -- nothing gates on it -- but it is the signal that says
    // "the Hall input is picking up EMI" when a bench reading looks odd, and
    // the counterpart to the hardware rate guard (PulseRateGuard) one layer
    // down. Saturates rather than wraps.
    uint32_t rejectedPulses() const { return rejectedPulses_; }

    // Mirrors the last snapshot's hal::WheelPulseSnapshot::sensorFault: the
    // pulse input is disabled and the reported speed is a hard 0 for that
    // reason, not because the car is standing still.
    bool sensorFault() const { return sensorFault_; }

    uint16_t speedMmPerSec() const {
        return static_cast<uint16_t>(
            (static_cast<uint32_t>(reportedRpm_) * config_.wheelCircumferenceMm) / 60u);
    }

private:
    hal::IWheelPulseSensor& sensor_;
    WheelSpeedConfig config_;
    bool seeded_ = false;
    uint32_t lastCount_ = 0;
    uint32_t lastPulseSeenMs_ = 0; // when update() last observed the count change
    uint16_t measuredRpm_ = 0;     // from the last valid pulse period
    uint16_t reportedRpm_ = 0;     // measured, decayed, or zeroed
    uint32_t rejectedPulses_ = 0;  // implausible periods seen (diagnostic)
    bool sensorFault_ = false;     // last snapshot's sensor-fault flag
};

} // namespace telemetry
