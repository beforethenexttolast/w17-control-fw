#include "link2/Link2Sender.hpp"
#include "link2/Link2Codec.hpp"

namespace link2 {

namespace {

int8_t toPercent(int16_t normalized) {
    int16_t percent = static_cast<int16_t>(normalized / 10);
    if (percent > 100) {
        percent = 100;
    } else if (percent < -100) {
        percent = -100;
    }
    return static_cast<int8_t>(percent);
}

} // namespace

Link2Sender::Link2Sender(hal::IByteSink& sink, Link2SenderConfig config)
    : sink_(sink), config_(config) {}

void Link2Sender::send(const ControlSnapshot& snapshot) {
    if (snapshot.commandedThrottle < config_.brakeOnBelow) {
        brakingActive_ = true;
    } else if (snapshot.commandedThrottle >= config_.brakeOffAbove) {
        brakingActive_ = false;
    }
    // In between: hold previous state (hysteresis).

    VehicleState state;
    state.throttlePercent = toPercent(snapshot.commandedThrottle);
    state.steeringPercent = toPercent(snapshot.steering);
    state.braking = brakingActive_;
    state.reverse = false; // reserved since v1: the ESC runs forward/brake
    state.drsOpen = snapshot.drsOpen;
    state.armed = snapshot.armed;
    state.failsafe = snapshot.failsafe;
    state.lowBattery = snapshot.lowBattery;
    state.ersDeploying = snapshot.ersDeploying;
    state.gear = snapshot.displayGear;
    state.rpm = snapshot.rpm;
    state.batteryMv = snapshot.batteryMv;
    state.ersPercent = snapshot.ersPercent;
    state.driveMode = snapshot.driveMode;
    // Configuration, not state: the persisted sound values ride every frame
    // regardless of failsafe/arm condition (board #2's own failsafe rules
    // silence the engine; volume never participates in that decision).
    state.soundProfile = sound_.profile;
    state.volume = sound_.volume;

    uint8_t frame[kFrameLen];
    encodeFrame(state, frame);
    sink_.write(frame, kFrameLen);
}

} // namespace link2
