#pragma once

#include <cstdint>

#include "crsf/CrsfFrame.hpp"

namespace channels {

// Which raw CRSF channel feeds which logical control, plus decode tuning.
// DEFAULTS ARE PLACEHOLDERS per CLAUDE.md section 3 ("verify against my TX")
// -- confirm every assignment at the bench and remap HERE only.
struct ChannelMapConfig {
    // Raw CRSF channel indices, 0-based (chN on the TX = index N-1).
    uint8_t steeringIndex = 0; // ch1
    uint8_t throttleIndex = 2; // ch3
    uint8_t armIndex = 4;      // ch5, 2-pos switch: must be ON to allow throttle
    uint8_t drsIndex = 5;      // ch6, 2-pos switch
    uint8_t gearUpIndex = 6;   // ch7, momentary
    uint8_t gearDownIndex = 7; // ch8, momentary
    uint8_t panIndex = 8;      // ch9  -- gimbal, deferred: decoded but unwired
    uint8_t tiltIndex = 9;     // ch10 -- gimbal, deferred: decoded but unwired
    uint8_t boostIndex = 10;   // ch11, held switch: ERS boost deploy
    uint8_t overtakeIndex = 11; // ch12, held switch: ERS overtake deploy
    // ch13, 3-pos: low = Training, mid = Gearbox, high = Gearbox+ERS.
    uint8_t driveModeIndex = 12;

    // Bench conveniences for reversed TX axes (flips the normalized sign).
    bool invertSteering = false;
    bool invertThrottle = false;

    // Switch hysteresis on the normalized [-1000, +1000] value: ON above
    // switchOnAbove, OFF below switchOffBelow, previous state held in between
    // (chatter-proof; a 2-pos CRSF switch sits at roughly +/-1000).
    // Extension point (deliberately deferred): per-switch inversion and a
    // 3-pos gear selector on a single channel would slot in here.
    int16_t switchOnAbove = 250;
    int16_t switchOffBelow = -250;

    // static_assert this at the definition site: a wrong index must fail the
    // build, not remap a control onto the wrong channel.
    constexpr bool valid() const {
        return steeringIndex < crsf::kNumChannels && throttleIndex < crsf::kNumChannels &&
               armIndex < crsf::kNumChannels && drsIndex < crsf::kNumChannels &&
               gearUpIndex < crsf::kNumChannels && gearDownIndex < crsf::kNumChannels &&
               switchOnAbove > switchOffBelow;
        // panIndex/tiltIndex/boostIndex/overtakeIndex/driveModeIndex are
        // deliberately unchecked: >= kNumChannels means "control absent" with
        // safe degraded behavior (analog 0, switch OFF, drive mode Gearbox).
    }
};

// Decoded logical controls for one frame.
struct Controls {
    int16_t steering = 0; // -1000..+1000, exact at the CRSF anchors 172/992/1811
    int16_t throttle = 0; // -1000..+1000
    int16_t pan = 0;      // decoded but unwired until the gimbal deliverable
    int16_t tilt = 0;
    bool armSwitch = false;
    bool drsSwitch = false;
    bool boostHeld = false;    // held (level) switches, not edges
    bool overtakeHeld = false;
    // 0 = Training, 1 = Gearbox, 2 = Gearbox+ERS. Stateless tri-state decode
    // (a 3-pos switch never dwells between detents); default/absent = 1 so
    // pre-first-frame ticks and a missing channel equal plain gearbox.
    uint8_t driveMode = 1;
    // Edge flags are consume-on-read: true ONLY in the Controls returned by
    // the decode() call that observed the OFF->ON transition. Act on them the
    // same tick or lose them.
    bool gearUpEdge = false;
    bool gearDownEdge = false;
};

// Stateful raw-frame -> Controls decoder (hysteresis + edge state). Pure C++,
// no hardware dependency.
//
// Call decode() exactly once per newly received frame -- including while
// failsafe is Safe. Edges derive from level state, so skipping intermediate
// frames is harmless, but pausing decode during a link outage would make a
// switch moved during the outage look like a fresh transition on recovery
// (a phantom gear change). Decode is pure; gate ACTUATION on failsafe, not
// decoding.
class ChannelDecoder {
public:
    explicit ChannelDecoder(ChannelMapConfig config = ChannelMapConfig{});

    Controls decode(const crsf::RcChannelsFrame& frame);

private:
    // Normalizes one raw channel; indices >= kNumChannels decode to 0
    // ("control absent" -- never silently remapped).
    int16_t normalizedAnalog(const crsf::RcChannelsFrame& frame, uint8_t index,
                              bool invert) const;
    // Applies hysteresis to one switch channel; `state` is that switch's
    // persistent ON/OFF memory.
    bool decodeSwitch(const crsf::RcChannelsFrame& frame, uint8_t index, bool& state) const;
    // Stateless 3-pos decode: < -333 -> 0, > +333 -> 2, else (incl. exactly
    // +/-333 and absent index) -> 1. No hysteresis/seeding needed: it is a
    // pure level classification and a real 3-pos never dwells between detents.
    uint8_t decodeTriState(const crsf::RcChannelsFrame& frame, uint8_t index) const;

    ChannelMapConfig config_;
    bool firstDecodeDone_ = false; // first decode seeds switch states, fires no edges
    bool armState_ = false;
    bool drsState_ = false;
    bool boostState_ = false;
    bool overtakeState_ = false;
    bool gearUpState_ = false;
    bool gearDownState_ = false;
};

} // namespace channels
