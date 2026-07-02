#include <Arduino.h>

#include "channels/ArmGate.hpp"
#include "channels/ChannelDecoder.hpp"
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
// that moment instead of being re-read every tick.
crsf::RcChannelsFrame latestChannels{};

// The channel map lives here, in one place; a bad index fails the build.
constexpr channels::ChannelMapConfig kChannelMap{};
static_assert(kChannelMap.valid(), "channel map: index out of range or bad thresholds");

channels::ChannelDecoder channelDecoder(kChannelMap);
channels::ArmGate armGate;
channels::Controls controls; // most recently decoded controls (all-neutral until a frame)

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

    // Decode on every new frame, including while failsafe is Safe: pausing
    // decode during an outage would make a switch moved during the outage
    // look like a fresh transition (phantom gear edge) on recovery. Decoding
    // is pure -- only the actuation below is gated on failsafe state.
    if (frameArrived) {
        controls = channelDecoder.decode(latestChannels);
    }

    const failsafe::State state =
        failsafeStateMachine.update(millis(), frameArrived, /*rxFailsafeFlag=*/false);

    // The arm gate runs every tick so a failsafe episode clears its
    // neutral-seen latch: after recovery, throttle must be re-observed at
    // neutral before the motor may run again (CLAUDE.md 6.2).
    const bool armed = armGate.update(controls.armSwitch, controls.throttle,
                                      /*forceDisarm=*/state == failsafe::State::Safe);

    if (state == failsafe::State::Safe) {
        steering.setPosition(0);
        esc.setThrottle(0);
        drs.setOpen(false);
        return;
    }

    // Steering stays live while disarmed: CLAUDE.md 6.2 gates throttle only,
    // and bench setup needs steering without arming the motor.
    steering.setPosition(controls.steering);
    esc.setThrottle(armed ? controls.throttle : 0);
    drs.setOpen(controls.drsSwitch);

    // controls.gearUpEdge / gearDownEdge are decoded but unconsumed until the
    // gearbox module lands (docs/ROADMAP.md D3).
}
