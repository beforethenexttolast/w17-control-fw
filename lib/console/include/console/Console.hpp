#pragma once

#include <cstddef>

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

// Pure tuning command handler. Operates on a caller-owned RAM `Settings`
// (mutations are RAM-only; only `save` asks the caller to persist). No
// hardware, no clock, no I/O -- the caller reads a line off the console and
// hands it here, then prints Result.text and acts on the flags.
//
// Grammar (dotted keys, space-separated): help | status | get [key] |
//   set <key> <value> | save | load | reset
// Keys: steer.center steer.trim batt.ppt gear.<N>.max gear.<N>.expo
//   (channels are read-only: `status` shows the map; there is no set).
//
// `armed` gates MUTATIONS: set/save/load/reset are refused while armed (tuning
// is a pit-lane activity). get/status/help are always allowed. Every `set`
// runs the owning sub-config's valid() and is rejected if it would produce an
// invalid Settings.
class Console {
public:
    Result handleLine(const char* line, settings::Settings& s, bool armed) const;
};

} // namespace console
