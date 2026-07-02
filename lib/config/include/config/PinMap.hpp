#pragma once

#include <cstdint>

// All GPIO assignments for ESP32 #1 "control", transcribed from CLAUDE.md
// section 1 (pin map). Keep every pin here so the map is trivial to change
// in one place. Pins marked "deferred" are declared now but not yet wired
// into src/main.cpp.

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
// Gimbal pan (MG90S) -- optional/deferred.
inline constexpr uint8_t kGimbalPanPin = 19;
// Gimbal tilt (MG90S) -- optional/deferred.
inline constexpr uint8_t kGimbalTiltPin = 23;

// Battery sense (27k/10k divider). ADC1_CH6, input-only, 11dB attenuation.
inline constexpr uint8_t kBatterySenseAdcPin = 34;
// Wheel-speed (A3144 Hall). Input-only, external 10k pull-up to 3.3V, rising-edge ISR.
inline constexpr uint8_t kWheelSpeedHallPin = 35;

} // namespace pinmap
