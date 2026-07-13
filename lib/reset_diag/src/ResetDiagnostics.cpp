#include "reset_diag/ResetDiagnostics.hpp"

namespace reset_diag {

ResetClass classify(RawResetReason raw) {
    // Every pinned RawResetReason value is mapped deliberately; there is no
    // default fall-through for known values (an unrecognized numeric value
    // reaching the default is treated as Unknown -- see the switch tail).
    switch (raw) {
        case RawResetReason::PowerOn:   return ResetClass::PowerOn;
        case RawResetReason::Sw:        return ResetClass::Software;
        case RawResetReason::Panic:     return ResetClass::Panic;
        case RawResetReason::IntWdt:    return ResetClass::InterruptWatchdog;
        case RawResetReason::TaskWdt:   return ResetClass::TaskWatchdog;
        case RawResetReason::Wdt:       return ResetClass::OtherWatchdog;
        case RawResetReason::Brownout:  return ResetClass::Brownout;
        case RawResetReason::DeepSleep: return ResetClass::DeepSleep;
        case RawResetReason::Ext:       return ResetClass::External;
        case RawResetReason::Sdio:      return ResetClass::Sdio;
        case RawResetReason::Unknown:   return ResetClass::Unknown;
    }
    // Only reached if a value outside the pinned enum is cast in (e.g. a future
    // ESP-IDF adds a reason main.cpp maps to an out-of-range Raw value).
    return ResetClass::Unknown;
}

const char* label(ResetClass c) {
    switch (c) {
        case ResetClass::PowerOn:           return "POWER_ON";
        case ResetClass::Software:          return "SOFTWARE";
        case ResetClass::Panic:             return "PANIC";
        case ResetClass::InterruptWatchdog: return "INT_WDT";
        case ResetClass::TaskWatchdog:      return "TASK_WDT";
        case ResetClass::OtherWatchdog:     return "WDT";
        case ResetClass::Brownout:          return "BROWNOUT";
        case ResetClass::DeepSleep:         return "DEEP_SLEEP";
        case ResetClass::External:          return "EXTERNAL";
        case ResetClass::Sdio:              return "SDIO";
        case ResetClass::Unknown:           return "UNKNOWN";
    }
    return "UNKNOWN"; // defensive: out-of-range class value
}

bool isValid(const SessionState& s) {
    return s.magic == kMagic &&
           s.magicInverse == static_cast<uint32_t>(~kMagic) &&
           s.version == kVersion;
}

namespace {

// Stamp a valid marker and start the counter at 1 for this boot.
void startFreshSession(SessionState& state, ResetClass reason) {
    state.magic = kMagic;
    state.magicInverse = static_cast<uint32_t>(~kMagic);
    state.version = kVersion;
    state.bootCount = 1u;
    state.lastResetClass = static_cast<uint8_t>(reason);
}

// Deterministic saturating increment -- never wraps.
uint32_t saturatingIncrement(uint32_t count) {
    return (count >= kMaxBootCount) ? kMaxBootCount : count + 1u;
}

} // namespace

BootReport updateSession(SessionState& state, ResetClass reason) {
    const bool incomingValid = isValid(state);

    // Fresh-session triggers: a new power session (POWER_ON), a brownout (RTC
    // may be corrupted, so distrust the retained bytes), or invalid/uninitialized
    // retained state. All others continue the current session.
    const bool forceFresh = (reason == ResetClass::PowerOn) ||
                            (reason == ResetClass::Brownout) ||
                            !incomingValid;

    if (forceFresh) {
        startFreshSession(state, reason);
    } else {
        state.bootCount = saturatingIncrement(state.bootCount);
        state.lastResetClass = static_cast<uint8_t>(reason);
        // magic / magicInverse / version already valid -- leave them.
    }

    BootReport report;
    report.reason = reason;
    report.bootCount = state.bootCount;
    report.incomingValid = incomingValid;
    report.freshSession = forceFresh;
    return report;
}

} // namespace reset_diag
