#pragma once

#include <cstdint>

#include "btpad/BtPadConfig.hpp"
#include "btpad/PadFrame.hpp"
#include "channels/ChannelDecoder.hpp" // channels::Controls -- the EXISTING shape

namespace btpad {

// PadFrame -> channels::Controls, the analog of channels::ChannelDecoder for
// the BT head (design §3.4/§4.2). Produces the SAME Controls struct the CRSF
// path produces, so everything downstream (FailsafeStateMachine, ArmGate,
// shaping, outputs, link2) is the same code, not a parallel re-implementation
// (design §3, the core claim).
//
// Envelope pins, unconditional in every decode (design §3.3):
//  - driveMode = 0 (TRAINING): one gentle shape, ERS can never activate
//    (needs driveMode 2), gear paddles inert. OWNER-PENDING(BT-4).
//  - pan/tilt = 0: the gimbal holds center in BT mode; a pad stick is not
//    CRSF ch9/10 (workspace boundary 4). OWNER-PENDING(BT-9).
//  - gearUpEdge/gearDownEdge never fire; boostHeld/overtakeHeld never set.
//
// Arm ritual (design §3.2, OWNER-PENDING(BT-3), recommended variant (a)):
//  - hold L1+R1 simultaneously for armHoldMs -> ritual latch ON (feeds
//    Controls::armSwitch; channels::ArmGate still requires throttle seen at
//    neutral before the motor may run -- that precondition is NOT relaxed).
//  - OPTIONS press -> latch OFF instantly (wins over a completing hold in the
//    same frame).
//  - forceDisarmRitual() (wiring calls it on any failsafe episode and on
//    disconnect) -> latch OFF. Stricter than the CRSF path, where the physical
//    arm switch survives an outage: here EVERY latch-clear also demands both
//    buttons be seen RELEASED before a new hold may begin, so an operator
//    frozen on the grips through an outage cannot be silently re-armed 1 s
//    after recovery -- re-arming is always a fresh, deliberate ritual.
//
// Pure C++, no hardware dependency; time is caller-injected (nowMs), same
// convention as FailsafeStateMachine. Call decode() once per new pad report.
class PadDecoder {
public:
    explicit PadDecoder(BtPadConfig config = BtPadConfig{});

    channels::Controls decode(const PadFrame& frame, uint32_t nowMs);

    // Clears the ritual latch and any hold in progress, and requires a full
    // release-then-rehold cycle before the next arm. Idempotent; wiring calls
    // it every pass while failsafe is Safe or the pad is disconnected.
    void forceDisarmRitual();

    // Live tuning (console path). Pure config copy; ritual/DRS state is
    // preserved, mirroring Gearbox::setConfig semantics. Caller is responsible
    // for having validated the config.
    void setConfig(const BtPadConfig& config) { config_ = config; }
    const BtPadConfig& config() const { return config_; }

    bool armRitualLatched() const { return armLatched_; }

private:
    // -512..+511 -> -1000..+1000, exact at both physical extremes (per-sign
    // scaling: the raw range is asymmetric). Out-of-range input clamps.
    static int16_t normalizeStick(int16_t raw);
    // 0..1023 -> 0..1000; out-of-range input clamps.
    static int16_t normalizeTrigger(int16_t raw);
    // Rescaled deadzone on the normalized value: 0 inside, then the remaining
    // travel maps linearly onto the full 0..1000 (no step at the edge, full
    // deflection still reaches +/-1000).
    int16_t applyDeadzone(int16_t normalized) const;

    void clearRitualAndRequireRelease();

    BtPadConfig config_;
    bool armLatched_ = false;
    bool holdActive_ = false;
    uint32_t holdStartMs_ = 0;
    bool requireReleaseBeforeRearm_ = false;
    bool drsToggleState_ = false;   // Square toggle, OWNER-PENDING(BT-5)
    bool squareWasPressed_ = false; // edge memory for the toggle
};

} // namespace btpad
