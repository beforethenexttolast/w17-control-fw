#include "channels/ChannelDecoder.hpp"

namespace channels {

namespace {

// Piecewise-linear normalization, exact at the CRSF anchors: raw 172 -> -1000,
// 992 -> 0, 1811 -> +1000 (CLAUDE.md section 2.1 raw range). The low and high
// half-spans differ by one raw unit (820 vs 819), so a single-span formula
// cannot hit both endpoints exactly. Integer division truncates toward zero,
// biasing toward neutral -- the safe direction. Raw values outside the
// nominal range (the 11-bit field can carry 0..2047) clamp to the endpoints.
int16_t normalizeRaw(uint16_t raw) {
    constexpr int32_t kLowSpan = crsf::kChannelRawCenter - crsf::kChannelRawMin;  // 820
    constexpr int32_t kHighSpan = crsf::kChannelRawMax - crsf::kChannelRawCenter; // 819

    const int32_t centered = static_cast<int32_t>(raw) - crsf::kChannelRawCenter;
    const int32_t normalized =
        (centered >= 0) ? (centered * 1000) / kHighSpan : (centered * 1000) / kLowSpan;

    if (normalized > 1000) {
        return 1000;
    }
    if (normalized < -1000) {
        return -1000;
    }
    return static_cast<int16_t>(normalized);
}

} // namespace

ChannelDecoder::ChannelDecoder(ChannelMapConfig config) : config_(config) {}

int16_t ChannelDecoder::normalizedAnalog(const crsf::RcChannelsFrame& frame, uint8_t index,
                                          bool invert) const {
    if (index >= crsf::kNumChannels) {
        return 0; // control absent
    }
    const int16_t value = normalizeRaw(frame.channels[index]);
    return invert ? static_cast<int16_t>(-value) : value;
}

bool ChannelDecoder::decodeSwitch(const crsf::RcChannelsFrame& frame, uint8_t index,
                                   bool& state) const {
    if (index >= crsf::kNumChannels) {
        state = false; // control absent
        return state;
    }
    const int16_t value = normalizeRaw(frame.channels[index]);

    if (!firstDecodeDone_) {
        // Seed from the actual level so a switch parked ON at boot reads ON
        // immediately (values inside the hysteresis band seed OFF).
        state = value > config_.switchOnAbove;
        return state;
    }

    if (value > config_.switchOnAbove) {
        state = true;
    } else if (value < config_.switchOffBelow) {
        state = false;
    }
    // In between: hold previous state (hysteresis).
    return state;
}

uint8_t ChannelDecoder::decodeTriState(const crsf::RcChannelsFrame& frame, uint8_t index) const {
    if (index >= crsf::kNumChannels) {
        return 1; // control absent: Gearbox, the safe middle
    }
    const int16_t value = normalizeRaw(frame.channels[index]);
    if (value < -333) {
        return 0;
    }
    if (value > 333) {
        return 2;
    }
    return 1;
}

Controls ChannelDecoder::decode(const crsf::RcChannelsFrame& frame) {
    Controls out;
    out.steering = normalizedAnalog(frame, config_.steeringIndex, config_.invertSteering);
    out.throttle = normalizedAnalog(frame, config_.throttleIndex, config_.invertThrottle);
    out.pan = normalizedAnalog(frame, config_.panIndex, /*invert=*/false);
    out.tilt = normalizedAnalog(frame, config_.tiltIndex, /*invert=*/false);

    out.armSwitch = decodeSwitch(frame, config_.armIndex, armState_);
    out.drsSwitch = decodeSwitch(frame, config_.drsIndex, drsState_);
    out.boostHeld = decodeSwitch(frame, config_.boostIndex, boostState_);
    out.overtakeHeld = decodeSwitch(frame, config_.overtakeIndex, overtakeState_);
    out.driveMode = decodeTriState(frame, config_.driveModeIndex);

    const bool gearUpWas = gearUpState_;
    const bool gearDownWas = gearDownState_;
    const bool gearUpNow = decodeSwitch(frame, config_.gearUpIndex, gearUpState_);
    const bool gearDownNow = decodeSwitch(frame, config_.gearDownIndex, gearDownState_);

    // Edges only from an OBSERVED off->on transition: the first decode ever
    // seeds levels and must not fire (a switch stuck high at boot is not a
    // gear-change request).
    if (firstDecodeDone_) {
        out.gearUpEdge = gearUpNow && !gearUpWas;
        out.gearDownEdge = gearDownNow && !gearDownWas;
    }

    firstDecodeDone_ = true;
    return out;
}

} // namespace channels
