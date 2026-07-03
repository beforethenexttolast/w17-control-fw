#pragma once

#include <cstddef>

#include "console/Console.hpp"
#include "hal/ICharIO.hpp"
#include "hal/ISettingsStore.hpp"
#include "settings/Settings.hpp"

namespace console {

// Glues the pure Console to the char-IO + store seams and owns the RAM
// Settings + line buffer. Still pure (no Arduino): the caller supplies the
// seams (mocks in tests, esp32 impls in main). main.cpp keeps only the
// module wiring -- it applies `settings()` to the live modules whenever
// poll() reports a change.
class ConsoleRunner {
public:
    ConsoleRunner(hal::ICharIO& io, hal::ISettingsStore& store)
        : io_(io), store_(store), settings_(settings::kDefaults) {}

    // At boot: load persisted settings (guard chain inside deserialize); on
    // any failure keep compile-time defaults and say so on the console.
    // Returns after populating settings() -- caller then applies to modules.
    void loadAtBoot();

    // Poll once per loop pass (outside the control tick). Reads available
    // input non-blocking, accumulates one line, and on newline runs the
    // command. Returns true if the RAM Settings changed (caller re-applies).
    bool poll(bool armed);

    const settings::Settings& settings() const { return settings_; }

private:
    void runLine(bool armed, bool& changedOut);

    hal::ICharIO& io_;
    hal::ISettingsStore& store_;
    Console console_;
    settings::Settings settings_;
    char line_[kMaxLine + 2] = {0};
    size_t len_ = 0;
    bool overflow_ = false; // current line exceeded kMaxLine -> discard to newline
};

} // namespace console
