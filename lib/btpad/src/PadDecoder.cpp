#include "btpad/PadDecoder.hpp"

namespace btpad {

PadDecoder::PadDecoder(BtPadConfig config) : config_(config) {}

int16_t PadDecoder::normalizeStick(int16_t raw) {
    if (raw < kStickRawMin) raw = kStickRawMin;
    if (raw > kStickRawMax) raw = kStickRawMax;
    if (raw >= 0) {
        return static_cast<int16_t>((static_cast<int32_t>(raw) * 1000) / kStickRawMax);
    }
    return static_cast<int16_t>((static_cast<int32_t>(raw) * 1000) /
                                -static_cast<int32_t>(kStickRawMin));
}

int16_t PadDecoder::normalizeTrigger(int16_t raw) {
    if (raw < 0) raw = 0;
    if (raw > kTriggerRawMax) raw = kTriggerRawMax;
    return static_cast<int16_t>((static_cast<int32_t>(raw) * 1000) / kTriggerRawMax);
}

int16_t PadDecoder::applyDeadzone(int16_t normalized) const {
    const int16_t dz = config_.steerDeadzone;
    if (dz <= 0) {
        return normalized;
    }
    const int32_t mag = normalized < 0 ? -static_cast<int32_t>(normalized) : normalized;
    if (mag <= dz) {
        return 0;
    }
    // dz <= 300 by BtPadConfig::valid(), so the divisor is always >= 700.
    const int32_t rescaled = ((mag - dz) * 1000) / (1000 - dz);
    return static_cast<int16_t>(normalized < 0 ? -rescaled : rescaled);
}

void PadDecoder::clearRitualAndRequireRelease() {
    armLatched_ = false;
    holdActive_ = false;
    requireReleaseBeforeRearm_ = true;
}

void PadDecoder::forceDisarmRitual() {
    clearRitualAndRequireRelease();
}

channels::Controls PadDecoder::decode(const PadFrame& frame, uint32_t nowMs) {
    channels::Controls c;

    // --- Steering: left stick X, deadzone + optional inversion. ---
    int16_t steer = applyDeadzone(normalizeStick(frame.leftStickX));
    if (config_.invertSteering != 0) {
        steer = static_cast<int16_t>(-steer);
    }
    c.steering = steer;

    // --- Throttle: net = R2 - L2 (forward minus brake; the ESC runs in
    // forward/brake mode, so L2 is brake, never reverse -- design §3.4 and
    // the gearbox reverse note in lib/gearbox/include/gearbox/Gearbox.hpp).
    // Each term is 0..1000, so the net is inherently within [-1000, +1000].
    c.throttle = static_cast<int16_t>(normalizeTrigger(frame.throttle) -
                                      normalizeTrigger(frame.brake));

    // --- Arm ritual (design §3.2, OWNER-DECIDED(BT-3)). ---
    const bool bothHeld =
        (frame.buttons & kButtonL1) != 0 && (frame.buttons & kButtonR1) != 0;
    if (requireReleaseBeforeRearm_) {
        // After any latch-clear, a frame with the pair released must be seen
        // before hold timing may begin (fresh deliberate ritual, never a
        // carried-over grip).
        if (!bothHeld) {
            requireReleaseBeforeRearm_ = false;
        }
    } else if (bothHeld) {
        if (!holdActive_) {
            holdActive_ = true;
            holdStartMs_ = nowMs;
        }
        if (!armLatched_ && nowMs - holdStartMs_ >= config_.armHoldMs) {
            armLatched_ = true; // >= : exactly armHoldMs arms (test-pinned)
        }
    } else {
        holdActive_ = false;
    }

    // OPTIONS: instant disarm, applied AFTER hold processing so it wins over a
    // hold completing in the same frame. Level-checked (idempotent), and it
    // also demands a release-before-rearm, so holding L1+R1 across an OPTIONS
    // tap cannot silently re-arm one hold-period later.
    if ((frame.miscButtons & kMiscOptions) != 0) {
        clearRitualAndRequireRelease();
    }
    c.armSwitch = armLatched_;

    // --- DRS: Square press-edge toggle (cosmetic; OWNER-DECIDED(BT-5)). ---
    const bool squarePressed = (frame.buttons & kButtonSquare) != 0;
    if (squarePressed && !squareWasPressed_) {
        drsToggleState_ = !drsToggleState_;
    }
    squareWasPressed_ = squarePressed;
    c.drsSwitch = drsToggleState_;

    // --- Demo-envelope pins (design §3.3). ---
    c.driveMode = 0; // TRAINING pinned; ERS (driveMode 2) unreachable. OWNER-DECIDED(BT-4)
    // c.pan / c.tilt stay 0: camera system OFF in BT mode (owner scope
    // 2026-08-17), gimbal held at center. OWNER-DECIDED(BT-9)
    // c.gearUpEdge / c.gearDownEdge stay false: paddles inert in v0. OWNER-DECIDED(BT-4)
    // c.boostHeld / c.overtakeHeld stay false: no ERS controls on the pad.

    return c;
}

} // namespace btpad
