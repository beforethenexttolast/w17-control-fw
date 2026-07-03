#include "settings_hal_esp32/Esp32NvsStore.hpp"

#include <Preferences.h>

namespace settings_hal_esp32 {

bool Esp32NvsStore::load(uint8_t* buf, size_t cap, size_t& len) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/true)) {
        return false; // namespace doesn't exist yet (first boot)
    }
    const size_t stored = prefs.getBytesLength(kKey);
    if (stored == 0 || stored > cap) {
        prefs.end();
        return false;
    }
    len = prefs.getBytes(kKey, buf, cap);
    prefs.end();
    return len == stored;
}

bool Esp32NvsStore::save(const uint8_t* buf, size_t len) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
        return false;
    }
    const size_t written = prefs.putBytes(kKey, buf, len);
    prefs.end();
    return written == len;
}

} // namespace settings_hal_esp32
