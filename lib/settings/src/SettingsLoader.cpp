#include "settings/SettingsLoader.hpp"

namespace settings {

LoadResult loadOrDefault(hal::ISettingsStore& store) {
    LoadResult result;
    result.settings = kDefaults; // start wholly-default; overwritten only on full success

    uint8_t buf[kBlobLen];
    size_t len = 0;
    if (!store.load(buf, sizeof(buf), len)) {
        result.status = LoadStatus::DefaultsNoStore; // first boot / read error
        return result;
    }

    // deserialize() is the ONE validation chain (length -> CRC -> version ->
    // Settings::valid()); it leaves `loaded` untouched on any failure. We only
    // adopt it on a clean true, so a failure keeps the complete defaults.
    Settings loaded;
    if (!deserialize(buf, len, loaded)) {
        result.status = LoadStatus::DefaultsInvalid;
        return result;
    }

    result.settings = loaded;
    result.status = LoadStatus::Loaded;
    return result;
}

} // namespace settings
