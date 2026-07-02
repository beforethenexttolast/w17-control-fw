#pragma once

#include "hal/IByteSink.hpp"
#include "link2/Link2Frame.hpp"

namespace link2 {

// Everything main.cpp knows at send time, in control-loop units (±1000).
struct ControlSnapshot {
    int16_t commandedThrottle = 0; // what esc.setThrottle() actually received
    int16_t steering = 0;
    bool drsOpen = false;
    bool armed = false;
    bool failsafe = true; // boot-safe default
    bool lowBattery = false;
    uint8_t displayGear = 1; // 1-based
    uint16_t rpm = 0;
    uint16_t batteryMv = 0;
};

struct Link2SenderConfig {
    // Brake-light hysteresis on the ±1000 commanded throttle: ON below -40,
    // OFF at/above -20. A single hard threshold would flicker the brake LED
    // at 20 Hz on stick noise around it. (Deliberately a different knob from
    // ArmGate's ±60 neutral window -- that one decides "safe to arm", this
    // one decides "does the brake light look right".)
    int16_t brakeOnBelow = -40;
    int16_t brakeOffAbove = -20;

    constexpr bool valid() const { return brakeOnBelow < brakeOffAbove; }
};

// Builds VehicleState from a ControlSnapshot (scaling ±1000 -> ±100, brake
// hysteresis) and writes one encoded frame to the sink.
class Link2Sender {
public:
    explicit Link2Sender(hal::IByteSink& sink, Link2SenderConfig config = Link2SenderConfig{});

    void send(const ControlSnapshot& snapshot);

private:
    hal::IByteSink& sink_;
    Link2SenderConfig config_;
    bool brakingActive_ = false;
};

} // namespace link2
