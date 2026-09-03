#pragma once

#include <cstdint>

#include "telemetry/WheelSpeed.hpp"

namespace telemetry {

// Interrupt-RATE plausibility guard for the Hall input (grand review
// 2026-09-03, finding timing-1; owner ruling OD-11 guard (c)).
//
// The problem it exists for: Esp32HallPulseCounter's 2 ms lockout is an early
// return INSIDE the ISR, so it bounds how many edges are COUNTED, not how often
// the ISR is ENTERED. GPIO35 is input-only with no internal pull-up (the 10k is
// external, A2 row), so a lifted pull-up or ESC EMI on a slow edge can enter the
// ISR at kHz rates while the counter looks perfectly sane -- and every one of
// those entries steals time from the 50 Hz control tick, which is the tick that
// enforces failsafe. Counting accepted edges cannot see this; counting ISR
// ENTRIES can.
//
// The judgement is deliberately NOT in the ISR: this is a pure, native-testable
// helper the control tick feeds with the free-running entry counter. It says
// only what to do with the interrupt; the HAL does it (Esp32HallPulseCounter).
//
// What it is NOT: a measurement. The storm rate that would actually starve the
// tick is unmeasurable until Phase B (A2 is NOT-EXECUTED, nothing has been
// powered), so the bound below is set from the one number that IS known -- the
// fastest edge rate a real wheel can produce -- times a margin large enough that
// no wheel can reach it. See the Phase-B measurement row in the readiness docs.

// Edges the FASTEST PLAUSIBLE wheel can produce in `windowMs`, rounded up.
// maxPlausibleRpm is a WHEEL rpm; magnets multiply the edges per revolution.
constexpr uint32_t plausibleEdgesPerWindow(uint16_t maxPlausibleRpm, uint8_t magnetsPerRev,
                                           uint16_t windowMs) {
    return (static_cast<uint32_t>(maxPlausibleRpm) * magnetsPerRev * windowMs + 59999u) / 60000u;
}

// The margin the owner ruled: at least 20x the physical maximum, so a real
// wheel can never trip the guard and put a false sensor fault on the HUD.
//
// LATENT COUPLING, stated so it cannot be discovered the hard way (review
// 2026-09-03, finding 11): both this constant and PulseRateGuardConfig::valid()
// below derive the margin from WheelSpeedConfig{} DEFAULTS, not from the
// WheelSpeedConfig the live WheelSpeed was constructed with. Benign today and
// verified: src/main.cpp builds `wheelSpeed` from `telemetry::WheelSpeedConfig{}`
// and no wheel field exists in the persisted settings::Settings blob, so nothing
// can change it at runtime or at boot. But a non-default magnetsPerRev or
// maxPlausibleRpm would raise the real physical maximum while these numbers
// stayed put -- silently shrinking the margin below the 20x the owner ruled,
// with valid() still returning true because it measures against the same
// defaults. If wheel config ever becomes configurable, hand it to the guard
// instead of defaulting.
inline constexpr uint8_t kRateGuardMarginX = 20;
inline constexpr uint16_t kRateGuardWindowMs = 100;

// A measurement window longer than this multiple of the nominal window means
// the CALLER stalled (a long flash write, a debugger) -- the guard did not get
// the tick it needs to measure a rate.
inline constexpr uint32_t kStalledWindowFactor = 10;
// ... but a stalled tick is ALSO what a severe storm produces, so a stalled
// window is still judged once the count runs this far past the elapsed-scaled
// allowance. Below it the reading is genuinely ambiguous; above it there is
// nothing left to be ambiguous about. See PulseRateGuard::update().
inline constexpr uint32_t kStalledStormFactor = 10;
inline constexpr uint32_t kRateGuardEntriesPerWindow =
    plausibleEdgesPerWindow(WheelSpeedConfig{}.maxPlausibleRpm, WheelSpeedConfig{}.magnetsPerRev,
                            kRateGuardWindowMs) *
    kRateGuardMarginX;

struct PulseRateGuardConfig {
    // Measurement window. 100 ms is five control ticks: long enough that a
    // single burst of ringing on one edge cannot fill it, short enough that a
    // genuine storm is detached within ~0.1 s.
    uint16_t windowMs = kRateGuardWindowMs;
    // Entries tolerated per window before the input is declared faulty.
    // Default: 9 plausible edges per 100 ms (5000 rpm, 1 magnet) x 20 = 180,
    // i.e. ~1800 entries/s against a real maximum of ~83/s.
    uint32_t maxEntriesPerWindow = kRateGuardEntriesPerWindow;
    // How long the interrupt stays detached before it is re-armed. A storm
    // that is still there simply re-trips on the next window, so this bounds
    // the ISR's duty cycle rather than latching the wheel sensor off for the
    // rest of the drive.
    uint16_t quietMs = 1000;

    constexpr bool valid() const {
        return windowMs > 0 && quietMs > 0 &&
               // The margin is the whole safety argument: a bound that a real
               // wheel could reach would produce false faults, which on this
               // car means a dead speedo and no ERS harvest.
               maxEntriesPerWindow >=
                   static_cast<uint32_t>(kRateGuardMarginX) *
                       plausibleEdgesPerWindow(WheelSpeedConfig{}.maxPlausibleRpm,
                                               WheelSpeedConfig{}.magnetsPerRev, windowMs);
    }
};

// The 20x margin is the entire safety argument for this guard, and until now
// nothing enforced it: valid() was checked only by a unit test constructing the
// default config. It is asserted HERE rather than beside main.cpp's other
// `constexpr X kFoo{}; static_assert(kFoo.valid())` pairs because the guard's
// config reaches the firmware as a DEFAULT ARGUMENT (Esp32HallPulseCounter's
// constructor) -- main.cpp never names the type, so it has nothing to attach an
// assertion to. At the definition site it also covers the native test build.
static_assert(PulseRateGuardConfig{}.valid(),
              "hall rate guard: the default bound is under 20x the fastest edge rate a "
              "real wheel can produce -- a real wheel could trip it (false sensor fault)");

class PulseRateGuard {
public:
    // What the caller must do to the interrupt. Returned edge-triggered: the
    // same action is never returned twice for one transition.
    enum class Action : uint8_t {
        None,
        Detach,  // rate implausible: detach the ISR now
        Reattach // quiet window elapsed: re-arm and start measuring again
    };

    explicit PulseRateGuard(PulseRateGuardConfig config = PulseRateGuardConfig{});

    // Call every control tick with the FREE-RUNNING ISR-entry counter (every
    // entry, including the ones the debounce lockout rejects). Wrap-safe in
    // both the clock and the counter (unsigned subtraction).
    Action update(uint32_t nowMs, uint32_t entryCount);

    // True while the input is detached because the rate was implausible.
    // Consumers (WheelSpeed) must report zero motion, not the last value.
    bool faulted() const { return faulted_; }

    // How many times the guard has tripped since boot (diagnostic; saturates).
    uint32_t faultCount() const { return faultCount_; }

    // Entries counted in the last completed window (diagnostic: the number a
    // Phase-B bench session records to check the margin against reality).
    uint32_t lastWindowEntries() const { return lastWindowEntries_; }

    const PulseRateGuardConfig& config() const { return config_; }

private:
    PulseRateGuardConfig config_;
    bool seeded_ = false;
    bool faulted_ = false;
    uint32_t windowStartMs_ = 0;
    uint32_t windowStartCount_ = 0;
    uint32_t faultSinceMs_ = 0;
    uint32_t faultCount_ = 0;
    uint32_t lastWindowEntries_ = 0;
};

} // namespace telemetry
