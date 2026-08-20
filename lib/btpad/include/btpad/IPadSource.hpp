#pragma once

#include "btpad/PadFrame.hpp"

namespace btpad {

// The hardware seam for BT show-off mode (design §4.2): the same role the
// hal::ICharIO / UART seams play elsewhere. The real implementation wraps
// Bluepad32 in lib/btpad_hal_esp32 and is referenced ONLY from src/main.cpp
// under W17_BT_SHOWOFF; tests script a FakePadSource.
//
// Contract the implementation must honor (PadLinkMonitor and the failsafe
// chain depend on it):
//  - poll() drains the stack and returns true iff at least one NEW input
//    report arrived since the previous poll() call, writing the LATEST state
//    into `frame`. "New report" means the stack delivered one -- NOT "the
//    values changed": a DS4 streams reports continuously (design §3.1), and
//    constant-input reports must still count, or staleness would nuisance-trip.
//    A bare (re)connect claim must NEVER manufacture a report by itself
//    (review F1): the first poll()==true after a connect requires a genuine
//    post-connect report, because the failsafe chain treats poll()==true as
//    "the pad is really talking" -- it clears the disconnect latch and feeds
//    the frame-timeout clock.
//  - connected() is true while exactly one controller is connected (the
//    single-pad policy, design §6.1). A disconnect observed by the stack's
//    callback must be visible as at least one poll cycle with
//    connected() == false, even if a reconnect follows immediately, so the
//    PadLinkMonitor disconnect latch can never miss the episode.
class IPadSource {
public:
    virtual ~IPadSource() = default;

    virtual bool poll(PadFrame& frame) = 0;
    virtual bool connected() const = 0;
};

} // namespace btpad
