# link2 protocol v1 — control board (ESP32 #1) → sound/light board (ESP32 #2)

One-way UART. **115200 baud, 8N1, 3.3 V logic, common ground.** Sender TX = ESP32 #1
GPIO25 → receiver RX on ESP32 #2. (GPIO26 is reserved for a future ack channel and is
not driven — do not connect anything to it yet.) Both boards power from the same UBEC
rail, so they come up together; avoid driving the line into an unpowered board #2 for
long periods.

Reference implementation: `lib/link2/` in this repo — **liftable wholesale** into the
board-#2 project (no dependencies beyond a byte-sink interface; the decoder and
`Link2FrameAssembler` are what board #2 needs).

## Frame layout (12 bytes)

```
offset  size  field
0       1     start byte, always 0xA5
1       1     length = payload byte count (9 in v1)
2       9     payload (below)
11      1     crc8 over bytes [1..10]  (length + payload; start byte excluded)
```

CRC8: polynomial 0xD5, initial value 0, MSB-first, no reflection (CRC-8/DVB-S2 — the
same algorithm CRSF uses; catalog check value for ASCII `"123456789"` is `0xBC`).

**Validation order:** start → length → CRC → version. `BadVersion` therefore means a
*well-formed frame from a newer sender*, not corruption. Receivers must hard-reject an
unsupported length byte the moment it arrives (do not buffer `length` unknown bytes —
a corrupted 0xFF length would otherwise swallow ~1 s of following frames).

## Payload v1 — all multi-byte fields little-endian (low byte first)

| offset | size | field | semantics |
|---|---|---|---|
| 0 | 1 | version | `1`. Reject anything else. |
| 1 | 1 | throttlePercent | int8, −100…+100. **What the ESC is actually commanded** (0 while disarmed or failsafe) — engine sound should track this, not stick position. Negative = braking, **never reverse motion** (the ESC runs forward/brake). |
| 2 | 1 | steeringPercent | int8, −100…+100. Left/right for turn indicators. Live even while disarmed. |
| 3 | 1 | flags | bit0 braking (already hysteresis-filtered by the sender — drive the brake light from it directly), bit1 reverse (**reserved, always 0 in v1 — do not key anything off it**), bit2 drsOpen, bit3 armed, bit4 failsafe, bit5 lowBattery, bits 6–7 reserved (sender writes 0, **receivers must mask, never reject**). |
| 4 | 1 | gear | 1-based display gear, 1…6. |
| 5–6 | 2 | rpm | uint16. **Wheel/axle rpm** (one magnet), plausible max ~5000 — *not* engine rpm; derive engine revs from throttlePercent or scale this. |
| 7–8 | 2 | batteryMv | uint16, 2S pack millivolts. Display garnish — the `lowBattery` flag is the authoritative judgment (calibrated, 3 s-qualified, hysteresis-latched on board #1). |

## State matrix

| condition | throttlePercent | braking | armed | failsafe | telemetry (rpm/battery) |
|---|---|---|---|---|---|
| failsafe (link lost) | 0 | 0 | 0 | 1 | still live |
| disarmed idle | 0 | 0 | 0 | 0 | live |
| driving | as commanded | as filtered | 1 | 0 | live |

## Timing — and the one rule the receiver MUST implement

Frames are sent at a nominal **20 Hz** (every 50 ms). Receivers must tolerate jitter and
must not phase-lock to the rate.

**Mandatory staleness timeout:** if no CRC-valid frame arrives for **500 ms** (10 missed
frames), the receiver must enter its own local failsafe — engine sound to idle/off,
hazard blink. On a one-way link, a cut wire is otherwise indistinguishable from "the
last state persists forever".

## Worked example

The byte-identical frame is pinned by `test/test_link2/test_main.cpp`
(`test_golden_frame_bytes`):

```
A5 09 01 2A E7 0C 03 DC 05 DC 1E F9
│  │  │  │  │  │  │  └─┴─ rpm = 0x05DC = 1500        └─┴─ batteryMv = 0x1EDC = 7900   └ crc8
│  │  │  │  │  │  └ gear 3
│  │  │  │  │  └ flags 0x0C = drsOpen | armed
│  │  │  │  └ steeringPercent = 0xE7 = −25
│  │  │  └ throttlePercent = 0x2A = +42
│  │  └ version 1
│  └ length 9
└ start
```
Decoded: throttle +42 %, steering −25 %, DRS open, armed, no failsafe, gear 3,
wheel 1500 rpm, battery 7.900 V.
