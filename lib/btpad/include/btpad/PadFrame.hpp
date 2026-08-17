#pragma once

#include <cstdint>

// BT show-off mode (docs/bt_showoff_design.md §3.4/§4.2). Pure contract header:
// the raw state of one gamepad input report as delivered by the IPadSource seam.
// PROTOTYPE, consumed only under W17_BT_SHOWOFF; every existing build is
// unaffected (nothing here is referenced outside the flag).

namespace btpad {

// Raw value conventions follow Bluepad32 3.10.x (the pinned line, design §4.3 /
// OWNER-DECIDED(BT-8)): sticks -512..+511, analog triggers 0..1023. The decoder
// clamps defensively, so an out-of-convention HAL value degrades to a saturated
// input, never UB. Exact ranges are a verify-at-bench item (design §3.4).
inline constexpr int16_t kStickRawMin = -512;
inline constexpr int16_t kStickRawMax = 511;
inline constexpr int16_t kTriggerRawMax = 1023;

// Button bits in PadFrame::buttons. These are THIS contract's bits: the HAL
// maps the stack's per-button accessors (l1()/r1()/x()...) onto them explicitly,
// bit by bit -- never a raw bitmask copy -- so a constant renumber in the BT
// stack cannot silently remap a control. Values happen to match Bluepad32
// 3.10.x BUTTON_* for legibility. DS4 naming: Bluepad32 maps Square onto its
// "X" slot (Cross=A, Circle=B, Square=X, Triangle=Y).
inline constexpr uint16_t kButtonSquare = 0x0004; // DRS toggle, OWNER-DECIDED(BT-5)
inline constexpr uint16_t kButtonL1 = 0x0010;     // arm ritual half, OWNER-DECIDED(BT-3)
inline constexpr uint16_t kButtonR1 = 0x0020;     // arm ritual half, OWNER-DECIDED(BT-3)

// Misc-button bits in PadFrame::miscButtons (same explicit-mapping rule).
// PS is deliberately unmapped by the decoder: it is the pad's own power/pairing
// button; a long-press powers the pad off, which lands in the disconnect ->
// failsafe path (design §3.2).
inline constexpr uint16_t kMiscPs = 0x0001;
inline constexpr uint16_t kMiscShare = 0x0002;
inline constexpr uint16_t kMiscOptions = 0x0004; // instant disarm, OWNER-DECIDED(BT-3)

// One input report. Plain aggregate; all-zero means "everything released and
// centered", which decodes to all-neutral Controls.
struct PadFrame {
    int16_t leftStickX = 0;  // steering source, -512..+511
    int16_t leftStickY = 0;  // unused in v0
    int16_t rightStickX = 0; // deliberately unused: camera system is OFF in BT
    int16_t rightStickY = 0; // mode (owner scope 2026-08-17), OWNER-DECIDED(BT-9)
    int16_t brake = 0;       // L2 analog, 0..1023 -> brake (never reverse)
    int16_t throttle = 0;    // R2 analog, 0..1023 -> forward throttle
    uint16_t buttons = 0;    // kButton* bits
    uint16_t miscButtons = 0; // kMisc* bits
};

} // namespace btpad
