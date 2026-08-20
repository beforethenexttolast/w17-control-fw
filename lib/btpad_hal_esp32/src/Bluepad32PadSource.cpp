#include "btpad_hal_esp32/Bluepad32PadSource.hpp"

// Belt-and-suspenders alongside the platformio.ini lib_ignore chain: the LDF
// (chain mode) does not evaluate #ifdef in main.cpp, so if this lib ever gets
// pulled into an env without the flag, this TU compiles EMPTY instead of
// failing on <Bluepad32.h> (which exists only in the BTstack custom core).
// Nothing references the class there, so nothing links -- same net result as
// the console libs' compiled-but-never-linked posture, ELF-verified.
#ifdef W17_BT_SHOWOFF

#include <Arduino.h>
#include <Bluepad32.h> // BTstack custom core only (env:esp32dev_btshowoff)

namespace btpad_hal_esp32 {

namespace {

// Single-instance trampoline for BP32's std::function callbacks (one pad
// source exists, mirroring the other one-per-peripheral HAL objects).
Bluepad32PadSource* g_instance = nullptr;

int16_t saturateToI16(int32_t v) {
    if (v > INT16_MAX) return INT16_MAX;
    if (v < INT16_MIN) return INT16_MIN;
    return static_cast<int16_t>(v);
}

} // namespace

void Bluepad32PadSource::begin(uint16_t pairWindowMs) {
    pairWindowMs_ = pairWindowMs;
    pairWindowStartMs_ = millis();
    g_instance = this;
    began_ = true;

    BP32.setup(
        [](Controller* ctl) {
            if (g_instance != nullptr) g_instance->handleConnected(ctl);
        },
        [](Controller* ctl) {
            if (g_instance != nullptr) g_instance->handleDisconnected(ctl);
        });

    // A DS4 touchpad otherwise enumerates a SECOND device (a virtual mouse);
    // the single-controller policy wants exactly one device per pad.
    BP32.enableVirtualDevice(false);

    // Pairing window [OWNER-PENDING(BT-10)]: discovery open from boot, locked
    // by servicePairingWindow() once the window elapses (pairWindowMs == 0
    // locks at the first poll). Bonds persist across reboots; a bonded pad's
    // reconnect is not a new pairing and stays allowed after the lock.
    // Deliberately NO forgetBluetoothKeys() anywhere in the boot path -- key
    // forgetting is a bench/console action only (design §6.1).
    BP32.enableNewBluetoothConnections(true);
}

void Bluepad32PadSource::servicePairingWindow(uint32_t nowMs) {
    if (newConnectionsLocked_) {
        return;
    }
    if (nowMs - pairWindowStartMs_ >= pairWindowMs_) {
        // Effectiveness of this lockout is a BT1 bench item (bluepad32 #130
        // reports it non-functional in at least one version).
        BP32.enableNewBluetoothConnections(false);
        newConnectionsLocked_ = true;
    }
}

void Bluepad32PadSource::handleConnected(Controller* ctl) {
    // Single-pad policy (design §6.1): exactly one controller; refuse a
    // second connection and anything that is not a gamepad.
    if (active_ != nullptr || ctl == nullptr || !ctl->isGamepad()) {
        if (ctl != nullptr) {
            ctl->disconnect();
        }
        return;
    }
    active_ = ctl;
    haveBaselineSnapshot_ = false; // next report is "the first" for freshness
}

void Bluepad32PadSource::handleDisconnected(Controller* ctl) {
    if (ctl != active_) {
        return; // a refused second pad going away is not our episode
    }
    active_ = nullptr;
    // Latch the episode so connected() shows >= one full poll cycle of false
    // even if a reconnect lands inside the same BP32.update() -- the seam
    // contract the PadLinkMonitor disconnect latch depends on.
    disconnectEventPending_ = true;
}

bool Bluepad32PadSource::snapshotsEqual(const Snapshot& a, const Snapshot& b) {
    return a.axisX == b.axisX && a.axisY == b.axisY && a.axisRX == b.axisRX &&
           a.axisRY == b.axisRY && a.brake == b.brake && a.throttle == b.throttle &&
           a.buttons == b.buttons && a.miscButtons == b.miscButtons && a.dpad == b.dpad &&
           a.gyro[0] == b.gyro[0] && a.gyro[1] == b.gyro[1] && a.gyro[2] == b.gyro[2] &&
           a.accel[0] == b.accel[0] && a.accel[1] == b.accel[1] && a.accel[2] == b.accel[2] &&
           a.battery == b.battery;
}

bool Bluepad32PadSource::poll(btpad::PadFrame& frame) {
    if (!began_) {
        connectedForCycle_ = false;
        return false;
    }

    // Drains the stack's shared state into the Controller objects; connect /
    // disconnect callbacks fire synchronously inside this call, on this task.
    BP32.update();
    servicePairingWindow(millis());

    if (disconnectEventPending_) {
        disconnectEventPending_ = false;
        connectedForCycle_ = false;
        haveBaselineSnapshot_ = false;
        return false;
    }

    connectedForCycle_ = (active_ != nullptr && active_->isConnected());
    if (!connectedForCycle_) {
        return false;
    }

    // Freshness = the full snapshot changed since the last poll (see the
    // header note: 3.10.x exposes no report counter, DS4 IMU noise makes
    // every real report differ, and a frozen snapshot fails SAFE via the
    // 500 ms staleness failsafe).
    Snapshot now;
    now.axisX = active_->axisX();
    now.axisY = active_->axisY();
    now.axisRX = active_->axisRX();
    now.axisRY = active_->axisRY();
    now.brake = active_->brake();
    now.throttle = active_->throttle();
    now.buttons = active_->buttons();
    now.miscButtons = active_->miscButtons();
    now.dpad = active_->dpad();
    now.gyro[0] = active_->gyroX();
    now.gyro[1] = active_->gyroY();
    now.gyro[2] = active_->gyroZ();
    now.accel[0] = active_->accelX();
    now.accel[1] = active_->accelY();
    now.accel[2] = active_->accelZ();
    now.battery = active_->battery();

    if (haveBaselineSnapshot_ && snapshotsEqual(now, lastSnapshot_)) {
        return false; // nothing new delivered by the stack
    }
    lastSnapshot_ = now;
    haveBaselineSnapshot_ = true;

    // Explicit per-control mapping onto the btpad contract bits -- never a
    // raw bitmask copy (PadFrame.hpp rule). DS4 via Bluepad32: Square sits on
    // the x() slot; OPTIONS on miscStart(); SHARE on miscSelect(); PS on
    // miscSystem(). Verify once at the BT1 bench with the real pad.
    frame.leftStickX = saturateToI16(now.axisX);
    frame.leftStickY = saturateToI16(now.axisY);
    frame.rightStickX = saturateToI16(now.axisRX);
    frame.rightStickY = saturateToI16(now.axisRY);
    frame.brake = saturateToI16(now.brake);
    frame.throttle = saturateToI16(now.throttle);
    frame.buttons = static_cast<uint16_t>((active_->x() ? btpad::kButtonSquare : 0) |
                                          (active_->l1() ? btpad::kButtonL1 : 0) |
                                          (active_->r1() ? btpad::kButtonR1 : 0));
    frame.miscButtons =
        static_cast<uint16_t>((active_->miscSystem() ? btpad::kMiscPs : 0) |
                              (active_->miscSelect() ? btpad::kMiscShare : 0) |
                              (active_->miscStart() ? btpad::kMiscOptions : 0));
    return true;
}

void Bluepad32PadSource::setLightbar(uint8_t red, uint8_t green, uint8_t blue) {
    if (active_ != nullptr && active_->isConnected()) {
        active_->setColorLED(red, green, blue);
    }
}

} // namespace btpad_hal_esp32

#endif // W17_BT_SHOWOFF
