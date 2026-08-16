#pragma once

#include <cstddef>
#include <cstdint>

#include "btpad/BtPadConfig.hpp"
#include "failsafe/GimbalDecay.hpp"
#include "gearbox/Gearbox.hpp"
#include "link2/Link2Sender.hpp"
#include "outputs/ServoOutput.hpp"
#include "telemetry/BatteryMonitor.hpp"

namespace settings {

// The bench-tunable subset of the firmware config, aggregated so it can be
// persisted to flash as one versioned blob and edited over the tuning
// console. Only the fields the docs call out as "trim on the bench" are here
// (steering, battery calibration, gear feel, gimbal link-loss decay, engine
// voice + volume, BT-demo envelope); ESC endpoints / failsafe / channel map
// are deliberately NOT tunable (see docs/ROADMAP.md item 6).
struct Settings {
    outputs::ServoConfig steering{};
    gearbox::GearboxConfig gearbox{};
    telemetry::BatteryConfig battery{};
    failsafe::GimbalDecayConfig gimbalDecay{}; // v2: gimbal link-loss decay rate
    // v2: engine voice profile + volume, transmitted to board #2 in every
    // link2 v2 frame (vision decision 15). Board #1 is the ONLY persisted
    // copy -- board #2 stays NVS-free and just consumes the wire bytes.
    link2::SoundConfig sound{};
    // v2: BT show-off tunables (docs/bt_showoff_design.md §3.3). Present
    // UNCONDITIONALLY -- in every build, flag or no flag -- so the blob
    // layout never forks between environments; only the CONSUMPTION of these
    // values sits behind W17_BT_SHOWOFF.
    btpad::BtPadConfig btpad{};

    // Composes each sub-config's own valid(). This is BOTH the compile-time
    // net (static_assert(kDefaults.valid())) and the runtime second line of
    // defense (a persisted blob that fails this is rejected -> defaults).
    constexpr bool valid() const {
        return steering.valid() && gearbox.valid() && battery.valid() &&
               gimbalDecay.valid() && sound.valid() && btpad.valid();
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
// History: v1 = steering + gearbox + battery. v2 carries ALL THREE 2026-08
// field groups on ONE bump: failsafe::GimbalDecayConfig (vision decision 11,
// merged first), link2::SoundConfig (engine voice + volume, vision decision
// 15, unified onto main's v2 at the 2026-08-17 rebase), and
// btpad::BtPadConfig (BT show-off tunables, folded onto the SAME v2 at the
// 2026-08-17 proto/bt-showoff-flagged reconciliation). Each branch had
// bumped 1 -> 2 independently; because no physical blob of any interim v2
// layout was ever flashed or saved, the reconciliation is one shared
// six-group v2 layout -- exactly the documented "second-to-merge renumbers"
// rule, applied twice, and no v3 is needed. A stored v1 blob fails the guard
// chain (its length AND version both differ) -> complete compiled defaults,
// all-or-nothing as always; re-tune and `save` once on v2.
inline constexpr uint8_t kBlobVersion = 2;
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
