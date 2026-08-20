#pragma once

#include <cstdint>

// All GPIO assignments for ESP32 #1 "control", transcribed from CLAUDE.md
// section 1 (pin map). Keep every pin here so the map is trivial to change
// in one place. Every pin below is wired in src/main.cpp except
// kBoard2UartRxPin (reserved-but-unused: link2 is one-way, so Esp32Link2Uart
// opens Serial1 with rxPin = -1) and the two boot-mode strap pins
// kBtModeStrapPin / kShowModeStrapPin (wired only under W17_BT_SHOWOFF --
// see their note).

namespace pinmap {

// CRSF in, from RadioMaster RP1 TX pad. UART2 RX. 420000 baud, 8N1, NOT inverted.
inline constexpr uint8_t kCrsfUartRxPin = 16;
// CRSF telemetry out, to RP1 RX pad. UART2 TX. Optional uplink (battery/RPM).
inline constexpr uint8_t kCrsfUartTxPin = 17;

// UART to ESP32 #2 (sound + light), TX side. UART1, remapped. 3.3V logic, common ground.
inline constexpr uint8_t kBoard2UartTxPin = 25;
// UART from ESP32 #2, RX side (optional ack/handshake).
inline constexpr uint8_t kBoard2UartRxPin = 26;

// Steering servo (DSServo DS3235SG, 180 deg). LEDC 50Hz, center 1500us.
inline constexpr uint8_t kSteeringServoPin = 13;
// ESC throttle (Hobbywing QuicRun 10BL120). LEDC 50Hz, neutral 1500us, boot arm sequence.
inline constexpr uint8_t kEscThrottlePin = 14;
// DRS servo (MG90S, 2-position). LEDC 50Hz.
inline constexpr uint8_t kDrsServoPin = 18;
// Camera gimbal pan (MG90S), LEDC 50Hz -- right stick X via ch9.
inline constexpr uint8_t kGimbalPanPin = 19;
// Camera gimbal tilt (MG90S), LEDC 50Hz -- right stick Y via ch10.
inline constexpr uint8_t kGimbalTiltPin = 23;

// Boot-mode selector: ONE SP3T slide switch, common to GND, throws on the
// two strap pins below, center = LAPTOP/Drive (both pins open on internal
// pull-ups). OWNER-RATIFIED(D3-SHOW-SELECT) 2026-08-20, extending the
// OWNER-DECIDED(BT-2) GPIO27 strap of docs/bt_showoff_design.md §2.2
// mechanism A. Both pins are read ONCE at boot (settle + majority-of-N per
// bootmode policy constants); any ambiguous reading -- including both pins
// low, which the part cannot produce, so it is a harness fault -- fails to
// Drive. Wired ONLY by the W17_BT_SHOWOFF prototype envs; delivery/tuning/
// sim builds never touch these pins. The physical switch itself is
// bench-gated (A2; wiring-atlas reconciliation + the A2/F20 continuity-
// matrix note remain wiring-time tasks) -- firmware side only for now.
//
// SOLO throw: BT pad mode. OWNER-DECIDED(BT-2) GPIO27, accepted 2026-08-17;
// semantics unchanged by D3. Not a strapping pin (0/2/12/15 avoided per
// CLAUDE.md).
inline constexpr uint8_t kBtModeStrapPin = 27;
// SHOW throw: showcase mode. GPIO32 (D3-SHOW-SELECT, 2026-08-20): free in
// this map, RTC-capable general I/O with internal pull-up, NOT input-only
// (unlike 34/35), not a strapping pin, no XTAL32K crystal fitted on the
// MH-ET D1-Mini / DevKit V1 boards so the pin is an ordinary GPIO here.
inline constexpr uint8_t kShowModeStrapPin = 32;

// Battery sense (27k/10k divider). ADC1_CH6, input-only, 11dB attenuation.
inline constexpr uint8_t kBatterySenseAdcPin = 34;
// Wheel-speed (A3144 Hall). Input-only, external 10k pull-up to 3.3V, rising-edge ISR.
inline constexpr uint8_t kWheelSpeedHallPin = 35;

} // namespace pinmap
