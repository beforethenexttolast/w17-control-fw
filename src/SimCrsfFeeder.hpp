#pragma once

// Wokwi-simulation-only CRSF feeder (validation Stage 2, CLAUDE.md section 4).
// Writes a scripted stream of canned CRSF frames to Serial2 TX (GPIO17),
// which diagram.json loops back into the CRSF RX pin (GPIO16), so the real
// UART driver + parser run unmodified. Compiled ONLY in [env:esp32dev_sim];
// the whole module vanishes from the real firmware.
//
// Demo scenario + run instructions: docs/SIMULATION.md.

#ifdef W17_SIM_CRSF_FEEDER

#include <cstdint>

namespace simfeeder {

// Call at the top of loop(). Sends RC frames at 50Hz and LINK_STATISTICS at
// 10Hz per the phase script; prints phase transitions on Serial0.
void tick(uint32_t nowMs);

} // namespace simfeeder

#endif // W17_SIM_CRSF_FEEDER
