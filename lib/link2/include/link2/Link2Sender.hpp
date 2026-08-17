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
    bool ersDeploying = false;
    uint8_t displayGear = 1; // 1-based
    uint16_t rpm = 0;
    uint16_t batteryMv = 0;
    uint8_t ersPercent = 100;
    uint8_t driveMode = 1; // 0 TRAINING / 1 RACE (gearbox) / 2 ERS (gearbox+ERS)
    // Boot state, not drive state: set ONCE at boot from the resolved
    // bootmode (bootmode::link2ShowcaseFlag) and constant for the whole
    // session -- true in a SHOWCASE boot, false in every DRIVE boot. Rides
    // every frame including failsafe frames. There is deliberately NO
    // awaitingController field here: the sender structurally cannot set the
    // BT bit until that mode ships.
    bool showcase = false;
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

// The NVS-tunable sound pair (tuning-console keys `sound.profile` /
// `sound.volume`) -- the values the v2 soundProfile/volume payload bytes
// carry. Lives HERE, in the control-side sender header, not in the shared
// codec headers: only the sender owns a persisted copy, board #2 just
// consumes the wire bytes. Defaults equal the VehicleState wire defaults in
// Link2Frame.hpp, so an unconfigured sender and a default-constructed frame
// say the same thing.
struct SoundConfig {
    uint8_t profile = kSoundProfileV10; // 0 V10 / 1 V6 turbo-hybrid
    uint8_t volume = kDefaultVolume;    // 0..100; 0 = true silence on board #2

    // Stricter than the wire, which passes reserved values through raw: the
    // sender must never PERSIST or TRANSMIT a value the receiver would have
    // to fall back on / clamp.
    constexpr bool valid() const {
        return profile < kSoundProfileCount && volume <= kVolumeMax;
    }
};

// Builds VehicleState from a ControlSnapshot (scaling ±1000 -> ±100, brake
// hysteresis), stamps the quasi-static sound config, and writes one encoded
// frame to the sink.
class Link2Sender {
public:
    explicit Link2Sender(hal::IByteSink& sink, Link2SenderConfig config = Link2SenderConfig{});

    // Pure config-copy in the same applySettings() pattern as the other
    // modules' setConfig(): the validated NVS sound values to stamp into
    // every outgoing frame from now on. No state reset (brake-light
    // hysteresis state is preserved). Callers pass an already-validated
    // object (Settings::valid() composes SoundConfig::valid()).
    void setSoundConfig(const SoundConfig& sound) { sound_ = sound; }

    void send(const ControlSnapshot& snapshot);

private:
    hal::IByteSink& sink_;
    Link2SenderConfig config_;
    SoundConfig sound_{}; // defaults V10 / volume 80 until settings arrive
    bool brakingActive_ = false;
};

} // namespace link2
