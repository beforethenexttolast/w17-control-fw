#pragma once

#include <cstdint>

namespace failsafe {

enum class State : uint8_t { Active, Safe };

struct Config {
    // No valid CRSF frame for this long forces Safe. Default 500 ms per
    // CLAUDE.md section 2.4 ("start at 500 ms").
    uint32_t linkTimeoutMs = 500;

    // Once link conditions are good again, the link must stay continuously
    // valid for this long before Safe -> Active, to avoid chattering on a
    // marginal link. A deliberate, self-contained re-arm condition for this
    // module: see FailsafeStateMachine::update() doc below.
    uint32_t rearmConfirmMs = 150;
};

// Safety-critical pure state machine. Computes safe/active purely from
// caller-supplied time and frame-arrival events -- it never reads a real
// clock itself, so it is fully testable with synthetic time. No hardware
// dependency.
class FailsafeStateMachine {
public:
    explicit FailsafeStateMachine(Config config = Config{});

    // Call every loop tick (or whenever new info is available).
    //   nowMs                - current time (e.g. millis()), supplied by the caller.
    //   frameArrivedThisTick - true if at least one CRC-valid RC frame was
    //                          decoded since the previous update() call. The
    //                          machine records the arrival time internally;
    //                          until the first arrival it has ever seen, the
    //                          link is unconditionally invalid, so the machine
    //                          can never report Active before a frame has
    //                          actually been received (it must not infer link
    //                          health from timestamps alone -- doing so was a
    //                          boot-time full-lock bug, see docs/ROADMAP.md A1).
    //   rxFailsafeFlag       - true if the RX itself signals failsafe. Fed by
    //                          CrsfReceiver::rxSignalsFailsafe() (latched
    //                          uplink-LQ==0 from LINK_STATISTICS frames).
    //
    // Dropping into Safe is immediate (timeout exceeded OR flag set) -- no
    // debounce on the way in, since that is the safety-critical direction.
    // Returning to Active requires the link to be continuously valid for
    // Config::rearmConfirmMs; any single bad tick during that window resets
    // the confirmation (it is a continuous-duration check, not cumulative).
    //
    // NOTE(deliverable-2): this is intentionally scoped to "is the link good
    // again" only. The separate arm-SWITCH gate from CLAUDE.md section 6.2
    // ("throttle stays neutral until arm switch ON and throttle observed at
    // neutral once") is a complementary safety layer that belongs in the
    // channels module; main.cpp carries a minimal neutral-latch until then.
    State update(uint32_t nowMs, bool frameArrivedThisTick, bool rxFailsafeFlag);

    State state() const { return state_; }

private:
    Config config_;
    State state_ = State::Safe;      // boot-safe default
    bool everReceivedFrame_ = false; // latches true on the first valid frame, never resets
    uint32_t lastFrameMs_ = 0;       // meaningful only once everReceivedFrame_ is true
    bool rearmWindowOpen_ = false;
    uint32_t rearmWindowStartMs_ = 0;
};

} // namespace failsafe
