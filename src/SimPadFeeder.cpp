#ifdef W17_SIM_PAD_FEEDER

#include "SimPadFeeder.hpp"

#include <Arduino.h>

// The looping BT-head demo session. Beat map + rationale in SimPadFeeder.hpp.
// Raw units follow the PadFrame contract: sticks -512..+511, triggers
// 0..1023, button bits from btpad/PadFrame.hpp.

namespace {

constexpr uint32_t kCycleMs = 13000;
constexpr uint32_t kReportPeriodMs = 16; // ~62 Hz, a plausible DS4 BT rate

// Phase boundaries (cycle-relative, ms). Names match the header's beat list.
constexpr uint32_t kConnectAt = 1500;      // A -> B
constexpr uint32_t kRitualFrom = 2500;     // B -> C (L1+R1 down)
constexpr uint32_t kRitualRelease = 3600;  //      (grips released; latched ~3500)
constexpr uint32_t kDriveFrom = 3700;      // C -> D
constexpr uint32_t kDropoutFrom = 6700;    // D -> E (silent + disconnected)
constexpr uint32_t kReclaimFrom = 7700;    // E -> F (connected, still silent)
constexpr uint32_t kStreamFrom = 8700;     // F -> G (reports resume, no ritual)
constexpr uint32_t kRitual2From = 9900;    // G -> H (fresh ritual)
constexpr uint32_t kRitual2Release = 11000; //     (latched ~10900; gentle drive)
constexpr uint32_t kOptionsFrom = 11600;   // H -> I (OPTIONS tap 100 ms)
constexpr uint32_t kOptionsUntil = 11700;

// Square taps inside phase D (press edges toggle DRS on, then off).
constexpr uint32_t kSquare1From = 4200, kSquare1Until = 4300;
constexpr uint32_t kSquare2From = 5800, kSquare2Until = 5900;

// Triangle wave over `periodMs`, 0..1000..0 per-mille (SimCrsfFeeder pattern).
uint32_t trianglePermille(uint32_t t, uint32_t periodMs) {
    const uint32_t phase = t % periodMs;
    const uint32_t half = periodMs / 2;
    return (phase < half) ? (phase * 1000 / half) : ((periodMs - phase) * 1000 / half);
}

struct PhaseState {
    int index;        // for change narration
    const char* name;
    bool connected;
    bool streaming;   // reports flowing this phase?
};

PhaseState phaseOf(uint32_t t) {
    if (t < kConnectAt) return {0, "A awaiting-pad (silent, disconnected)", false, false};
    if (t < kRitualFrom) return {1, "B connected, neutral stream (link recovers, disarmed)", true, true};
    if (t < kDriveFrom) return {2, "C arm ritual (L1+R1 held 1 s)", true, true};
    if (t < kDropoutFrom) return {3, "D driving (R2 ramp, steering sweep, Square DRS taps)", true, true};
    if (t < kReclaimFrom) return {4, "E dropout (silent, disconnected -> failsafe)", false, false};
    if (t < kStreamFrom) return {5, "F reconnect CLAIM only (no reports -> stays Safe)", true, false};
    if (t < kRitual2From) return {6, "G neutral stream, no ritual (Active, STAYS DISARMED)", true, true};
    if (t < kOptionsFrom) return {7, "H fresh ritual -> armed, gentle drive", true, true};
    return {8, "I OPTIONS disarm -> neutral idle", true, true};
}

btpad::PadFrame frameFor(uint32_t t) {
    btpad::PadFrame f; // all-neutral baseline

    // Ritual holds.
    const bool ritual1 = t >= kRitualFrom && t < kRitualRelease;
    const bool ritual2 = t >= kRitual2From && t < kRitual2Release;
    if (ritual1 || ritual2) {
        f.buttons |= btpad::kButtonL1 | btpad::kButtonR1;
    }

    // Drive shapes (phase D).
    if (t >= kDriveFrom && t < kDropoutFrom) {
        const uint32_t td = t - kDriveFrom;
        // R2: 0..1023..0 triangle, 1.5 s period.
        f.throttle = static_cast<int16_t>(trianglePermille(td, 1500) * 1023 / 1000);
        // Steering: full sweep -512..+511..-512, 2 s period.
        f.leftStickX = static_cast<int16_t>(
            -512 + static_cast<int32_t>(trianglePermille(td, 2000)) * 1023 / 1000);
        if ((t >= kSquare1From && t < kSquare1Until) || (t >= kSquare2From && t < kSquare2Until)) {
            f.buttons |= btpad::kButtonSquare;
        }
    }

    // Gentle drive after the second ritual (phase H tail): ~35% trigger.
    if (t >= kRitual2Release && t < kOptionsFrom) {
        f.throttle = 360;
    }

    // OPTIONS tap (phase I head): instant disarm.
    if (t >= kOptionsFrom && t < kOptionsUntil) {
        f.miscButtons |= btpad::kMiscOptions;
    }

    return f;
}

} // namespace

namespace simpad {

void SimPadSource::begin(uint16_t pairWindowMs) {
    (void)pairWindowMs; // pairing lockdown is real-stack policy; sim has none
    Serial.println("[simpad] scripted 13 s session loop armed (beats A-I; see SimPadFeeder.hpp)");
}

void SimPadSource::tick(uint32_t nowMs) {
    const uint32_t t = nowMs % kCycleMs;
    const PhaseState phase = phaseOf(t);

    if (phase.index != lastAnnouncedPhase_) {
        lastAnnouncedPhase_ = phase.index;
        Serial.printf("[simpad] t=%lu phase %s\n", static_cast<unsigned long>(nowMs), phase.name);
    }

    connected_ = phase.connected;
    if (!phase.streaming) {
        pendingReport_ = false; // silence: whatever was queued is gone with the link
        return;
    }
    if (nowMs - lastReportMs_ >= kReportPeriodMs) {
        lastReportMs_ = nowMs;
        frame_ = frameFor(t);
        pendingReport_ = true;
    }
}

bool SimPadSource::poll(btpad::PadFrame& frame) {
    if (!pendingReport_) {
        return false;
    }
    frame = frame_;
    pendingReport_ = false;
    return true;
}

void SimPadSource::setLightbar(uint8_t red, uint8_t green, uint8_t blue) {
    (void)red;
    (void)green;
    (void)blue; // no pad to color in the sim; deliberately silent (50 Hz caller)
}

} // namespace simpad

#endif // W17_SIM_PAD_FEEDER
