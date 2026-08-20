#pragma once

// Sim-only scripted BT-pad source (validation Stage 2 for the BT head;
// design §7 sim stage, landed per review F2). Implements btpad::IPadSource
// with a canned looping session so the REAL loopBtPad wiring in src/main.cpp
// runs end to end -- decode, ritual, failsafe, arm gate, shaping, link2 --
// with NO Bluetooth and NO Bluepad32: [env:esp32dev_simbt] builds this on
// the PINNED STOCK core with btpad_hal_esp32 still lib_ignore'd, which is
// itself the build-proof that the BT head has no BT-stack dependency.
// Compiled ONLY under W17_SIM_PAD_FEEDER; the module vanishes from every
// other build (same posture as src/SimCrsfFeeder.*).
//
// Session beats (13 s loop; narrated on Serial0 at each phase change):
//   A [    0-1500)  awaiting-pad: disconnected, silent -> boot failsafe Safe
//   B [ 1500-2500)  connect + neutral stream -> Active ~1670, still disarmed
//   C [ 2500-3700)  ritual: L1+R1 held 1 s -> latch ~3500, ArmGate arms
//                   (triggers neutral); grips released at 3600
//   D [ 3700-6700)  drive shapes: R2 triangle, steering sweep, Square taps
//                   toggle DRS on (~4200) and off (~5800)
//   E [ 6700-7700)  dropout: disconnected, silent -> Safe within ~540 ms,
//                   ritual cleared (strict BT-3)
//   F [ 7700-8700)  reconnect CLAIM only (connected, zero reports) -> the
//                   disconnect latch holds, failsafe stays Safe (wiring-level
//                   twin of the BT1 reconnect-without-input probe, review F1)
//   G [ 8700-9900)  neutral stream, no ritual -> Active again but STAYS
//                   DISARMED (re-arm demands the full deliberate ritual)
//   H [ 9900-11600) fresh ritual -> armed ~10900; gentle R2 from 11000
//   I [11600-13000) OPTIONS tap -> instant disarm; neutral idle to loop end

#ifdef W17_SIM_PAD_FEEDER

#include <cstdint>

#include "btpad/IPadSource.hpp"
#include "btpad/PadFrame.hpp"

namespace simpad {

class SimPadSource : public btpad::IPadSource {
public:
    // Same call-shape as the real wrapper so main.cpp's BT-mode setup line is
    // identical. The pairing window is Bluepad32 policy; nothing to do here.
    void begin(uint16_t pairWindowMs);

    // Call at the top of loopBtPad() (the simfeeder::tick pattern): advances
    // the script clock, updates the connected flag, and queues at most one
    // report per report period while a streaming phase is active.
    void tick(uint32_t nowMs);

    // btpad::IPadSource -- honors the seam contract: true once per QUEUED
    // report (a scripted report is a genuine report by construction; the
    // connected flag alone never yields one -- phase F depends on that).
    bool poll(btpad::PadFrame& frame) override;
    bool connected() const override { return connected_; }

    // Output-garnish no-op so loopBtPad's lightbar block compiles unchanged.
    void setLightbar(uint8_t red, uint8_t green, uint8_t blue);

private:
    btpad::PadFrame frame_{};
    bool pendingReport_ = false;
    bool connected_ = false;
    uint32_t lastReportMs_ = 0;
    int lastAnnouncedPhase_ = -1;
};

} // namespace simpad

#endif // W17_SIM_PAD_FEEDER
