#pragma once

#include <cstdint>

namespace hal {

// One coherent-enough view of the Hall pulse stream. The ISR (esp32 impl)
// timestamps each edge, so speed is computed from the measured pulse PERIOD
// -- exact at any update cadence -- rather than from counts-per-window,
// which would quantize to the caller's tick rate (at ~55 Hz pulses and a
// 50 Hz control tick, count deltas alternate 1/2 and the speed would flap
// 2:1). The pair may rarely be torn (count from a newer edge than the
// period); each field is individually consistent and the consumer clamps
// implausible values, so this is acceptable by design.
struct WheelPulseSnapshot {
    uint32_t count;            // monotonically increasing edge count since boot
    uint32_t lastPeriodMicros; // micros between the two most recent edges;
                               // 0 until two edges have ever been seen
    // True while the implementation has DISABLED the pulse input because the
    // interrupt rate was implausible (telemetry::PulseRateGuard; finding
    // timing-1 / OD-11). It is a statement about the SENSOR, not the wheel:
    // consumers must report zero motion, never the last known value, because
    // "we stopped listening" and "the car stopped" are the same thing to
    // everything downstream (speedo, GPS-groundspeed frame, ERS harvest gate).
    bool sensorFault = false;
};

class IWheelPulseSensor {
public:
    virtual ~IWheelPulseSensor() = default;

    virtual WheelPulseSnapshot read() const = 0;
};

} // namespace hal
