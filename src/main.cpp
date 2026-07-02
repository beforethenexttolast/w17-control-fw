#include <Arduino.h>

#include "config/PinMap.hpp"
#include "crsf/CrsfFrameAssembler.hpp"
#include "crsf_hal_esp32/Esp32CrsfUart.hpp"
#include "failsafe/FailsafeStateMachine.hpp"
#include "hal/IClock.hpp"
#include "outputs/DrsOutput.hpp"
#include "outputs/EscOutput.hpp"
#include "outputs/ServoOutput.hpp"
#include "outputs_hal_esp32/Esp32LedcPwm.hpp"

namespace {

// Real-clock hal::IClock backed by millis(), used by EscOutput's boot-arm hold.
class Esp32MillisClock : public hal::IClock {
public:
    uint32_t nowMs() const override { return millis(); }
};

crsf_hal_esp32::Esp32CrsfUart crsfUart(pinmap::kCrsfUartRxPin, pinmap::kCrsfUartTxPin);
crsf::CrsfFrameAssembler crsfAssembler;

// Own copy of the most recent channels: CrsfFrameAssembler::lastFrame() is
// documented valid only immediately after FrameReady, so it is copied out at
// that moment instead of being re-read every tick. Only meaningful once the
// failsafe machine reports Active (which requires real frames to have arrived).
crsf::RcChannelsFrame latestChannels{};

failsafe::FailsafeStateMachine failsafeStateMachine;

Esp32MillisClock clock;

// Default configs, named so setup() can hand their safe positions to the PWM
// layer as the initial pulse.
constexpr outputs::ServoConfig steeringConfig{};
constexpr outputs::EscConfig escConfig{};
constexpr outputs::DrsConfig drsConfig{};

outputs_hal_esp32::Esp32LedcPwm steeringPwm(pinmap::kSteeringServoPin, /*channel=*/0);
outputs_hal_esp32::Esp32LedcPwm escPwm(pinmap::kEscThrottlePin, /*channel=*/1);
outputs_hal_esp32::Esp32LedcPwm drsPwm(pinmap::kDrsServoPin, /*channel=*/2);

outputs::ServoOutput steering(steeringPwm, steeringConfig);
outputs::EscOutput esc(escPwm, clock, escConfig);
outputs::DrsOutput drs(drsPwm, drsConfig);

// Minimal half of the CLAUDE.md section 6.2 arm gate ("no arm-into-full-
// throttle"): throttle stays neutral until the throttle channel has been
// observed at neutral once -- and again after every failsafe episode, so a
// link recovery mid-stick-input cannot snap the motor on.
// TODO(deliverable-2): replaced by the full arm-switch gate in the channels module.
bool throttleSeenNeutral = false;

// Raw CRSF units around center (992) accepted as "neutral" for the latch
// above: ~6% of the 819-unit half-travel, a small deadband per CLAUDE.md
// section 3.
constexpr int32_t kThrottleNeutralDeadbandRaw = 50;

// Linearly maps a raw CRSF channel value onto the [-1000, +1000] range used
// by the outputs:: classes. TODO(deliverable-2): replace this whole
// placeholder mapping with the configurable channels module (CLAUDE.md
// section 3) and the gearbox.
int16_t rawChannelToNormalized(uint16_t raw) {
    const int32_t centered = static_cast<int32_t>(raw) - crsf::kChannelRawCenter;
    const int32_t span = crsf::kChannelRawMax - crsf::kChannelRawCenter;
    int32_t normalized = (centered * 1000) / span;
    if (normalized > 1000) {
        normalized = 1000;
    } else if (normalized < -1000) {
        normalized = -1000;
    }
    return static_cast<int16_t>(normalized);
}

} // namespace

void setup() {
    crsfUart.begin();

    // Attach PWM with an explicit safe initial pulse (center/neutral/closed)
    // so the outputs never depend on the ordering of the calls below.
    steeringPwm.begin(steeringConfig.centerMicros);
    escPwm.begin(escConfig.neutralMicros);
    drsPwm.begin(drsConfig.closedMicros);

    steering.setPosition(0);
    esc.setThrottle(0); // first command: starts the ESC boot-arm hold window
    drs.setOpen(false);
}

void loop() {
    bool frameArrived = false;
    while (crsfUart.available() > 0) {
        const crsf::CrsfFrameAssembler::FeedResult result =
            crsfAssembler.feedByte(static_cast<uint8_t>(crsfUart.read()));
        if (result == crsf::CrsfFrameAssembler::FeedResult::FrameReady) {
            latestChannels = crsfAssembler.lastFrame();
            frameArrived = true;
        }
    }

    const failsafe::State state =
        failsafeStateMachine.update(millis(), frameArrived, /*rxFailsafeFlag=*/false);

    if (state == failsafe::State::Safe) {
        throttleSeenNeutral = false; // neutral must be re-observed after any failsafe
        steering.setPosition(0);
        esc.setThrottle(0);
        drs.setOpen(false);
        return;
    }

    // TODO(deliverable-2): replace with the channels/gearbox modules. Raw
    // channel indices below follow CLAUDE.md section 3 defaults: steering =
    // ch1 (index 0), throttle = ch3 (index 2).
    const uint16_t rawThrottle = latestChannels.channels[2];
    if (!throttleSeenNeutral) {
        const int32_t deviation =
            static_cast<int32_t>(rawThrottle) - crsf::kChannelRawCenter;
        if (deviation >= -kThrottleNeutralDeadbandRaw &&
            deviation <= kThrottleNeutralDeadbandRaw) {
            throttleSeenNeutral = true;
        }
    }

    steering.setPosition(rawChannelToNormalized(latestChannels.channels[0]));
    esc.setThrottle(throttleSeenNeutral ? rawChannelToNormalized(rawThrottle) : 0);
}
