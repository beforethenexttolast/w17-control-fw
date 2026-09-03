#pragma once

#include <cstddef>
#include <cstdint>

#include "settings/Settings.hpp"

namespace console {

// Max output text a single command produces (help is the longest).
inline constexpr size_t kMaxOutput = 512;
inline constexpr size_t kMaxLine = 96; // reject longer lines (flood guard)

// Outcome of handling one command line.
struct Result {
    char text[kMaxOutput] = {0}; // human-readable response to print
    bool settingsChanged = false; // RAM Settings were mutated (caller re-applies live)
    bool saveRequested = false;   // persist current RAM Settings to NVS
    bool loadRequested = false;   // reload from NVS into RAM Settings
};

// Bench-only Hall counters surfaced by `status` (finding R2, OD-11's
// Phase-B interrupt-rate margin measurement -- PHASE_B_FIRST_POWER.md B4.4
// and D8_BENCH_BRINGUP.md Phase 8 name this line as their instrument). The
// caller (main.cpp, tuning env only) reads these off the live
// Esp32HallPulseCounter each poll; Console itself stays hardware-free, so
// every other caller (including every existing test) can omit this
// parameter and get the zero-initialized default.
struct HallDiagnostics {
    uint32_t isrEntries = 0;
    uint32_t lastWindowEntries = 0;
    uint32_t guardFaults = 0;
};

// Pure tuning command handler. Operates on a caller-owned RAM `Settings`
// (mutations are RAM-only; only `save` asks the caller to persist). No
// hardware, no clock, no I/O -- the caller reads a line off the console and
// hands it here, then prints Result.text and acts on the flags.
//
// Grammar (dotted keys, space-separated): help | status | get [key] |
//   set <key> <value> | save | load | reset
// Keys: steer.min steer.max steer.center steer.trim batt.ppt gimbal.decay
//   sound.profile sound.volume gear.<N>.max gear.<N>.expo
//   btpad.max btpad.expo btpad.deadzone btpad.invert btpad.armhold
//   btpad.pairwin (BT show-off tunables -- editable in every tuning build,
//   consumed only under W17_BT_SHOWOFF; docs/bt_showoff_design.md §3.3)
//   (channels are read-only: `status` shows the map; there is no set).
//
// `armed` gates MUTATIONS: set/save/load/reset are refused while armed (tuning
// is a pit-lane activity). get/status/help are always allowed. Every `set`
// runs the owning sub-config's valid() and is rejected if it would produce an
// invalid Settings. `hall` is read-only diagnostics folded into `status`
// (default zeros when the caller has none to give).
class Console {
public:
    Result handleLine(const char* line, settings::Settings& s, bool armed,
                       const HallDiagnostics& hall = HallDiagnostics{}) const;
};

} // namespace console
