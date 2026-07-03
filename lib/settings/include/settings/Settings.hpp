#pragma once

#include <cstddef>
#include <cstdint>

#include "gearbox/Gearbox.hpp"
#include "outputs/ServoOutput.hpp"
#include "telemetry/BatteryMonitor.hpp"

namespace settings {

// The bench-tunable subset of the firmware config, aggregated so it can be
// persisted to flash as one versioned blob and edited over the tuning
// console. Only the fields the docs call out as "trim on the bench" are here
// (steering, battery calibration, gear feel); ESC endpoints / failsafe /
// channel map are deliberately NOT tunable (see docs/ROADMAP.md item 6).
struct Settings {
    outputs::ServoConfig steering{};
    gearbox::GearboxConfig gearbox{};
    telemetry::BatteryConfig battery{};

    // Composes each sub-config's own valid(). This is BOTH the compile-time
    // net (static_assert(kDefaults.valid())) and the runtime second line of
    // defense (a persisted blob that fails this is rejected -> defaults).
    constexpr bool valid() const {
        return steering.valid() && gearbox.valid() && battery.valid();
    }
};

// The compile-time defaults: today's values. static_assert below guarantees a
// bad default can't compile -- the safety net that used to live as per-module
// asserts in main.cpp.
inline constexpr Settings kDefaults{};
static_assert(kDefaults.valid(), "default Settings are invalid");

// --- Versioned blob format: [version][struct bytes][crc8], CRC over
// [version + struct bytes]. Bump kBlobVersion on ANY layout change so an old
// persisted blob fails the version check and falls back to defaults. ---
inline constexpr uint8_t kBlobVersion = 1;
inline constexpr size_t kBlobLen = 1 + sizeof(Settings) + 1;

// CRC-8 poly 0xD5, the same algorithm CRSF/link2 use, DUPLICATED here so
// lib/settings stays dependency-light (a test cross-checks it against crsf).
uint8_t computeCrc8(const uint8_t* data, size_t len);

// Serializes `s` into out[kBlobLen]. Returns kBlobLen.
size_t serialize(const Settings& s, uint8_t out[kBlobLen]);

// Deserializes a blob into `out`. Validation order (mirrors Link2Codec):
// length -> CRC -> version -> Settings::valid(). Returns true only if ALL
// pass; on any failure `out` is left untouched (caller keeps kDefaults).
bool deserialize(const uint8_t* data, size_t len, Settings& out);

} // namespace settings
