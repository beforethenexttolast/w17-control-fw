#include "failsafe/FailsafeStateMachine.hpp"

namespace failsafe {

FailsafeStateMachine::FailsafeStateMachine(Config config) : config_(config) {}

State FailsafeStateMachine::update(uint32_t nowMs, bool frameArrivedThisTick, bool rxFailsafeFlag) {
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
        return state_;
    }

    if (state_ == State::Active) {
        return state_;
    }

    // state_ == Safe, link currently valid: progress the re-arm confirmation window.
    if (!rearmWindowOpen_) {
        rearmWindowOpen_ = true;
        rearmWindowStartMs_ = nowMs;
        return state_; // still Safe at the instant the window opens
    }

    if (nowMs - rearmWindowStartMs_ >= config_.rearmConfirmMs) {
        state_ = State::Active;
        rearmWindowOpen_ = false;
    }
    return state_;
}

} // namespace failsafe
