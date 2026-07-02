#ifdef W17_SIM_CRSF_FEEDER

#include "SimCrsfFeeder.hpp"

#include <Arduino.h>

#include "crsf/CrsfFrameBuilder.hpp"

// The ~25s looping demo script. Channel indices follow the ChannelMapConfig
// defaults (steering ch1/idx0, throttle ch3/idx2, arm ch5/idx4, DRS ch6/idx5,
// gearUp ch7/idx6, gearDown ch8/idx7). Every frame carries all 16 channels at
// explicit raw values, switches at full extremes (172/1811) -- the decoder's
// hysteresis is +/-250 and the first decode seeds from levels, so half-travel
// values would cause missed or phantom edges.

namespace {

constexpr uint32_t kCycleMs = 25000;
constexpr uint16_t kOff = crsf::kChannelRawMin;     // 172, switch off / full left
constexpr uint16_t kOn = crsf::kChannelRawMax;      // 1811, switch on / full right
constexpr uint16_t kMid = crsf::kChannelRawCenter;  // 992, stick center

constexpr uint8_t kSteeringIdx = 0;
constexpr uint8_t kThrottleIdx = 2;
constexpr uint8_t kArmIdx = 4;
constexpr uint8_t kDrsIdx = 5;
constexpr uint8_t kGearUpIdx = 6;
constexpr uint8_t kGearDownIdx = 7;

// Triangle wave over `periodMs`, returned as 0..1000..0 per-mille.
uint32_t trianglePermille(uint32_t t, uint32_t periodMs) {
    const uint32_t phase = t % periodMs;
    const uint32_t half = periodMs / 2;
    return (phase < half) ? (phase * 1000 / half) : ((periodMs - phase) * 1000 / half);
}

// Signed per-mille (-1000..+1000) to raw, using the same piecewise anchors as
// the decoder so the demo hits exact endpoints.
uint16_t analogRaw(int32_t permille) {
    if (permille >= 0) {
        return static_cast<uint16_t>(kMid + permille * (kOn - kMid) / 1000);
    }
    return static_cast<uint16_t>(kMid + permille * (kMid - kOff) / 1000);
}

struct PhaseOutput {
    const char* name;
    bool sendRc;
    bool sendStats;
    uint8_t uplinkLq;
    uint16_t ch[crsf::kNumChannels];
};

PhaseOutput computePhase(uint32_t t) {
    PhaseOutput out{};
    out.sendRc = true;
    out.sendStats = true;
    out.uplinkLq = 100;
    for (size_t i = 0; i < crsf::kNumChannels; ++i) {
        out.ch[i] = kMid;
    }
    out.ch[kArmIdx] = kOff;
    out.ch[kDrsIdx] = kOff;
    out.ch[kGearUpIdx] = kOff;
    out.ch[kGearDownIdx] = kOff;

    if (t < 2000) {
        // Boot / repeat-cycle outage: firmware must sit Safe on the
        // never-received-a-frame (first cycle) or timeout (later cycles) path.
        out.name = "SILENT";
        out.sendRc = false;
        out.sendStats = false;
    } else if (t < 5000) {
        // Steering live while disarmed; ESC stays neutral.
        out.name = "DISARMED_STEERING";
        out.ch[kSteeringIdx] =
            analogRaw(static_cast<int32_t>(trianglePermille(t - 2000, 1500)) * 16 / 10 - 800);
    } else if (t < 6500) {
        // Arm switch ON with throttle at ~60%: ArmGate must block (CLAUDE.md
        // 6.2 "no arm-into-full-throttle"). This runs >5s after boot so it
        // demonstrates the ArmGate, not the ESC's 2s boot hold.
        out.name = "ARM_BLOCKED";
        out.ch[kArmIdx] = kOn;
        out.ch[kThrottleIdx] = analogRaw(600);
    } else if (t < 8000) {
        // Throttle to neutral: arms.
        out.name = "ARM_NEUTRAL";
        out.ch[kArmIdx] = kOn;
    } else if (t < 15000) {
        // Driving: throttle sweeps, two gear-up pulses, DRS open mid-phase.
        out.name = "DRIVING";
        out.ch[kArmIdx] = kOn;
        out.ch[kThrottleIdx] = analogRaw(static_cast<int32_t>(trianglePermille(t - 8000, 2000)));
        out.ch[kSteeringIdx] =
            analogRaw(static_cast<int32_t>(trianglePermille(t - 8000, 3000)) * 8 / 10 - 400);
        out.ch[kDrsIdx] = (t >= 10000 && t < 14000) ? kOn : kOff;
        const bool gearUpPulse = (t >= 9000 && t < 9100) || (t >= 12000 && t < 12100);
        out.ch[kGearUpIdx] = gearUpPulse ? kOn : kOff;
    } else if (t < 17500) {
        // Pure silence, NO LQ=0 burst: exercises the frame-TIMEOUT failsafe
        // path distinctly (~500ms delayed drop).
        out.name = "TIMEOUT_OUTAGE";
        out.sendRc = false;
        out.sendStats = false;
    } else if (t < 19000) {
        // Recovery: stats (LQ=100) are written before RC in tick() -- the
        // LQ latch clears ONLY on good stats, RC frames alone can never
        // clear it. Re-arm after the 150ms confirm window.
        out.name = "RECOVERY_1";
        out.ch[kArmIdx] = kOn;
    } else if (t < 21000) {
        // Hold-position failsafe: LQ=0 stats while RC frames KEEP FLOWING at
        // 50% throttle -- the firmware must drop instantly despite fresh
        // frames (finding A8 mitigation; D7's most valuable demonstration).
        out.name = "HOLD_POSITION_FAILSAFE";
        out.uplinkLq = 0;
        out.ch[kArmIdx] = kOn;
        out.ch[kThrottleIdx] = analogRaw(500);
    } else if (t < 23000) {
        // Recovery with the stick still pulled: blocked until neutral is
        // re-observed (fresh-neutral rule), then arms at t=22s.
        out.name = "RECOVERY_2";
        out.ch[kArmIdx] = kOn;
        out.ch[kThrottleIdx] = (t < 22000) ? analogRaw(500) : kMid;
    } else {
        // Two gear-down pulses so every demo cycle restarts from gear 1
        // (gear deliberately survives failsafe -- without this the cycles
        // would ratchet the gear up to the top).
        out.name = "COOLDOWN";
        out.ch[kArmIdx] = kOn;
        const bool gearDownPulse = (t >= 23200 && t < 23300) || (t >= 24200 && t < 24300);
        out.ch[kGearDownIdx] = gearDownPulse ? kOn : kOff;
    }
    return out;
}

} // namespace

namespace simfeeder {

void tick(uint32_t nowMs) {
    static uint32_t lastRcMs = 0;
    static uint32_t lastStatsMs = 0;
    static const char* lastPhaseName = nullptr;

    const PhaseOutput phase = computePhase(nowMs % kCycleMs);

    if (phase.name != lastPhaseName) {
        lastPhaseName = phase.name;
        Serial.printf("[sim] phase: %s\n", phase.name);
    }

    // Stats before RC (both in code order and, on phase entry, same-tick):
    // recovery depends on an LQ>0 stats frame reaching the receiver no later
    // than the first recovered RC frame.
    if (phase.sendStats && nowMs - lastStatsMs >= 100) {
        lastStatsMs = nowMs;
        crsf::CrsfLinkStatistics stats;
        stats.uplinkRssiAnt1 = 60;
        stats.uplinkRssiAnt2 = 65;
        stats.uplinkLinkQuality = phase.uplinkLq;
        stats.uplinkSnr = 9;
        stats.rfMode = 4;
        uint8_t frame[16];
        const size_t len = crsf::buildLinkStatisticsFrame(stats, frame);
        Serial2.write(frame, len);
    }

    if (phase.sendRc && nowMs - lastRcMs >= 20) {
        lastRcMs = nowMs;
        uint8_t frame[crsf::kRcChannelsFrameLen];
        crsf::buildRcChannelsFrame(phase.ch, frame);
        Serial2.write(frame, sizeof(frame));
    }
}

} // namespace simfeeder

#endif // W17_SIM_CRSF_FEEDER
