#include "console/Console.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace console {

namespace {

// Case-sensitive token compare against a whole token [start, end).
bool tokEq(const char* start, const char* end, const char* lit) {
    const size_t n = static_cast<size_t>(end - start);
    return std::strlen(lit) == n && std::strncmp(start, lit, n) == 0;
}

// Splits `line` into up to `maxTok` tokens on spaces; returns the count.
int tokenize(const char* line, const char* starts[], const char* ends[], int maxTok) {
    int n = 0;
    const char* p = line;
    while (*p && n < maxTok) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        starts[n] = p;
        while (*p && *p != ' ' && *p != '\t') ++p;
        ends[n] = p;
        ++n;
    }
    return n;
}

void say(Result& r, const char* msg) {
    std::snprintf(r.text, kMaxOutput, "%s", msg);
}

const char* const kRejectedMsg = "rejected: value out of range / would violate config invariants";

// uint16 range check before narrowing: without it, "set steer.min 66036"
// would wrap into a plausible in-range value and be silently accepted.
bool fitsU16(long v) {
    return v >= 0 && v <= 0xFFFF;
}

// Parses a signed integer token; returns false if it isn't a clean number.
bool parseInt(const char* start, const char* end, long& out) {
    char buf[24];
    const size_t n = static_cast<size_t>(end - start);
    if (n == 0 || n >= sizeof(buf)) return false;
    std::memcpy(buf, start, n);
    buf[n] = '\0';
    char* endptr = nullptr;
    out = std::strtol(buf, &endptr, 10);
    return endptr == buf + n;
}

// Parses "gear.<N>" prefix of a key token into a 1-based gear number; returns
// 0 if the key isn't a gear key. `suffix` is set to point past "gear.N.".
int parseGearIndex(const char* start, const char* end, const char** suffix) {
    const size_t n = static_cast<size_t>(end - start);
    if (n < 8 || std::strncmp(start, "gear.", 5) != 0) return 0; // "gear.N.x"
    const char* p = start + 5;
    int gear = 0;
    if (p >= end || *p < '1' || *p > '9') return 0;
    while (p < end && *p >= '0' && *p <= '9') {
        gear = gear * 10 + (*p - '0');
        ++p;
    }
    if (p >= end || *p != '.') return 0;
    *suffix = p + 1;
    return gear;
}

} // namespace

Result Console::handleLine(const char* line, settings::Settings& s, bool armed) const {
    Result r;
    const char* starts[4];
    const char* ends[4];
    const int nt = tokenize(line, starts, ends, 4);

    if (nt == 0) {
        r.text[0] = '\0';
        return r;
    }

    const char* c0 = starts[0];
    const char* c0e = ends[0];

    if (tokEq(c0, c0e, "help")) {
        say(r,
            "commands: help | status | get <key> | set <key> <val> | save | load | reset\r\n"
            "keys: steer.min steer.max steer.center steer.trim batt.ppt gear.<N>.max gear.<N>.expo\r\n"
            "note: set/save/load/reset only while DISARMED; channels are read-only");
        return r;
    }

    if (tokEq(c0, c0e, "status")) {
        std::snprintf(r.text, kMaxOutput,
                      "armed=%d\r\nsteer.center=%u steer.trim=%d [%u..%u]\r\n"
                      "batt.ppt=%u\r\ngears=%u  g1.max=%d g1.expo=%u ...\r\n"
                      "channels(read-only): steer=%u thr=%u arm=%u drs=%u",
                      armed ? 1 : 0, s.steering.centerMicros, s.steering.trimMicros,
                      s.steering.minMicros, s.steering.maxMicros, s.battery.calibrationPpt,
                      s.gearbox.numGears, s.gearbox.gears[0].maxOutput, s.gearbox.gears[0].expoPercent,
                      0u, 2u, 4u, 5u);
        return r;
    }

    const bool isSet = tokEq(c0, c0e, "set");
    const bool isGet = tokEq(c0, c0e, "get");

    if (isGet) {
        if (nt < 2) {
            say(r, "usage: get <key>");
            return r;
        }
    }

    // --- Mutating commands gated on DISARMED. ---
    if ((isSet || tokEq(c0, c0e, "save") || tokEq(c0, c0e, "load") || tokEq(c0, c0e, "reset")) &&
        armed) {
        say(r, "refused: disarm to change settings (tuning is a pit-lane activity)");
        return r;
    }

    if (tokEq(c0, c0e, "save")) {
        r.saveRequested = true;
        say(r, "saving to flash");
        return r;
    }
    if (tokEq(c0, c0e, "load")) {
        r.loadRequested = true;
        say(r, "reloading from flash");
        return r;
    }
    if (tokEq(c0, c0e, "reset")) {
        s = settings::kDefaults;
        r.settingsChanged = true;
        say(r, "reverted to defaults (RAM only; type 'save' to persist)");
        return r;
    }

    if (!isSet && !isGet) {
        say(r, "unknown command (try 'help')");
        return r;
    }

    // Both get and set need a key.
    const char* k = starts[1];
    const char* ke = ends[1];

    // Resolve the key to a getter (writes into r.text) and, for set, apply a
    // parsed value onto a COPY, validate, then commit. Range enforcement is
    // Settings::valid() -- the single source of truth.
    long v = 0;
    if (isSet) {
        if (nt < 3 || !parseInt(starts[2], ends[2], v)) {
            say(r, "usage: set <key> <integer>");
            return r;
        }
    }

    settings::Settings next = s; // trial copy for set
    bool matched = false;

    if (tokEq(k, ke, "steer.min")) {
        matched = true;
        if (isSet) {
            if (!fitsU16(v)) {
                say(r, kRejectedMsg);
                return r;
            }
            next.steering.minMicros = static_cast<uint16_t>(v);
        } else std::snprintf(r.text, kMaxOutput, "steer.min=%u", s.steering.minMicros);
    } else if (tokEq(k, ke, "steer.max")) {
        matched = true;
        if (isSet) {
            if (!fitsU16(v)) {
                say(r, kRejectedMsg);
                return r;
            }
            next.steering.maxMicros = static_cast<uint16_t>(v);
        } else std::snprintf(r.text, kMaxOutput, "steer.max=%u", s.steering.maxMicros);
    } else if (tokEq(k, ke, "steer.center")) {
        matched = true;
        if (isSet) next.steering.centerMicros = static_cast<uint16_t>(v);
        else std::snprintf(r.text, kMaxOutput, "steer.center=%u", s.steering.centerMicros);
    } else if (tokEq(k, ke, "steer.trim")) {
        matched = true;
        if (isSet) next.steering.trimMicros = static_cast<int16_t>(v);
        else std::snprintf(r.text, kMaxOutput, "steer.trim=%d", s.steering.trimMicros);
    } else if (tokEq(k, ke, "batt.ppt")) {
        matched = true;
        if (isSet) next.battery.calibrationPpt = static_cast<uint16_t>(v);
        else std::snprintf(r.text, kMaxOutput, "batt.ppt=%u", s.battery.calibrationPpt);
    } else {
        const char* suffix = nullptr;
        const int gear = parseGearIndex(k, ke, &suffix);
        if (gear >= 1 && gear <= s.gearbox.numGears) {
            const int idx = gear - 1;
            if (tokEq(suffix, ke, "max")) {
                matched = true;
                if (isSet) next.gearbox.gears[idx].maxOutput = static_cast<int16_t>(v);
                else std::snprintf(r.text, kMaxOutput, "gear.%d.max=%d", gear,
                                   s.gearbox.gears[idx].maxOutput);
            } else if (tokEq(suffix, ke, "expo")) {
                matched = true;
                if (isSet) next.gearbox.gears[idx].expoPercent = static_cast<uint8_t>(v);
                else std::snprintf(r.text, kMaxOutput, "gear.%d.expo=%u", gear,
                                   s.gearbox.gears[idx].expoPercent);
            }
        } else if (gear != 0) {
            say(r, "gear index out of range");
            return r;
        }
    }

    if (!matched) {
        say(r, "unknown key (try 'help')");
        return r;
    }

    if (isSet) {
        if (!next.valid()) {
            say(r, kRejectedMsg);
            return r;
        }
        s = next;
        r.settingsChanged = true;
        say(r, "ok (RAM only; type 'save' to persist)");
    }
    return r;
}

} // namespace console
