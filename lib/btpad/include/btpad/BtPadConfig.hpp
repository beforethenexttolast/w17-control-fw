#pragma once

#include <cstdint>

namespace btpad {

// The bench-tunable BT show-off values (design §3.3/§3.4). Aggregated into
// settings::Settings UNCONDITIONALLY -- in every build, flag or no flag -- so
// the persisted blob layout never forks between environments; only the
// CONSUMPTION of these values is gated on W17_BT_SHOWOFF (design §3.3).
//
// Failsafe timing is deliberately NOT here (non-tunable in both modes, same as
// the CRSF path -- design §3 row 1), and neither is the arm-neutral window
// (channels::ArmGateConfig, shared).
struct BtPadConfig {
    // Demo envelope: driveMode is pinned to TRAINING in the decoder, and the
    // BT control tick shapes throttle with THESE GearParams instead of the
    // compiled kTrainingGearParams. Defaults mirror training's {400, 50}.
    // OWNER-PENDING(BT-4): proposed demo-cap values, owner-tunable at the bench.
    int16_t maxOutput = 400;  // 1..1000, same rule as gearbox::GearParams
    uint8_t expoPercent = 50; // 0..100, same rule as gearbox::GearParams

    // Steering feel. Deadzone is in normalized units (of +/-1000 half-travel);
    // default 40 =~ 4%, covers DS4 stick drift (design §3.3 proposed value).
    // The decoder RESCALES outside the deadzone, so full deflection still
    // reaches +/-1000 ("full range", design §3.3).
    int16_t steerDeadzone = 40; // 0..300 (above 30% of travel is surely a typo)
    uint8_t invertSteering = 0; // 0/1 (u8, not bool: deterministic blob bytes)

    // Arm ritual: L1+R1 held simultaneously this long -> ritual latch ON
    // (design §3.2). OWNER-PENDING(BT-3): proposed 1000 ms default.
    // Floor 100 ms so a config typo cannot create a near-instant accidental
    // arm; ceiling 10 s keeps the ritual physically performable.
    uint16_t armHoldMs = 1000; // 100..10000

    // Pairing lockdown: new-pairing window after a BT-mode boot; afterwards
    // the wrapper calls enableNewBluetoothConnections(false) (design §6.1).
    // OWNER-PENDING(BT-10): proposed 30 s default; 0 = never accept new
    // pairings while a bond exists. Lockout effectiveness is a bench gate item.
    uint16_t pairWindowMs = 30000; // 0..60000

    // static_assert'd via settings::kDefaults.valid() at the aggregation site;
    // also the runtime guard when a persisted blob is deserialized.
    constexpr bool valid() const {
        return maxOutput >= 1 && maxOutput <= 1000 && expoPercent <= 100 &&
               steerDeadzone >= 0 && steerDeadzone <= 300 && invertSteering <= 1 &&
               armHoldMs >= 100 && armHoldMs <= 10000 && pairWindowMs <= 60000;
    }
};

} // namespace btpad
