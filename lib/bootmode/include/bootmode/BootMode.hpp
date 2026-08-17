#pragma once

#include <cstdint>

// Boot-selected operating mode (showcase-mode design, owner-accepted
// 2026-08-17; vision decision 2 "core-if-cheap"). SHOWCASE is a stationary
// demo: lights + engine sound (+ live camera when the ground station is up),
// with drive authority STRUCTURALLY off -- see the policies below.
//
// Doctrine (shared with the BT show-off design, docs/bt_showoff_design.md on
// its proto branch, section 2): the mode is resolved EXACTLY ONCE at boot
// from a physical selector, there is no runtime switching in either
// direction (changing mode = key cycle), and every fault in the selector
// path fails toward Drive -- the car may come up LESS entertaining than
// asked, never MORE armed.
//
// Selector hardware status (D3): the eventual selector is a physical strap
// shared with the BT mode's boot selector; its PIN is OWNER-PENDING (the
// BT-2 candidates are GPIO27/32/33, to be reconciled against PinMap.hpp and
// the wiring atlas, and the strap joins the A2 continuity-matrix scope,
// F20). The NVS override, if any, belongs to the BT-branch settings-blob
// reconciliation -- NO settings-blob change ships with this seam. Until the
// strap is wired, main.cpp injects a compile-time StrapReading::Floating,
// which resolves to Drive, so every shipped build behaves exactly as before
// this lib existed.
//
// Pure logic: no hardware headers; everything constexpr and natively tested
// (test/test_bootmode).
namespace bootmode {

// The boot modes this firmware knows. Selector labels on the physical part
// are LAPTOP (= Drive, the normal CRSF drive mode) and SHOW (= Showcase).
// If the BT show-off mode is ever approved it adds a third position (SOLO);
// it deliberately does NOT exist here yet -- showcase must never depend on
// the Bluepad32 branch.
enum class BootMode : uint8_t {
    Drive,    // normal CRSF drive mode (LAPTOP position; also every fault)
    Showcase, // stationary demo: arm structurally off, link2 showcase bit set
};

// What the (future) strap read yields after debounce/classification. The
// electrical mapping of levels to positions is decided with the pin (D3 /
// BT-2); this enum is the post-classification abstraction the resolver
// consumes, so the resolver truth table is testable before any pin exists.
enum class StrapReading : uint8_t {
    Floating,      // unwired, broken, or ambiguous -- MUST resolve to Drive
    DrivePosition, // selector at LAPTOP
    ShowPosition,  // selector at SHOW
};

// Strap -> mode. Total over uint8_t: ONLY an unambiguous SHOW position
// selects Showcase; DrivePosition, Floating, and any out-of-range value
// (a corrupted read can be cast straight in) all resolve to Drive. This is
// the fail-toward-drive rule (BT design 2.2-A): a broken selector makes the
// car less entertaining, never more armed.
constexpr BootMode resolve(StrapReading reading) {
    return reading == StrapReading::ShowPosition ? BootMode::Showcase : BootMode::Drive;
}

// --- Policy 1: the arm input (the one line that de-fangs showcase) ---------
//
// What main.cpp must feed ArmGate::update() as `armSwitchOn`. In SHOWCASE
// the decoded arm switch is STRUCTURALLY ignored -- this function returns
// false no matter what the handset says, so the gate's neutral-seen latch
// can never set, `armed` can never assert, and the existing throttle chain
//   baseCommanded = (active && armed) ? modeShaped : 0
// pins the ESC command to 0 (applyBoost(0) == 0 is already test-pinned), so
// the ESC never leaves neutral. Drive authority is off BY CONSTRUCTION --
// no new gate, no parallel path, nothing to keep in sync with the arm gate.
// In Drive mode this is a transparent pass-through: byte-identical behavior.
constexpr bool armSwitchInput(BootMode mode, bool decodedArmSwitch) {
    return mode == BootMode::Showcase ? false : decodedArmSwitch;
}

// --- Policy 2: the link2 failsafe flag (owner decision D4) -----------------
//
// What the 20 Hz link2 frame reports in its `failsafe` flag when the CRSF
// failsafe FSM is Safe. In SHOWCASE there is no drive authority to lose, so
// a shelf demo that never had a CRSF link this boot must not hazard-blink
// -- but a link that existed and DIED must still be told (a dead table
// radio has to look dead):
//
//     failsafe-on-the-wire = fsmSafe && everLinkedThisBoot
//
// This is the exact NeverConnected-vs-Lost distinction board #2 already
// applies to the link2 stream itself, applied one level up to the CRSF
// link. `everLinkedThisBoot` is FailsafeStateMachine::hasEverReceivedFrame()
// -- latched on the first valid RC frame, never reset until reboot.
//
// In Drive mode the flag is fsmSafe, UNCHANGED -- this exception is
// deliberately showcase-scoped and the normal-mode wire stays byte-identical
// (test-pinned). Truth table: docs/link2_protocol.md, state-matrix note.
constexpr bool link2FailsafeFlag(BootMode mode, bool fsmSafe, bool everLinkedThisBoot) {
    return mode == BootMode::Showcase ? (fsmSafe && everLinkedThisBoot) : fsmSafe;
}

// --- Policy 3: the link2 showcase flag (modeFlags bit0) --------------------
//
// Truth on the wire: the bit says exactly "board #1 booted in SHOWCASE",
// nothing else, in every frame of the boot including failsafe frames (the
// mode is boot state, not drive state). Board #2 keys ignition on
// `armed || showcase` and owns the curated presentation.
constexpr bool link2ShowcaseFlag(BootMode mode) { return mode == BootMode::Showcase; }

} // namespace bootmode
