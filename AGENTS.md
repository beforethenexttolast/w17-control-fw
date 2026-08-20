# ESP32 #1 "Control Board" — Firmware Maintenance Guide

> **Codex-session guidance. Corrected 2026-08-17 (the 2026-08-11 port inverted ownership). Source of truth: this repo's CLAUDE.md + the workspace CLAUDE.md/AGENTS.md.**

Repo-specific rules for `w17-control-fw`. Shared W17 workspace rules (ownership, the seven
cross-repo safety boundaries, commit/review etiquette) live in the parent
`../AGENTS.md`; volatile status (checkpoints, gate state) lives in `../CURRENT_STATUS.md`.
This file covers what is specific to the control firmware and is stable across sessions.

## Ownership and session role (read first)

This repo is **Claude-Code-owned** (registry: `WORKSPACE_MAP.md` at the workspace root). A
Codex session here is a **guest**: read-only by default; edits only when the owner's task
explicitly names this repo. All safety boundaries, gates, and invariants below bind Codex
sessions identically.

## What this firmware is

**ESP32 #1 — CONTROL.** Takes the radio link in, decides what the car does, drives the
steering servo + ESC + DRS + camera-gimbal servos, runs failsafe + a "virtual gearbox," and
reports state one-way to ESP32 #2 (sound + light). It is the **only producer of final
hardware outputs**, and only from already-arbitrated inputs.

- MCU: MH-ET Live D1-Mini ESP32 (ESP32-WROOM-32 class; owner decision 2026-07-24). The
  on-hand USB-C DevKit V1 clones are TEST/SPARE boards only, not the car's controllers.
- Stack: PlatformIO + Arduino-ESP32. Board env `esp32dev`, plus a `native` test env.
- The build is mature: the module set below exists, is unit-tested, and is reviewed. Treat
  this as a maintenance codebase, not a greenfield one — no day-1 scaffolding is pending.

## Modules (each a library under `lib/`, pure logic split from hardware)

- `crsf` — ELRS CRSF receiver/parser (UART2). Decodes `RC_CHANNELS_PACKED` (16 × 11-bit,
  raw 172–1811, center 992), validates CRC8 (poly 0xD5). Byte-level parser is a pure
  function over a buffer.
- `channels` — raw channels → named controls via a config table (throttle, steering, DRS,
  gear up/down, arm, pan/tilt on ch9/ch10).
- `gearbox` — virtual gearbox: per-gear output cap + expo curve. Pure `(rawThrottle, gear) →
  outputThrottle`.
- `failsafe` — SAFETY-CRITICAL pure state machine over (linkUp, lastFrameMicros, now) →
  safe/active. Link loss or RX failsafe → throttle neutral, steering center, DRS closed;
  latch until link returns and a re-arm condition is met. Leaving Safe requires a LINK
  PROOF (2026-08-20 hardening): ≥150 ms elapsed since the proof anchor (a frame-bearing
  tick) AND ≥5 frame-bearing ticks since that anchor, with no intra-proof frame gap
  >60 ms (a gap discards the proof) — a single CRC-valid frame (or any frame trickle
  below the assumed 50–250 Hz cadence) can never reach Active; loss detection is unchanged.
  `hasEverLinked()` (the SHOWCASE D4 `everLinkedThisBoot`) latches at proof completion,
  not at first raw frame. Also home of `GimbalDecay`
  (vision decision 11, 2026-08-16): during failsafe the two gimbal axes decay to center
  instead of holding, rate = NVS tunable `gimbal.decay`; recovery slews back, no snap.
- `outputs` — LEDC 50 Hz PWM for steering, ESC (neutral 1500 µs, boot arm sequence), DRS,
  pan/tilt. Real `ledcWrite` sits behind an interface so logic tests assert commanded µs.
- `telemetry` — battery ADC (divider + calibration → volts, monitoring only) and Hall
  wheel-speed (rising-edge ISR → RPM/speed). Pure conversion functions.
- `ers` — energy-recovery/ERS behavior feeding telemetry + link2 (harvest state, etc.).
- `link2` — framed one-way UART message (start byte + payload + checksum) to ESP32 #2
  carrying drive + telemetry state for sound/light. The payload field list lives in
  `docs/link2_protocol.md` ONLY — deliberately not summarized here: a second list drifts
  (the one that used to sit here had silently fallen behind the payload table; 2026-08-16
  vision audit, defect 7). **This repo owns the link2 protocol**; the soundlight repo
  holds a copy — protocol changes happen here first.
- `settings`, `console`, `hal`, and the `*_hal_esp32` impls — persisted config, serial
  console, hardware seams. Pure logic files carry no Arduino/ESP32 headers. The UART0 console
  char-IO is its own `console_hal_esp32` lib, kept separate from the NVS store (`settings_hal_esp32`)
  so the delivery build links the store without the console.

## Delivery vs tuning builds (stable invariant)

Three concerns are deliberately separated — do not recouple them:

- **Loading** validated NVS tuning happens in **every** build. Delivery `esp32dev` loads the
  saved blob at boot through the shared `settings::loadOrDefault` (guard chain: length → CRC →
  version → `Settings::valid()`; any failure ⇒ complete compiled defaults, never a partial mix)
  and applies it via the modules' `setConfig()`.
- **The tuning console** (UART0 surface + command parser) is `-DW17_TUNING_CONSOLE`
  (`[env:esp32dev_tuning]`) **only**. Delivery opens no UART0 console and links no console
  parser (ELF-verified).
- **Mutation** (`set`/`save`/`reset`) is reachable only through that console — delivery cannot
  change, save, or reset tuning.

So: **delivery `esp32dev` reads validated NVS but is console-free and read-only.** Keep NVS
loading independent of `W17_TUNING_CONSOLE`. Canonical delivery runbook:
`docs/D8_BENCH_BRINGUP.md` Phase 11a.

## Pin map — where the truth lives

Pins are **maintained in the config header `lib/config/include/config/PinMap.hpp`**, not
decided from this file. Reconcile any pin question against that header and the current
project docs (`docs/`, `docs/w17_wiring_assembly_atlas.html`) — do not treat any table in a
brief as authoritative. GPIO 34/35 are input-only (battery analog, Hall with external
pull-up); avoid GPIO 6–11 (flash) and be careful with strapping pins 0/2/12/15.

## Architecture rules

- No ESP32/Arduino headers in pure-logic files; wrap UART, LEDC, ADC, GPIO/ISR behind thin
  interfaces (real esp32 impl + native mock).
- Config structs validate at the definition site; document magic numbers (CRSF baud/ranges/
  CRC poly, servo µs) with a one-line source.
- Readable over clever — every diff is reviewed.

## Test philosophy (most needs no hardware)

- **Stage 1 — native unit tests** (`pio test -e native`): CRSF parser (canned frames incl.
  CRC-fail), channel map, gearbox, failsafe transitions, output µs scaling, link2 codec.
  This catches the real bugs; run it before any change ships.
- **Stage 2 — Wokwi sim** (`wokwi.toml` + `diagram.json`): virtual ESP32, servos on PWM
  pins, pot on GPIO34 (battery sim), button on GPIO35 (wheel-pulse sim), CRSF via a harness.
- **Stage 3 — real hardware** (last, gated — see below).

## Safety priorities (non-negotiable, order preserved)

1. **Failsafe first** — link-loss → throttle neutral, proven before any feature.
2. **Arm gate** — throttle stays neutral until the arm switch is ON *and* throttle has been
   seen at neutral once (no arm-into-full-throttle). A failsafe episode LATCHES a disarm
   (OWNER-RATIFIED 2026-08-20): re-arming afterwards additionally requires the arm switch
   seen OFF and then ON again on the proven link — link recovery with the switch still on
   never re-arms by itself, and a boot with the switch already ON demands the same toggle
   (`lib/channels/include/channels/ArmGate.hpp`).
3. **ESC boot arm sequence** — neutral for N ms before accepting throttle.
4. **Battery telemetry is monitoring only** — warn, never auto-cut.

## Hardware gates — READ BEFORE ANY BENCH WORK

Durable gate wording (live status in `../CURRENT_STATUS.md`):

- **A1.1–A1.6 software / pre-power validation: COMPLETE.**
- **A2 no-power checklist: committed but NOT EXECUTED.** No measurements recorded; A2 is not
  closed. Canonical checklist: `project-review/13_phase_a_a2_no_power_checklist.md`.
- **Phase B (powered bring-up) is BLOCKED** until A2 is filled in, reviewed, and approved.
- **No battery, no USB-powered bring-up, no bench PSU, no powered hardware** unless the
  active task explicitly says Phase B *and* A2 has passed. Do not flash or power hardware in
  a normal maintenance session.
- **ESC motor power stays disconnected** until the relevant bench gates approve it (failsafe
  + arm chain proven live in Phase A → B).
- Still bench-validation items (not settled by code review alone): battery ADC calibration,
  PWM timing, real ESC behavior, Hall ISR under real pulses, and any real-hardware timing.
  (I2S/WS2812 timing lives in the soundlight repo but is the same class of item.)

## iPhone / pan-tilt boundary

- **Firmware is and stays unaware of the iPhone** — it never parses iPhone JSON or receives
  iPhone UDP.
- **No iPhone → CRSF. No iPhone → servo / gimbal / ESC.**
- Camera pan/tilt is a normal **stick-driven CRSF ch9/ch10** path, source-agnostic — it is
  **separate from** iPhone-derived head tracking.
- **iPhone-derived active pan/tilt remains BLOCKED** behind a separate, reviewed safety
  milestone. No servo movement from head tracking until that milestone approves it. See
  `project-review/iphone_pan_tilt_firmware_readiness.md`.

## Working here

- Edits stay in this repo only — and, as a guest session, only when the owner's task
  explicitly names this repo (see the ownership section above). Never modify sibling repos
  unless explicitly asked.
- Keep pure logic testable and hardware behind seams; add/extend native tests with behavior.
- Plan first for anything touching multiple files; show diffs.
