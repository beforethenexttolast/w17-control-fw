#include "console/ConsoleRunner.hpp"

namespace console {

void ConsoleRunner::loadAtBoot() {
    uint8_t buf[settings::kBlobLen];
    size_t len = 0;
    settings::Settings loaded;
    if (store_.load(buf, sizeof(buf), len) && settings::deserialize(buf, len, loaded)) {
        settings_ = loaded;
        io_.write("[tune] loaded settings from flash\r\n");
    } else {
        settings_ = settings::kDefaults;
        io_.write("[tune] using defaults (no valid saved settings)\r\n");
    }
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
        uint8_t buf[settings::kBlobLen];
        size_t len = 0;
        settings::Settings loaded;
        if (store_.load(buf, sizeof(buf), len) && settings::deserialize(buf, len, loaded)) {
            settings_ = loaded;
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
