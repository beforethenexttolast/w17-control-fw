#pragma once

namespace btpad {

// Produces the (frameArrived, failsafeFlag) pair the BT head feeds into the
// UNCHANGED failsafe::FailsafeStateMachine -- the exact analog of what the
// CRSF path feeds it (design §3 row 1):
//
//   frameArrived  = "a new report from the bonded pad was consumed this tick"
//                   (IPadSource::poll() result, accumulated by the caller
//                   between control ticks exactly like rcFrameSinceTick).
//   failsafeFlag  = padSignalsFailsafe(): a disconnect LATCH mirroring the
//                   CRSF LQ==0 latch semantics in crsf::CrsfReceiver --
//                   latched by a disconnect, cleared ONLY by reconnect + the
//                   first consumed report (a mere "connected again" claim from
//                   the stack must not clear it; genuine recovery always
//                   delivers a report immediately, design §3.1).
//
// The 500 ms staleness clock stays PRIMARY (BT link-supervision timeouts are
// seconds; the stack's disconnect event is the secondary trigger -- design
// §3.1). Both signals feed the same state machine, same Config, same outcomes.
//
// Pure C++, no hardware dependency. Call update() once per loop pass with the
// facts from the seam; read padSignalsFailsafe() when feeding the tick.
class PadLinkMonitor {
public:
    void update(bool newReportConsumed, bool connectedNow) {
        // Clear first, then latch: a disconnect observed in the same pass as a
        // report wins (the conservative direction).
        if (connectedNow && newReportConsumed) {
            disconnectLatched_ = false;
        }
        if (wasConnected_ && !connectedNow) {
            disconnectLatched_ = true;
        }
        wasConnected_ = connectedNow;
    }

    bool padSignalsFailsafe() const { return disconnectLatched_; }

private:
    bool wasConnected_ = false;
    bool disconnectLatched_ = false;
};

} // namespace btpad
