# ESP32 #1 "Control Board" — Firmware Build Brief (Claude Code handoff)

## 0. Context — what you're building and why

I'm building a **1/10-scale FPV 3D-printed RC Formula 1 car** (Mercedes W17 livery, a gift, hard deadline **21 Jul 2026**), on a belt-drive OpenRC F1 chassis. It has **two ESP32-WROOM-32 boards**:

- **ESP32 #1 — CONTROL (this firmware).** Takes the radio link in, decides what the car does, drives the steering servo + ESC + DRS/gimbal servos, runs failsafe + a "virtual gearbox," and reports state to board #2.
- **ESP32 #2 — sound + light** (separate firmware, based on **TheDIYGuy999** RC sound/LED projects + a MAX98357A I2S amp + WS2812 LEDs). **Out of scope here** — #1 only talks *to* it over a one-way UART.

I'm strong on **electronics + Linux**, newer to RC. I want **modular, unit-testable code**, hardware access behind thin interfaces so most of it compiles and tests on my laptop with **no ESP32 attached**. Be concise and verdict-first; I review diffs.

**Stack:** PlatformIO + Arduino-ESP32 framework (ESP-IDF acceptable if you justify it). Board env `esp32dev`, plus a `native` test env.

---

## 1. Hardware target + pin map

MCU: **ESP32-WROOM-32 DevKit V1**.

**Pin map — STARTING PROPOSAL. Reconcile against my wiring atlas (`docs/w17_wiring_assembly_atlas.html`, which I'll copy into the repo) before locking it. Keep pins in one config header so they're trivial to change.**

| Signal | GPIO | Notes |
|---|---|---|
| **CRSF in** (from RadioMaster RP1 TX pad) | **16** (UART2 RX) | ELRS CRSF: **420000 baud, 8N1, NOT inverted** |
| CRSF telemetry out (to RP1 RX pad) | 17 (UART2 TX) | optional uplink (battery/RPM) |
| **UART → ESP32 #2** (TX) | 25 (UART1, remapped) | 3.3 V logic, **common ground**, e.g. 115200 8N1 |
| UART ← ESP32 #2 (RX) | 26 (UART1, remapped) | optional ack/handshake |
| **Steering servo** (DSServo DS3235SG, 180°) | 13 | LEDC 50 Hz servo PWM, center 1500 µs |
| **ESC throttle** (Hobbywing QuicRun 10BL120) | 14 | LEDC 50 Hz, neutral 1500 µs, **arm sequence on boot** |
| **DRS servo** (MG90S, 2-position) | 18 | LEDC 50 Hz |
| Gimbal pan (MG90S) — *optional/deferred* | 19 | LEDC 50 Hz |
| Gimbal tilt (MG90S) — *optional/deferred* | 23 | LEDC 50 Hz |
| **Battery sense** (27 kΩ/10 kΩ divider) | 34 | ADC1_CH6, **input-only**, 11 dB atten |
| **Wheel-speed** (A3144 Hall) | 35 | **input-only**, external 10 kΩ pull-up to 3.3 V, rising-edge ISR |

GPIO 34/35 are input-only (no internal pull-ups) — fine here: the battery line is analog, and the Hall already has an external 10 kΩ pull-up on the board. Avoid GPIO 6–11 (flash). GPIO 0/2/12/15 are strapping pins — don't use for outputs that are driven at boot.

**Inputs/peripherals summary:** RP1 ELRS receiver (CRSF) → steering servo, ESC, up to 3× MG90S, battery divider (ADC), Hall sensor (wheel speed), UART to board #2.

---

## 2. Functional spec — build as separate, testable modules

Each module = a small library under `lib/` with pure logic separated from hardware. Suggested set:

1. **`crsf` — CRSF receiver/parser.** Consume the ELRS serial stream on UART2. Decode `RC_CHANNELS_PACKED` frames (type `0x16`): 16 × 11-bit channels, raw range **172–1811, center 992**. Validate the **CRC8 (poly 0xD5)**; reject bad frames. Expose: `channels[16]`, `linkUp` (bool), `lastFrameMicros`, and any failsafe flag the RX signals. **The byte-level parser must be a pure function over a buffer** so it unit-tests with canned frames.
2. **`channels` — logical channel map.** Translate raw channels → named controls via a **config table** (easy to re-map): `throttle`, `steering`, `drs` (2-pos), `gearUp`/`gearDown` (or a gear selector), `arm`, optional `pan`/`tilt`. Normalize to clean ranges (e.g. −1000…+1000, or 0…1000).
3. **`gearbox` — virtual gearbox.** N gears (start with 3–4). Each gear applies a **max-output cap + expo curve** to throttle, so low gears = gentle/limited, top gear = full. Up/down via switches or momentary edges. **Pure function: `(rawThrottle, gear) → outputThrottle`.** This is the "feel" layer; make it heavily testable.
4. **`failsafe` — SAFETY-CRITICAL.** If **no valid CRSF frame for T ms** (start at 500 ms) **OR** the RX failsafe flag is set → force outputs safe: **throttle = neutral, steering = center, DRS = closed.** Latch until the link returns *and* a re-arm condition is met. Pure state machine over (linkUp, lastFrameMicros, now) → safe/active. **Test this before any feature.**
5. **`outputs` — servo/ESC PWM.** LEDC-based 50 Hz generation. Steering (~500–2500 µs, configurable endpoints + trim), ESC throttle (1000–2000 µs, neutral 1500, **boot arm sequence**: hold neutral N ms before accepting throttle), DRS (two positions), optional pan/tilt. **Put the actual `ledcWrite` behind an interface** so logic tests use a mock and assert the commanded µs.
6. **`telemetry` — battery + wheel speed.** Battery: read ADC1 (27 k/10 k divider, 11 dB atten), apply a calibration factor (I'll trim it with a multimeter) → volts. Wheel speed: count Hall rising edges in an ISR; convert pulses → RPM/ground-speed using `magnetsPerRev` (start = 1) and wheel circumference. Pure conversion functions; the ADC read + ISR are the only hardware bits.
7. **`link2` — UART to ESP32 #2.** A small **framed message** (start byte + payload + checksum) carrying: `throttlePercent`, `braking` (bool), `reverse` (bool), `rpm`/`speed`, `gear`, `drsOpen`, `armed`, `failsafe`. Define + document the frame; board #2 (TheDIYGuy999) consumes it to drive engine sound + WS2812 brake/indicator lights. Encode/decode must unit-test.
8. **`main.cpp` — wiring + loop.** Non-blocking. Parse CRSF as bytes arrive; run failsafe + outputs at a fixed cadence (≥50 Hz). **No `delay()` in the control path.**

---

## 3. Behavior defaults (I can change these)

- **Channel map (verify against my TX):** throttle = ch3, steering = ch1 (or ch4), DRS = a 2-pos switch, gear up/down = 2 switches (or a 3-pos), **arm = a switch (must be ON to allow throttle)**.
- **Brake light** = throttle below neutral (with a small deadband).
- **ESC** is set to sensored mode + forward/brake (or forward/reverse) in *its own* config; the firmware just emits the PWM.
- **DRS** = wing-flap servo toggled by its switch.

---

## 4. Validation plan — do it in this order (most needs no hardware)

**Stage 1 — native unit tests (PlatformIO `native` env; Unity or doctest).** Test the pure logic with **zero hardware**: CRSF parser (feed canned byte arrays → assert decoded channels; assert CRC-bad frames are rejected), channel map, gearbox curves, failsafe transitions (inject "no frame for T ms" and the failsafe flag), output µs scaling, `link2` frame encode/decode. This is ~half the firmware and catches the real bugs.

**Stage 2 — Wokwi simulation (`wokwi.toml` + `diagram.json`).** Virtual ESP32 with servos on the PWM pins, a **potentiometer on GPIO34** (battery sim), and a **pushbutton on GPIO35** (wheel-pulse sim). Drive CRSF via a test harness feeding canned frames into the UART. Confirm end-to-end flow + timing without real parts.

**Stage 3 — real hardware (last).** Real CRSF from `elrs-joystick-control` (DualShock → PC → CRSF over FT232 → ELRS TX module → RP1 → ESP32 #1). Verify PWM on a servo/scope, the ADC divider reading, and the Hall ISR on the bench — then on the car.

---

## 5. Architecture rules

- **Hardware-abstracted:** no ESP32/Arduino headers in the pure-logic files. Wrap UART, LEDC, ADC, GPIO/ISR behind thin interfaces; provide a real impl (esp32) and a mock/host impl (native).
- **PlatformIO layout:** `src/` (entry + glue), `lib/` (each module as a library), `test/` (native tests), `platformio.ini` with **`[env:esp32dev]`** and **`[env:native]`**, plus `wokwi.toml` + `diagram.json`.
- Document magic numbers (CRSF baud/ranges/CRC poly, servo µs) with a one-line source.
- Readable over clever — I review every diff.

---

## 6. Safety priorities (non-negotiable, build first)

1. **Failsafe first** — implement + test link-loss → throttle neutral *before* any feature.
2. **Arm gate** — throttle stays neutral until the arm switch is ON **and** throttle has been observed at neutral once (no arm-into-full-throttle).
3. **ESC boot arm sequence** — neutral for N ms before accepting throttle.
4. **Battery telemetry is monitoring only** — warn, don't auto-cut.

---

## 7. Bench-wiring notes (so the firmware matches the build)

- **Battery divider:** 27 kΩ (battery side) + 10 kΩ (to GND), tap → GPIO34, ADC1, 11 dB atten → 8.4 V reads ≈ 2.27 V. I'll calibrate the factor with a multimeter.
- **Hall A3144:** 5 V supply, **10 kΩ pull-up to 3.3 V**, open-collector output → GPIO35, rising-edge ISR. One axle magnet ⇒ `magnetsPerRev = 1`.
- **ESC:** its built-in BEC **+5 V (red) wire is isolated** — ESP32 drives only the ESC **signal** wire + shares ground. Separate 5 A UBECs power the ESP32 rail.
- **UART to #2:** 3.3 V logic, common ground, 115200 8N1 (adjust if needed).

---

## 8. First deliverable — stop and show me after this

1. PlatformIO skeleton: `platformio.ini` (`esp32dev` + `native`), folder layout, the hardware-interface seams.
2. The **`crsf` parser** module **+ its native unit tests** (canned frames, including a CRC-fail case).
3. The **`failsafe`** + **`outputs` scaling** modules **+ tests**.

**Then pause and show me** so I can verify the foundation before you build gearbox / channel map / telemetry / `link2` / Wokwi. Use plan mode for anything that touches multiple files.
