#pragma once

#include <cstddef>
#include <cstdint>

// Boot-selected operating mode. THE one boot-mode seam for every mode this
// firmware knows (showcase-mode design, owner-accepted 2026-08-17; BT
// show-off design docs/bt_showoff_design.md, ratified the same day; unified
// here at the 2026-08-17 three-mode reconciliation -- there is deliberately
// no second resolver anywhere).
//
// Doctrine (shared by both designs): the mode is resolved EXACTLY ONCE at
// boot from a physical selector, there is no runtime switching in either
// direction (changing mode = key cycle), and every fault in the selector
// path fails toward Drive -- the car may come up LESS entertaining than
// asked, never MORE armed.
//
// Selector hardware status: ONE physical selector shared by all modes (D3 /
// BT-1). The LAPTOP/SOLO strap pin is DECIDED -- GPIO27, OWNER-DECIDED(BT-2)
// on the BT branch (config/PinMap.hpp kBtModeStrapPin; wiring-atlas
// reconciliation + the A2/F20 continuity-matrix note remain wiring-time
// tasks). It is WIRED only by the W17_BT_SHOWOFF prototype envs; every
// delivery-lineage build keeps the compile-time StrapReading::Floating
// injection in main.cpp, which resolves to Drive -- byte-identical behavior.
// The NVS override, if any, stays deferred (no settings-blob change here).
//
// OWNER-PENDING(D3-SHOW-SELECT) -- how SHOWCASE is selected, the one open
// selector corner: the decided GPIO27 strap is a TWO-position switch
// (LAPTOP = open/pull-up, SOLO = closed to GND), which cannot express a
// third position. Proposal: replace it with an SP3T slide switch, common to
// GND, throws on GPIO27 (SOLO) and GPIO32 (SHOW), center = LAPTOP (both
// open/pulled-up); both-low is electrically impossible from the part but a
// harness fault -- classify it Floating -> Drive, same fail direction as
// everything else. Alternatives: a second 2-pos switch on GPIO32, or an ADC
// ladder on one pin (rejected: analog thresholds in the selector path).
// Until the owner picks, SHOWCASE remains selectable only via the
// compile-time injection (exactly how the native tests and any bench build
// exercise it) -- classifyStrapLevels() below covers the wired
// LAPTOP/SOLO pin only, and D3-SHOW-SELECT adds the second-pin
// classification when decided.
//
// Pure logic: no hardware headers; everything constexpr and natively tested
// (test/test_bootmode).
namespace bootmode {

// The boot modes this firmware knows. Selector labels on the physical part
// are LAPTOP (= Drive, the normal CRSF drive mode), SHOW (= Showcase) and
// SOLO (= BtSolo, the BT show-off mode -- owner-ratified 2026-08-17,
// docs/bt_showoff_design.md; the mode's code exists only under
// W17_BT_SHOWOFF, but the enum value exists everywhere so there is ONE mode
// model, not a parallel one).
enum class BootMode : uint8_t {
    Drive,    // normal CRSF drive mode (LAPTOP position; also every fault)
    Showcase, // stationary demo: arm structurally off, link2 showcase bit set
    BtSolo,   // BT pad drive: CRSF UART never opened, arm via pad ritual only
};

// What the strap read yields after debounce/classification. The electrical
// mapping of levels to positions for the SHOW position is OWNER-PENDING
// (D3-SHOW-SELECT above); LAPTOP/SOLO ride the decided GPIO27 strap.
enum class StrapReading : uint8_t {
    Floating,      // unwired, broken, or ambiguous -- MUST resolve to Drive
    DrivePosition, // selector at LAPTOP
    ShowPosition,  // selector at SHOW
    SoloPosition,  // selector at SOLO (BT show-off)
};

// Strap -> mode. Total over uint8_t: ONLY an unambiguous SHOW position
// selects Showcase and ONLY an unambiguous SOLO position selects BtSolo;
// DrivePosition, Floating, and any out-of-range value (a corrupted read can
// be cast straight in) all resolve to Drive. This is the fail-toward-drive
// rule (BT design 2.2-A): a broken selector makes the car less
// entertaining, never more armed.
constexpr BootMode resolve(StrapReading reading) {
    return reading == StrapReading::ShowPosition
               ? BootMode::Showcase
               : (reading == StrapReading::SoloPosition ? BootMode::BtSolo : BootMode::Drive);
}

// --- Strap sampling policy + classification (LAPTOP/SOLO pin, GPIO27) ------
//
// The boot-time read the W17_BT_SHOWOFF envs perform (BT design §2.2
// mechanism A, OWNER-DECIDED(BT-1)): enable the internal pull-up, wait
// kStrapSettleMs, take kStrapSampleCount samples kStrapSampleSpacingMs
// apart, classify by STRICT majority. Odd count -> no tie in practice; the
// classifier still defines ties as Floating (-> Drive).
inline constexpr uint32_t kStrapSettleMs = 10;
inline constexpr size_t kStrapSampleCount = 9;
inline constexpr uint32_t kStrapSampleSpacingMs = 1;

// Sampled electrical levels (true = HIGH) -> position. Pull-up idle HIGH =
// LAPTOP/Drive; switch closed to GND = SOLO. No samples, null, or a tie
// classify as Floating -- every fault direction lands on Drive via
// resolve(). (The SHOW position classifies on a second input once
// D3-SHOW-SELECT is decided; it deliberately has no electrical mapping here
// yet.)
constexpr StrapReading classifyStrapLevels(const bool* levelsHigh, size_t sampleCount) {
    if (levelsHigh == nullptr || sampleCount == 0) {
        return StrapReading::Floating;
    }
    size_t lows = 0;
    for (size_t i = 0; i < sampleCount; ++i) {
        if (!levelsHigh[i]) {
            ++lows;
        }
    }
    if (lows * 2 > sampleCount) {
        return StrapReading::SoloPosition; // strict majority LOW: switch at SOLO
    }
    if ((sampleCount - lows) * 2 > sampleCount) {
        return StrapReading::DrivePosition; // strict majority HIGH: LAPTOP / open
    }
    return StrapReading::Floating; // tie: ambiguous -> Drive via resolve()
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
// In BtSolo it is ALSO a pass-through: there the decoded switch is the pad
// arm RITUAL (btpad::PadDecoder, L1+R1 hold / OPTIONS / strict
// clear-on-failsafe), which gates arming upstream -- double-gating here
// would add nothing and hide the ritual's own semantics (test-pinned in
// test_btpad).
constexpr bool armSwitchInput(BootMode mode, bool decodedArmSwitch) {
    return mode == BootMode::Showcase ? false : decodedArmSwitch;
}

// --- Policy 2: the link2 failsafe flag (owner decision D4) -----------------
//
// What the 20 Hz link2 frame reports in its `failsafe` flag when the input
// failsafe FSM is Safe. In SHOWCASE there is no drive authority to lose, so
// a shelf demo that never had a CRSF link this boot must not hazard-blink
// -- but a link that existed and DIED must still be told (a dead table
// radio has to look dead):
//
//     failsafe-on-the-wire = fsmSafe && everLinkedThisBoot
//
// This is the exact NeverConnected-vs-Lost distinction board #2 already
// applies to the link2 stream itself, applied one level up to the CRSF
// link. `everLinkedThisBoot` is FailsafeStateMachine::hasEverLinked() --
// latched on the first PROVEN link this boot (the first completed
// Safe -> Active link proof), never reset until reboot. Proof completion,
// not first raw frame, is the latch point (2026-08-20 hardening): a lone
// CRC-colliding garbage frame is not "a link that existed", and must not
// leave a shelf demo hazard-blinking forever; any real radio that connects
// completes the ~150 ms proof and is still "told" when it dies.
//
// In Drive mode the flag is fsmSafe, UNCHANGED -- this exception is
// deliberately showcase-scoped and the normal-mode wire stays byte-identical
// (test-pinned). In BtSolo the flag is ALSO plain fsmSafe: the pad modes
// HAVE drive authority, and "awaiting controller" SHOULD read as failsafe on
// board #2's lights until the dedicated modeFlags-bit1 surface is emitted
// (a future slice -- OWNER-DECIDED(BT-7), design §6.3). Truth table:
// docs/link2_protocol.md, state-matrix note.
constexpr bool link2FailsafeFlag(BootMode mode, bool fsmSafe, bool everLinkedThisBoot) {
    return mode == BootMode::Showcase ? (fsmSafe && everLinkedThisBoot) : fsmSafe;
}

// --- Policy 3: the link2 showcase flag (modeFlags bit0) --------------------
//
// Truth on the wire: the bit says exactly "board #1 booted in SHOWCASE",
// nothing else, in every frame of the boot including failsafe frames (the
// mode is boot state, not drive state). Board #2 keys ignition on
// `armed || showcase` and owns the curated presentation. BtSolo NEVER sets
// it (the ratified showcase/BT bit split: bit0 belongs to showcase, bit1 --
// reserved, unemitted today -- to the BT pairing surface).
constexpr bool link2ShowcaseFlag(BootMode mode) { return mode == BootMode::Showcase; }

} // namespace bootmode
