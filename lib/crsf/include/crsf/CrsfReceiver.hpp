#pragma once

#include "crsf/CrsfFrame.hpp"
#include "crsf/CrsfFrameAssembler.hpp"

namespace crsf {

// Receiver facade per CLAUDE.md section 2.1: channels, link-up, last-frame
// time, and the failsafe indication the RX signals. Owns the frame assembler
// and dispatches CRC-valid frames by type. Pure C++, no hardware dependency;
// timestamps are caller-supplied (ms -- deliberate deviation from the spec's
// "lastFrameMicros": the whole codebase is ms-based and micros wraps in ~71 min).
class CrsfReceiver {
public:
    enum class ByteResult : uint8_t {
        None,         // nothing new (mid-frame, unknown type, or ignored frame)
        NewRcFrame,   // channels() was just updated
        NewLinkStats, // linkStats() was just updated
        CorruptFrame  // framing/CRC failure (assembler resyncs automatically)
    };

    // Feed one raw UART byte. nowMs stamps any completed RC frame.
    ByteResult feedByte(uint8_t b, uint32_t nowMs);

    // Owned copies -- stable between feedByte() calls (unlike the assembler's
    // internal buffer), so callers may hold references across a loop tick.
    const RcChannelsFrame& channels() const { return channels_; }
    const CrsfLinkStatistics& linkStats() const { return linkStats_; }

    bool hasEverReceivedRcFrame() const { return everRcFrame_; }
    uint32_t lastRcFrameMs() const { return lastRcFrameMs_; }

    // "The failsafe flag the RX signals": CRSF carries no explicit failsafe
    // bit. ELRS convention: while connected the RX sends LINK_STATISTICS
    // (~10 Hz); when it declares link loss it sends a short forced burst of
    // stats with uplink LQ = 0 (then usually goes silent). This flag LATCHES
    // on LQ == 0 and clears ONLY on a stats frame with LQ > 0 -- deliberately
    // never on RC frames and never on staleness expiry. Rationale: if the RX
    // is (mis)configured to keep emitting hold-position RC frames during an
    // outage, those frames must not be able to clear the latch; genuine
    // recovery always brings fresh LQ > 0 stats within ~100 ms anyway. (This
    // intentionally diverges from Betaflight, whose ~250 ms stats staleness
    // window only zeroes OSD display values, not failsafe.)
    bool rxSignalsFailsafe() const {
        return everLinkStats_ && linkStats_.uplinkLinkQuality == 0;
    }

    // Convenience link-health summary for telemetry/link2 reporting
    // (CLAUDE.md 2.1 "linkUp"). NOT the actuation authority: the
    // FailsafeStateMachine remains the sole decider of safe-vs-active, with
    // its own latch and re-arm confirmation semantics.
    bool linkUp(uint32_t nowMs, uint32_t timeoutMs = 500) const {
        return everRcFrame_ && (nowMs - lastRcFrameMs_) < timeoutMs && !rxSignalsFailsafe();
    }

private:
    CrsfFrameAssembler assembler_;
    RcChannelsFrame channels_{};
    CrsfLinkStatistics linkStats_{};
    bool everRcFrame_ = false;
    bool everLinkStats_ = false;
    uint32_t lastRcFrameMs_ = 0;
};

} // namespace crsf
