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

    // --- Link-proof requirement (2026-08-20 drive-mode hardening) ----------
    // Before this, ONE CRC-valid frame timestamped the link "fresh" for the
    // whole linkTimeoutMs, so the confirm window above could elapse on that
    // single timestamp alone: one garbage frame that happened to pass CRC8
    // (1-in-256 per candidate) followed by silence produced ~350 ms of
    // Active with zero real link behind it. Leaving Safe now additionally
    // requires a PROOF built from actual arrivals:
    //
    //   rearmMinFrameTicks  - update() calls that saw >= 1 valid frame,
    //                         required inside one confirm window before
    //                         Safe -> Active. At the 50 Hz control tick
    //                         (src/main.cpp kControlPeriodMs = 20) and the
    //                         slowest link cadence this repo assumes
    //                         (50-250 Hz CRSF, docs/ROADMAP.md A9; ~62 Hz
    //                         pad reports, src/SimPadFeeder.cpp), a healthy
    //                         link makes essentially EVERY tick a frame
    //                         tick, so the 150 ms window offers 8-9
    //                         opportunities; 5 leaves margin for beat/jitter
    //                         misses while a lone frame (count 1) can never
    //                         satisfy it.
    //   rearmMaxFrameGapMs  - longest silence between valid frames tolerated
    //                         inside the proof; a larger gap discards the
    //                         window and the count, and the proof restarts
    //                         from the next real frame. 60 ms = 3 tick
    //                         periods (a healthy >= 50 Hz source shows
    //                         observed gaps of 20-40 ms) and is 8x tighter
    //                         than linkTimeoutMs, so garbage-then-silence
    //                         breaks the proof within <= 80 ms of the last
    //                         garbage frame.
    //
    // Both gate ENTRY to Active only. Every path into Safe (timeout, RX
    // failsafe flag, first-frame-ever latch) is untouched -- link LOSS
    // detection is exactly as fast as before.
    uint16_t rearmMinFrameTicks = 5;
    uint32_t rearmMaxFrameGapMs = 60;

    // static_assert this at the definition site. Encodes the fail-closed
    // relations: a single frame must never prove a link (>= 2), and the
    // intra-proof gap tolerance must be strictly tighter than both the
    // confirm window it polices and the loss timeout.
    constexpr bool valid() const {
        return linkTimeoutMs > 0 && rearmConfirmMs > 0 && rearmMinFrameTicks >= 2 &&
               rearmMaxFrameGapMs > 0 && rearmMaxFrameGapMs <= rearmConfirmMs &&
               rearmMaxFrameGapMs < linkTimeoutMs;
    }
};

static_assert(Config{}.valid(), "failsafe::Config defaults must satisfy their own bounds");

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
    // Returning to Active requires a LINK PROOF: starting from a tick that
    // actually carried a frame, the link must stay continuously valid for
    // Config::rearmConfirmMs, at least Config::rearmMinFrameTicks of the
    // ticks inside that window must carry a frame, and no two frames inside
    // it may be more than Config::rearmMaxFrameGapMs apart. Any bad tick, or
    // any over-gap silence, discards the whole proof (continuous-duration
    // check, not cumulative). A healthy-cadence link (frames every control
    // tick) re-arms at exactly the same instant as before the proof existed;
    // only frame-starved "links" are slowed -- all the way to never.
    //
    // NOTE: this is intentionally scoped to "is the link good again" only.
    // The separate arm-SWITCH gate from CLAUDE.md section 6.2 ("throttle
    // stays neutral until arm switch ON and throttle observed at neutral
    // once") is a complementary safety layer implemented in channels::ArmGate
    // (lib/channels/include/channels/ArmGate.hpp): main.cpp runs it every
    // control tick and feeds it this machine's Safe state as forceDisarm.
    State update(uint32_t nowMs, bool frameArrivedThisTick, bool rxFailsafeFlag);

    State state() const { return state_; }

    // True once ANY valid RC frame has been seen this boot; latches, never
    // resets (the same internal latch that keeps the machine from inferring
    // link health before a first frame). Read-only observer; diagnostic
    // truth about BYTES, not about a link -- a lone CRC-colliding garbage
    // frame sets it. Policy decisions ride hasEverLinked() below.
    bool hasEverReceivedFrame() const { return everReceivedFrame_; }

    // True once a link has been PROVEN this boot -- latches on the first
    // Safe -> Active transition (full link proof completed), never resets.
    // This is `everLinkedThisBoot` for the SHOWCASE link2 failsafe-flag
    // policy (bootmode::link2FailsafeFlag, owner decision D4: a shelf demo
    // that NEVER had a link must not hazard, a link that existed and died
    // must still be told). Latching at proof completion -- not at first
    // frame -- keeps a lone noise frame from making a shelf demo report a
    // dead link forever, and keeps the flag quiet during the very first
    // confirm window (there is no "link that died" until one provably
    // existed). Drive-mode behavior keys off state() exactly as before and
    // never reads this.
    bool hasEverLinked() const { return everLinkedThisBoot_; }

private:
    Config config_;
    State state_ = State::Safe;      // boot-safe default
    bool everReceivedFrame_ = false; // latches true on the first valid frame, never resets
    bool everLinkedThisBoot_ = false; // latches true on the first proven Safe -> Active
    uint32_t lastFrameMs_ = 0;       // meaningful only once everReceivedFrame_ is true
    bool rearmWindowOpen_ = false;
    uint32_t rearmWindowStartMs_ = 0;
    uint16_t rearmFrameTicks_ = 0; // frame-bearing ticks inside the open window
};

} // namespace failsafe
