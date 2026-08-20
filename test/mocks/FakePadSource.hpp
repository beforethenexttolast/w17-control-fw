#pragma once

#include "btpad/IPadSource.hpp"

namespace test_mocks {

// Scripted btpad::IPadSource for native tests (and the future sim feeder
// pattern, design §7): the test queues at most one pending report and flips
// the connected flag; poll() honors the seam contract -- true once per
// scripted report, latest state copied out, false when nothing new.
class FakePadSource : public btpad::IPadSource {
public:
    bool poll(btpad::PadFrame& frame) override {
        if (!pendingReport_) {
            return false;
        }
        frame = frame_;
        pendingReport_ = false;
        return true;
    }

    bool connected() const override { return connected_; }

    // Test script surface.
    void scriptReport(const btpad::PadFrame& frame) {
        frame_ = frame;
        pendingReport_ = true;
    }
    void setConnected(bool connected) { connected_ = connected; }

private:
    btpad::PadFrame frame_{};
    bool pendingReport_ = false;
    bool connected_ = false;
};

} // namespace test_mocks
