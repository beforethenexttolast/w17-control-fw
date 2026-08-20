#include "failsafe/FailsafeStateMachine.hpp"

namespace failsafe {

FailsafeStateMachine::FailsafeStateMachine(Config config) : config_(config) {}

State FailsafeStateMachine::update(uint32_t nowMs, bool frameArrivedThisTick, bool rxFailsafeFlag) {
    // Capture the PREVIOUS arrival time before stamping this tick's frame:
    // the intra-proof gap below must measure real silence between frames,
    // not the zero distance from a frame to itself.
    const uint32_t previousFrameMs = lastFrameMs_;
    const bool hadFrameBefore = everReceivedFrame_;

    if (frameArrivedThisTick) {
        everReceivedFrame_ = true;
        lastFrameMs_ = nowMs;
    }

    // Until the first frame has ever arrived, the link is unconditionally
    // invalid -- timestamps alone must never make the link look healthy.
    const bool linkValid = everReceivedFrame_ && !rxFailsafeFlag &&
                           (nowMs - lastFrameMs_ < config_.linkTimeoutMs);

    if (!linkValid) {
        state_ = State::Safe;
        rearmWindowOpen_ = false;
        rearmFrameTicks_ = 0;
        return state_;
    }

    if (state_ == State::Active) {
        return state_;
    }

    // state_ == Safe, link currently valid: progress the link proof.
    //
    // Over-gap silence inside an open proof discards it (count included):
    // a frame-starved "link" -- e.g. one CRC-colliding garbage frame, or a
    // trickle far below the 50-250 Hz cadence this repo assumes -- must
    // never coast to Active on timestamps. hadFrameBefore is guaranteed
    // here (linkValid implies everReceivedFrame_), kept for explicitness.
    if (rearmWindowOpen_ && hadFrameBefore &&
        (nowMs - previousFrameMs > config_.rearmMaxFrameGapMs)) {
        rearmWindowOpen_ = false;
        rearmFrameTicks_ = 0;
    }

    if (!rearmWindowOpen_) {
        // The proof anchors on a real arrival: a tick without a frame can
        // keep the link "valid" (timestamp still fresh) but cannot open it.
        if (frameArrivedThisTick) {
            rearmWindowOpen_ = true;
            rearmWindowStartMs_ = nowMs;
            rearmFrameTicks_ = 1;
        }
        return state_; // still Safe at the instant the window opens
    }

    if (frameArrivedThisTick && rearmFrameTicks_ < UINT16_MAX) {
        ++rearmFrameTicks_;
    }

    if (nowMs - rearmWindowStartMs_ >= config_.rearmConfirmMs &&
        rearmFrameTicks_ >= config_.rearmMinFrameTicks) {
        state_ = State::Active;
        everLinkedThisBoot_ = true; // first PROVEN link this boot (D4 latch)
        rearmWindowOpen_ = false;
        rearmFrameTicks_ = 0;
    }
    return state_;
}

} // namespace failsafe
