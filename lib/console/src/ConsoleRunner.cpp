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

// ONE line per pass, deliberately (finding timing-2). This drain used to run
// EVERY buffered line before returning, each with a blocking UART0 write, on the
// same loopTask that owns the 50 Hz control tick -- so a pasted block of console
// commands (or a stuck-key flood) executed as one unbounded burst. src/main.cpp
// documents the intended behaviour at its call site as "polled outside the
// control tick, non-blocking, one capped line per pass"; this makes the code
// match. A human interface does not need more than one line per ~20 ms tick, and
// what is left in the RX ring is still there next pass.
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
            return changed; // the cap: the rest of the ring waits for next pass
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
        // The NVS commit happens INLINE, on the loopTask (finding timing-3,
        // carried UNVERIFIED). A flash write disables the flash cache, and the
        // CRSF UART ISR under this pinned core was NOT confirmed to be
        // IRAM-resident, so RX may be deaf for the duration -- the 128-byte
        // FIFO holds ~3 ms at 420 kbaud. Left inline deliberately: `save` is
        // bench-only (this translation unit is not in the delivery image), the
        // console refuses mutations unless DISARMED, and a gap is at worst a
        // failsafe blip, which is the safe direction and far inside the 500 ms
        // budget. The bench operator is told to expect (and record) that gap:
        // docs/D8_BENCH_BRINGUP.md, Phase 3, the "Know what `save` does to the
        // link" bullet. That observation is what would settle this finding.
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
