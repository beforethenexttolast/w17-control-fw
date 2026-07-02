#pragma once

#include <cstdint>

#include "hal/IClock.hpp"
#include "hal/IPwmOutput.hpp"

namespace outputs {

struct EscConfig {
    uint16_t minMicros = 1000;     // ESC throttle full-reverse/min endpoint
    uint16_t maxMicros = 2000;     // ESC throttle full-forward/max endpoint
    uint16_t neutralMicros = 1500; // ESC neutral -- most ESCs require this to arm
    uint32_t bootArmHoldMs = 2000; // hold neutral this long, measured from the FIRST
                                   // setThrottle() call, before accepting any
                                   // non-neutral command (CLAUDE.md section 6.3,
                                   // "ESC boot arm sequence")
};

// Throttle scaling + the ESC's electrical boot-arm sequence: hold neutral for
// `bootArmHoldMs` before accepting any other value. The hold is anchored to
// the first setThrottle() call -- the moment this output actually starts
// commanding pulses -- NOT to construction: as a global, this object is
// constructed during static initialization, long before setup() attaches the
// PWM, so a construction-anchored timer could partially (or fully) elapse
// before the ESC has seen a single neutral pulse (review finding A5,
// docs/ROADMAP.md).
//
// NOTE: this is the ESC's own boot-arm requirement (CLAUDE.md 6.3), distinct
// from the higher-level arm-SWITCH gate (CLAUDE.md 6.2, "throttle stays
// neutral until arm switch ON and throttle observed at neutral once"), which
// belongs in the not-yet-built channel-map module. Two separate "arm"
// concepts; this class only implements the boot-hold one.
class EscOutput {
public:
    // `clock` is injected so the boot-arm timer is testable with a fake clock
    // -- no delay() in the control path, per CLAUDE.md architecture rules.
    EscOutput(hal::IPwmOutput& pwm, hal::IClock& clock, EscConfig config = EscConfig{});

    // normalizedThrottle in [-1000, +1000], 0 = neutral. The first call starts
    // the boot-arm hold; until bootArmHoldMs has elapsed since that first call,
    // always writes neutralMicros regardless of the requested value.
    // Out-of-range input is clamped defensively.
    void setThrottle(int16_t normalizedThrottle);

    // True once bootArmHoldMs has elapsed since the first setThrottle() call
    // (inclusive: the tick where elapsed == bootArmHoldMs counts as armed).
    // Always false before setThrottle() has ever been called.
    bool isArmed() const;

private:
    hal::IPwmOutput& pwm_;
    hal::IClock& clock_;
    EscConfig config_;
    bool armHoldStarted_ = false;
    uint32_t armHoldStartMs_ = 0;
};

} // namespace outputs
