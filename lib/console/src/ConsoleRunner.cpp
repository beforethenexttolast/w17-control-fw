#include "console/ConsoleRunner.hpp"

#include "settings/SettingsLoader.hpp"

namespace console {

void ConsoleRunner::loadAtBoot() {
    // Route through the SAME shared loader the delivery firmware uses, so both
    // boots run one identical guard chain (length -> CRC -> version -> valid()).
    const settings::LoadResult r = settings::loadOrDefault(store_);
    settings_ = r.settings;
    io_.write(r.loadedFromStore() ? "[tune] loaded settings from flash\r\n"
                                  : "[tune] using defaults (no valid saved settings)\r\n");
}

bool ConsoleRunner::poll(bool armed) {
    bool changed = false;
    for (int c = io_.read(); c >= 0; c = io_.read()) {
        if (c == '\r') {
            continue; // tolerate CRLF
        }
        if (c == '\n') {
            line_[len_] = '\0';
            if (overflow_) {
                io_.write("line too long, ignored\r\n");
                overflow_ = false;
            } else {
                runLine(armed, changed);
            }
            len_ = 0;
            continue;
        }
        if (len_ < kMaxLine) {
            line_[len_++] = static_cast<char>(c);
        } else {
            overflow_ = true; // keep discarding until newline (flood guard)
        }
    }
    return changed;
}

void ConsoleRunner::runLine(bool armed, bool& changedOut) {
    Result r = console_.handleLine(line_, settings_, armed);

    if (r.saveRequested) {
        uint8_t buf[settings::kBlobLen];
        const size_t n = settings::serialize(settings_, buf);
        io_.write(store_.save(buf, n) ? "saved\r\n" : "SAVE FAILED\r\n");
        return;
    }
    if (r.loadRequested) {
        // Same shared guard chain as boot; on any failure keep the current RAM
        // Settings (do NOT clobber them with defaults on a bad reload).
        const settings::LoadResult lr = settings::loadOrDefault(store_);
        if (lr.loadedFromStore()) {
            settings_ = lr.settings;
            changedOut = true;
            io_.write("loaded\r\n");
        } else {
            io_.write("no valid saved settings\r\n");
        }
        return;
    }

    if (r.text[0] != '\0') {
        io_.write(r.text);
        io_.write("\r\n");
    }
    if (r.settingsChanged) {
        changedOut = true;
    }
}

} // namespace console
