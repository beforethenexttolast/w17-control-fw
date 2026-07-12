#pragma once

#include "hal/ISettingsStore.hpp"
#include "settings/Settings.hpp"

namespace settings {

// Outcome of a boot load. Distinguishes "nothing stored yet" (first boot) from
// "something stored but rejected by the guard chain" so the caller (and tests)
// can observe WHY defaults were used, not just that they were.
enum class LoadStatus {
    Loaded,          // a stored blob passed every check and was applied
    DefaultsNoStore, // store empty / unreadable (first boot) -> defaults
    DefaultsInvalid, // a blob was present but failed the guard chain -> defaults
};

struct LoadResult {
    // ALWAYS a wholly valid object: either the fully-validated stored settings
    // or the complete compiled defaults. Never a partial/mixed object.
    Settings settings = kDefaults;
    LoadStatus status = LoadStatus::DefaultsNoStore;

    bool loadedFromStore() const { return status == LoadStatus::Loaded; }
};

// The single boot loader shared by BOTH the delivery firmware (esp32dev) and
// the tuning boot (esp32dev_tuning via ConsoleRunner::loadAtBoot). Reads the
// persisted blob through the store seam and runs the complete validation chain
// inside deserialize() -- length -> CRC -> version -> Settings::valid(). It is
// all-or-nothing: on ANY failure the result carries the complete compiled
// defaults (never a partially valid object, never stored/default sub-configs
// mixed, never corrupt data clamped into apparent validity).
LoadResult loadOrDefault(hal::ISettingsStore& store);

} // namespace settings
