#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/ISettingsStore.hpp"

namespace settings_hal_esp32 {

// Persists the settings blob in ESP32 NVS via the Arduino Preferences library
// (one blob under a namespace). Opens read-only for load and read-write only
// inside save() to minimize wear/locking.
class Esp32NvsStore : public hal::ISettingsStore {
public:
    bool load(uint8_t* buf, size_t cap, size_t& len) override;
    bool save(const uint8_t* buf, size_t len) override;

private:
    static constexpr const char* kNamespace = "w17tune";
    static constexpr const char* kKey = "settings";
};

} // namespace settings_hal_esp32
