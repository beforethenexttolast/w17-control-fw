#pragma once

#include <cstddef>
#include <cstdint>

namespace gearbox {

// One gear's throttle shaping (CLAUDE.md section 2.3: "max-output cap + expo
// curve ... low gears = gentle/limited, top gear = full").
struct GearParams {
    int16_t maxOutput;   // ceiling on forward throttle for this gear, 0..1000
    uint8_t expoPercent; // 0 = linear, 100 = full cubic expo (softens small inputs)
};

struct GearboxConfig {
    static constexpr size_t kMaxGears = 6;

    // Default 4-gear table: gentle first gear with strong expo, linear full
    // power on top. Pure "feel" values -- tune freely on the bench.
    GearParams gears[kMaxGears] = {
        {400, 50},
        {600, 35},
        {800, 20},
        {1000, 0},
    };
    uint8_t numGears = 4;
    uint8_t initialGear = 0; // 0-based

    // static_assert this at the definition site. maxOutput must be
    // non-decreasing across gears: "low gears gentle, top gear full" -- a
    // non-monotone table is almost certainly a config typo.
    constexpr bool valid() const {
        if (numGears < 1 || numGears > kMaxGears || initialGear >= numGears) {
            return false;
        }
        for (size_t i = 0; i < numGears; ++i) {
            if (gears[i].maxOutput < 1 || gears[i].maxOutput > 1000 ||
                gears[i].expoPercent > 100) {
                return false;
            }
            if (i > 0 && gears[i].maxOutput < gears[i - 1].maxOutput) {
                return false;
            }
        }
        return true;
    }
};

// Pure shaping function: (throttle, gear) -> output throttle.
//
// Input is clamped to [-1000, 1000] first (standalone module, trusts no caller).
// Forward (x > 0): expo curve, then SCALED by maxOutput -- scaling rather than
// clipping so full stick travel maps onto the gear's whole range (the
// "virtual gear" feel; clipping would flat-line the top of the stick travel
// in low gears). Expo preserves endpoints: x=1000 always shapes to exactly
// maxOutput.
// Brake/reverse (x <= 0): passes through unshaped, for simplicity and
// predictability. NOTE: in the ESC's forward/reverse-with-brake mode,
// "reverse" is indistinguishable from "brake" at the PWM level, so reverse
// would be ungoverned by the gearbox -- run the ESC in forward/brake mode
// for this car (see docs/ROADMAP.md D8 bench checklist).
int16_t shapeThrottle(int16_t normalizedThrottle, const GearParams& gear);

// Stateful gear selection over shapeThrottle. Gear is deliberately NOT reset
// by failsafe or disarm: the re-arm surprise is already closed by ArmGate
// (fresh throttle-neutral required after every episode), and resetting would
// silently change the car's behavior after a brief link blip.
//
// A shift at constant partial stick steps the output discontinuously -- an
// accepted property of gears; slew limiting is deferred feel-tuning.
class Gearbox {
public:
    // The config is expected to have passed the static_assert(valid()) at its
    // definition site; the constructor still clamps numGears/initialGear into
    // range so a bypassed assert cannot produce an out-of-range gear index.
    explicit Gearbox(GearboxConfig config = GearboxConfig{});

    void shiftUp();             // saturates at the top gear, no wrap
    void shiftDown();           // saturates at gear 0, no wrap
    void setGear(uint8_t gear); // clamped to the top gear; future 3-pos selector path

    uint8_t currentGear() const { return currentGear_; } // 0-based; display gear = +1

    int16_t apply(int16_t normalizedThrottle) const;

private:
    GearboxConfig config_;
    uint8_t currentGear_;
};

} // namespace gearbox
