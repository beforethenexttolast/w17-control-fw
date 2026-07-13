#pragma once

#include <cstdint>

// R5-b reset diagnostics -- PURE LOGIC, no Arduino/ESP-IDF headers.
//
// Answers, after a reboot: why did this boot happen, what class of reset was
// it, how many boots have occurred in the current RTC-retained power session,
// and was the retained state valid? The real esp_reset_reason() call and the
// RTC_NOINIT_ATTR storage live in src/main.cpp (ESP32-only); this library is
// the platform-independent classifier + session-update math so it can be
// covered by native tests without importing framework code.
//
// RETENTION LIMITS (see main.cpp for where this is stored):
//   * RTC-retained diagnostics are NOT persistent storage. They live in the
//     ESP32 .rtc_noinit segment, retained across software / watchdog / deep-
//     sleep resets but INDETERMINATE after a full power loss -- a power-cycle
//     starts a brand-new session. No power-cycle persistence is claimed.
//   * Brownout may corrupt RTC memory, so brownout-retained data is distrusted
//     (treated as a fresh session), not incremented.
//   * Magic + inverted-magic + version validation is REQUIRED: uninitialized or
//     corrupted RTC bytes must never be trusted.
//   * No NVS write, no flash wear, no heap, no dynamic strings are involved.

namespace reset_diag {

// Portable mirror of the pinned Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7 esp32-target
// esp_reset_reason_t VALUES (esp_system.h). Kept as an independent type so native
// tests never include ESP-IDF headers; main.cpp maps the real esp_reset_reason_t
// onto this 1:1 with a switch whose completeness is checked at compile time.
// Values are pinned to the enum members below and MUST match esp_system.h:
//   UNKNOWN=0 POWERON=1 EXT=2 SW=3 PANIC=4 INT_WDT=5 TASK_WDT=6 WDT=7
//   DEEPSLEEP=8 BROWNOUT=9 SDIO=10
enum class RawResetReason : uint8_t {
    Unknown   = 0,  // ESP_RST_UNKNOWN  -- reason could not be determined
    PowerOn   = 1,  // ESP_RST_POWERON  -- power-on event
    Ext       = 2,  // ESP_RST_EXT      -- external pin (not applicable on plain ESP32)
    Sw        = 3,  // ESP_RST_SW       -- software reset via esp_restart
    Panic     = 4,  // ESP_RST_PANIC    -- exception / panic
    IntWdt    = 5,  // ESP_RST_INT_WDT  -- interrupt watchdog
    TaskWdt   = 6,  // ESP_RST_TASK_WDT -- task watchdog (the R5-a control-loop TWDT)
    Wdt       = 7,  // ESP_RST_WDT      -- other watchdogs
    DeepSleep = 8,  // ESP_RST_DEEPSLEEP-- exit from deep sleep
    Brownout  = 9,  // ESP_RST_BROWNOUT -- brownout (software or hardware)
    Sdio      = 10, // ESP_RST_SDIO     -- reset over SDIO
};

// Stable internal classification. TaskWdt and Panic are DELIBERATELY distinct:
// a watchdog-induced fault may report either on real hardware, and R5-b must not
// collapse them (see the task brief). OtherWatchdog / Sdio exist so every pinned
// enum value maps somewhere explicit -- nothing falls through silently.
enum class ResetClass : uint8_t {
    PowerOn,            // POWER_ON
    Software,           // SOFTWARE
    Panic,              // PANIC
    InterruptWatchdog,  // INT_WDT
    TaskWatchdog,       // TASK_WDT
    OtherWatchdog,      // WDT
    Brownout,           // BROWNOUT
    DeepSleep,          // DEEP_SLEEP
    External,           // EXTERNAL
    Sdio,               // SDIO
    Unknown,            // UNKNOWN
};

// Map a raw pinned reset reason to its stable class. Every RawResetReason value
// is handled explicitly (compile covers all enumerators).
ResetClass classify(RawResetReason raw);

// Concise, stable text label for a class (e.g. "TASK_WDT"). Never null; an
// out-of-range class defensively returns "UNKNOWN".
const char* label(ResetClass c);

// --- RTC-retained session state ---------------------------------------------
// PLAIN AGGREGATE ON PURPOSE: no constructor, no default member initializers, so
// an instance declared RTC_NOINIT_ATTR in main.cpp is left untouched by C++
// startup (its bytes survive warm resets and are garbage after power loss). All
// initialization goes through updateSession() below -- never rely on the members
// being zeroed.
struct SessionState {
    uint32_t magic;        // == kMagic when initialized by this firmware
    uint32_t magicInverse; // == ~kMagic; guards against single-bit RTC corruption
    uint32_t version;      // == kVersion; guards against a layout change
    uint32_t bootCount;    // boots in the current retained power session (saturating)
    uint8_t  lastResetClass; // most recent ResetClass, stored as its integral value
};

// Validity marker for SessionState. A single word plus its inverse plus a
// version is small but rejects uninitialized/corrupted RTC bytes far better
// than a lone magic. Bump kVersion if the struct layout ever changes.
constexpr uint32_t kMagic = 0x57313742u;         // "W17B"
constexpr uint32_t kVersion = 1u;

// Saturating ceiling for the boot counter. Deterministic overflow policy: the
// counter clamps here instead of wrapping (no undefined/implementation-defined
// overflow, and "many boots" never looks like "few boots").
constexpr uint32_t kMaxBootCount = 0xFFFFFFFFu;

// True iff the retained bytes carry this firmware's valid, current-version
// marker. Uninitialized or corrupted RTC memory fails all three checks.
bool isValid(const SessionState& s);

// Result of a single boot's diagnostic update (what main.cpp prints).
struct BootReport {
    ResetClass reason;    // classified reset reason for THIS boot
    uint32_t   bootCount; // boot count AFTER this boot's update
    bool       incomingValid; // was the retained state valid on entry?
    bool       freshSession;  // did this boot start a new session (vs. continue one)?
};

// PURE session update. Reads the (possibly garbage) retained state, decides
// whether to continue or restart the session, writes the new state back into
// `state`, and returns a report. No hardware, no I/O, no allocation.
//
//   * Power-on            -> always a fresh session (bootCount = 1).
//   * Brownout            -> fresh session (RTC may be corrupted; distrust it).
//   * Invalid magic/ver   -> fresh session (uninitialized or corrupted).
//   * Any other reset with valid retained state -> increment (saturating),
//     record the reason. Software / panic / task-WDT / int-WDT / deep-sleep /
//     external / SDIO / unknown all continue the session.
//
// `freshSession` is true exactly when the session was (re)started here; main.cpp
// reports "retained=no" in that case and "retained=yes" when the counter carried
// forward. `incomingValid` reflects the raw validity of the bytes on entry
// (useful even when a fresh session was forced for another reason).
BootReport updateSession(SessionState& state, ResetClass reason);

} // namespace reset_diag
