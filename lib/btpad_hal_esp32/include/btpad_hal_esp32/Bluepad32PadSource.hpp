#pragma once

#include <cstdint>

#include "btpad/IPadSource.hpp"

// Bluepad32 3.10.x controller handle (global-namespace class). Forward-declared
// so this header stays light; the full <Bluepad32.h> is pulled only by the
// .cpp, which compiles solely in env:esp32dev_btshowoff (the BTstack custom
// core -- on any other core the include fails LOUDLY, by design).
class Controller;

namespace btpad_hal_esp32 {

// The real btpad::IPadSource over Bluepad32 (docs/bt_showoff_design.md §4.2).
// Referenced ONLY from src/main.cpp under W17_BT_SHOWOFF, exactly the pattern
// of the other *_hal_esp32 libs. Also owns the pairing policy
// [OWNER-DECIDED(BT-10)] and the lightbar output garnish [OWNER-DECIDED(BT-6)].
//
// Freshness contract note (the honest wrinkle, verified against the pinned
// 3.10.2 sources -- ArduinoBluepad32.h and uni_platform_arduino.c): the 3.10.x
// Arduino API exposes NO per-report event or sequence counter; BP32.update()
// returns void and the platform layer just overwrites shared state. "New
// report since last poll" is therefore DERIVED: poll() diffs a full state
// snapshot INCLUDING the IMU fields. A genuine DS4 streams gyro/accel with
// every report, so consecutive snapshots differ in practice even with hands
// off (design §3.1's bench-verify assumption, sharpened). The failure
// direction is SAFE: a pad whose snapshots freeze (e.g. an IMU-less clone at
// constant input) reads as "no new reports" and trips the 500 ms staleness
// failsafe -- the car stops. The BT1 bench gate measures the real idle
// snapshot-diff rate; if it ever shows gaps, the fix is a report-seqno patch
// in the platform layer (custom-core rebuild), never a relaxation here.
//
// Review F1 (2026-08-17): handleConnected seeds the baseline from the
// CONNECT-TIME controller state, so a bare stack (re)connect claim yields
// zero frames -- "new" requires an actual post-connect state change. Without
// the seed, the first poll after any reconnect counted as a phantom report,
// clearing the PadLinkMonitor latch and allowing up to ~650 ms of
// Active-with-neutral-controls before staleness re-tripped (fail-safe, but a
// seam-contract violation). NATIVE-COVERAGE LIMITATION, stated honestly:
// this fix lives inside the Bluepad32-specific freshness derivation, which
// compiles only against the BTstack custom core; test/mocks/FakePadSource is
// a PARALLEL implementation of the seam, so no native test can execute this
// code path. The pure-side latch semantics ("a mere connected-again claim
// must not clear it") are already pinned natively (test_link_monitor_* and
// the harness recovery test in test_btpad); THIS wrapper's end of the
// contract is verified by the BT1 reconnect-without-input bench probe
// (design §5).
class Bluepad32PadSource : public btpad::IPadSource {
public:
    // Call once, in BT-mode setup() only (CRSF mode must never call this --
    // that is what keeps the BT stack ABSENT at runtime, design §2.1).
    // pairWindowMs: new-pairing window after boot [OWNER-DECIDED(BT-10)];
    // once elapsed, enableNewBluetoothConnections(false) -- already-bonded
    // pads still reconnect (that is a reconnect, not a new pairing). The
    // lockout's real-world effectiveness is a BT1 bench item (bluepad32 #130).
    void begin(uint16_t pairWindowMs);

    // IPadSource. poll() drives BP32.update() (callbacks fire inside it, on
    // this same task -- no cross-task state here), services the pairing
    // window, and applies the snapshot-diff freshness rule above.
    bool poll(btpad::PadFrame& frame) override;
    // Reflects the state as of the last poll(); a disconnect episode is
    // guaranteed visible as >= one full poll cycle of false, even if a
    // reconnect landed in the same BP32.update() (the seam contract
    // PadLinkMonitor depends on).
    bool connected() const override { return connectedForCycle_; }

    // Output-only garnish [OWNER-DECIDED(BT-6): lightbar only in the
    // prototype]. No safety surface; silently ignored when no pad is up.
    void setLightbar(uint8_t red, uint8_t green, uint8_t blue);

private:
    // Full controller state for the freshness diff. Field-wise compare (a raw
    // memcmp would be hostage to padding); includes IMU so a streaming DS4
    // always differs report-to-report.
    struct Snapshot {
        int32_t axisX = 0;
        int32_t axisY = 0;
        int32_t axisRX = 0;
        int32_t axisRY = 0;
        int32_t brake = 0;
        int32_t throttle = 0;
        uint16_t buttons = 0;
        uint16_t miscButtons = 0;
        uint8_t dpad = 0;
        int32_t gyro[3] = {0, 0, 0};
        int32_t accel[3] = {0, 0, 0};
        uint8_t battery = 0;
    };
    static bool snapshotsEqual(const Snapshot& a, const Snapshot& b);
    // Reads the controller's full current state (all accessors are const).
    static Snapshot captureSnapshot(const Controller& ctl);

    void handleConnected(Controller* ctl);
    void handleDisconnected(Controller* ctl);
    void servicePairingWindow(uint32_t nowMs);

    Controller* active_ = nullptr; // single-pad policy: exactly one, or none
    bool began_ = false;
    bool disconnectEventPending_ = false;
    bool connectedForCycle_ = false;
    bool haveBaselineSnapshot_ = false;
    Snapshot lastSnapshot_{};
    uint16_t pairWindowMs_ = 0;
    uint32_t pairWindowStartMs_ = 0;
    bool newConnectionsLocked_ = false;
};

} // namespace btpad_hal_esp32
