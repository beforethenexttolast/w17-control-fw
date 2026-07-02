#include <Arduino.h>

#include "channels/ArmGate.hpp"
#include "channels/ChannelDecoder.hpp"
#include "config/PinMap.hpp"
#include "crsf/CrsfReceiver.hpp"
#include "crsf_hal_esp32/Esp32CrsfUart.hpp"
#include "failsafe/FailsafeStateMachine.hpp"
#include "gearbox/Gearbox.hpp"
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

// Owns frame assembly + typed dispatch; exposes channels, link stats, and the
// RX failsafe signal (latched uplink-LQ==0 from LINK_STATISTICS frames).
crsf::CrsfReceiver crsfReceiver;

// The channel map lives here, in one place; a bad index fails the build.
constexpr channels::ChannelMapConfig kChannelMap{};
static_assert(kChannelMap.valid(), "channel map: index out of range or bad thresholds");

channels::ChannelDecoder channelDecoder(kChannelMap);
channels::ArmGate armGate;
channels::Controls controls; // most recently decoded controls (all-neutral until a frame)

constexpr gearbox::GearboxConfig kGearboxConfig{};
static_assert(kGearboxConfig.valid(), "gearbox: bad gear table (range or non-monotonic)");
gearbox::Gearbox virtualGearbox(kGearboxConfig);

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
    const uint32_t nowMs = millis();

    // Accumulate (|=) rather than assign: an RC frame and a stats frame can
    // both complete in one drain pass, and the last result must not mask the
    // RC arrival.
    bool frameArrived = false;
    while (crsfUart.available() > 0) {
        const crsf::CrsfReceiver::ByteResult result =
            crsfReceiver.feedByte(static_cast<uint8_t>(crsfUart.read()), nowMs);
        frameArrived |= (result == crsf::CrsfReceiver::ByteResult::NewRcFrame);
    }

    // Decode on every new frame, including while failsafe is Safe: pausing
    // decode during an outage would make a switch moved during the outage
    // look like a fresh transition (phantom gear edge) on recovery. Decoding
    // is pure -- only the actuation below is gated on failsafe state.
    if (frameArrived) {
        controls = channelDecoder.decode(crsfReceiver.channels());

        // Gear edges are consume-on-read and `controls` is cached across
        // loop ticks, so shifts MUST happen here -- in the free-running loop
        // body one press would re-fire every tick until the next frame.
        // A gear shift is state, not actuation, so it is not gated on
        // failsafe (and gear deliberately survives failsafe/disarm: ArmGate
        // already forces a fresh throttle-neutral after every episode).
        if (controls.gearUpEdge) {
            virtualGearbox.shiftUp();
        }
        if (controls.gearDownEdge) {
            virtualGearbox.shiftDown();
        }
    }

    // rxSignalsFailsafe: latched uplink-LQ==0 from the RX's LINK_STATISTICS
    // -- an independent loss signal alongside the frame timeout, and the only
    // one that fires if the RX keeps sending hold-position RC frames.
    const failsafe::State state =
        failsafeStateMachine.update(nowMs, frameArrived, crsfReceiver.rxSignalsFailsafe());

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

    // Named local: link2 (D6) will report this post-gearbox value so the
    // engine sound tracks actual motor output, not stick position.
    const int16_t shapedThrottle = virtualGearbox.apply(controls.throttle);
    esc.setThrottle(armed ? shapedThrottle : 0);

    drs.setOpen(controls.drsSwitch);
}
