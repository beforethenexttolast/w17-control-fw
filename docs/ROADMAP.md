# W17 Control Firmware — Review Verdict & Remaining-Work Roadmap

Status date: 2026-07-02 · Gift deadline: **2026-07-21 (19 days)**
Deliverable #1 (skeleton, crsf, failsafe, outputs, 27 native tests) is DONE and was
adversarially reviewed. This file records the confirmed defects and the plan for everything left.

---

## A. Review verdict on deliverable #1

### CONFIRMED — must fix before any bench flash with servos/ESC connected

**A1. [CRITICAL] Failsafe goes Active at boot with zero CRSF frames received —
steering slams to full lock for ~350 ms on every power-up.**
`lib/failsafe/src/FailsafeStateMachine.cpp:9` + `src/main.cpp:25`
The FSM has no "a frame has ever arrived" concept: link-valid is purely
`nowMs - lastFrameMs < 500`, and `lastFrameMs` boots as 0, so for the first 500 ms the link
*reads* valid with no frames. Trace: rearm window opens on the first loop tick → Active at
~t=155 ms → main.cpp reads the **zero-initialized** channel array → `rawChannelToNormalized(0)`
= −1211 → clamped −1000 → steering commanded to 500 µs (hard full-left, stalling the 35 kg·cm
DS3235SG against the printed rack) until t=500 ms. The ESC gets full-brake/reverse in the same
window and is saved *only* because `bootArmHoldMs` (2000) happens to exceed the window — an
unrelated tunable, not a designed guard. The header's own comment "never claim Active before a
frame has ever arrived" is an invariant the code does not implement, and
`test_climbs_to_active_after_full_rearm_window_from_boot` currently codifies the buggy behavior.
**Fix:** make frame-arrival an explicit FSM input (`everReceivedFrame_` latch set only on real
FrameReady events); regression tests: no frame ever fed → Safe forever, at any `nowMs`.

**A2. [MAJOR] No arm gate on the live throttle pass-through.**
`src/main.cpp:92` — CLAUDE.md §6.2 (non-negotiable #2) requires throttle neutral until arm
switch ON **and** throttle observed at neutral once. The placeholder drives the ESC from raw
ch3 with neither check: TX throttle held high at power-on goes straight to the ESC when the 2 s
boot hold expires. The arm *switch* legitimately waits for the channels module, but the
neutral-latch half needs nothing from it.
**Fix now:** minimal neutral-latch in main.cpp (throttle forced 0 until the decoded channel has
been seen inside a deadband around 992 once). Full arm-switch gate lands in the channels module.

### Triaged from the unverified pool (panel was cut off; manually assessed)

| # | Finding | Verdict | Action |
|---|---|---|---|
| A3 | Failsafe re-arm after link recovery snaps throttle to current stick position mid-drive | Real design gap | Channels-module arm gate must require throttle-neutral again after any failsafe (D2) |
| A4 | `Esp32LedcPwm::begin()` leaves duty 0; safe idle depends on caller ordering | Real, cheap | Write neutral/center pulse inside `begin()` (D1.5) |
| A5 | ESC boot-arm hold timed from static init, not from when pulses actually start | Real but small (static init → setup gap is ms) | Re-anchor hold to first `setThrottle()`/`begin()` (D1.5) |
| A6 | `main.cpp` reads `lastFrame()` every tick, violating its documented "valid only immediately after FrameReady" contract | Real contract violation, currently harmless | Copy channels out on FrameReady (D1.5) |
| A7 | Interleaved telemetry frames (LinkStatistics 0x14) reported as FrameInvalid; RX failsafe flag/LQ never parsed | Real limitation, by design for D1 | Parse LinkStatistics in D4; feeds real `rxFailsafeFlag` |
| A8 | RP1 configured with "Set Position" failsafe would defeat the timeout (RX keeps sending frames) | Real operational hazard | Bench checklist: RP1 failsafe mode MUST be **No Pulses** (D8) |
| A9 | Assembler discards buffered bytes on CRC fail (a 0xC8 inside a corrupt frame isn't rescanned) | True; costs ≤1 extra frame per corruption at 50–250 Hz frame rates | Accept; note as known limitation |
| A10 | `millis()` wraparound at 49.7 days | True; irrelevant for RC session lengths | Accept; comment only |
| A11 | Trim large enough to push center past an endpoint inverts direction silently | True; needs absurd config values | Config validation later, low priority |
| A12 | Loop free-runs instead of a fixed ≥50 Hz cadence | Spec-visible; harmless now | Fixed-tick scheduler when link2 lands (D6) |
| A13 | crsf module doesn't expose `linkUp`/`lastFrameMicros` per CLAUDE.md §2.1 | Deviation, undocumented | `CrsfReceiver` facade in D4 |

Review coverage note: the build-config reviewer and part of the test-gaps reviewer did not
complete (session limits), so platformio.ini/library.json semantics and test-gap analysis have
had one pass, not an adversarial one.

---

## B. Roadmap — everything remaining, in build order

### D1.5 — Safety fixes — ✅ DONE 2026-07-02
Scope: findings A1, A2(minimal), A4, A5, A6. All landed:
- `FailsafeStateMachine`: `everReceivedFrame_` latch; `update(nowMs, frameArrivedThisTick,
  rxFailsafeFlag)` — frame arrival is now an explicit event, timestamps alone can never make
  the link look healthy. Boot-climb test fixed (it had codified the bug); new regression:
  no frame ever → Safe at every timestamp.
- `main.cpp`: throttle neutral-latch (reset on every failsafe episode — also closes A3 at
  this level until the channels module lands); channels copied out on FrameReady (A6).
- `Esp32LedcPwm::begin(initialPulseMicros)`: commands the safe position immediately on attach.
- `EscOutput`: arm-hold anchored to the first `setThrottle()` call; new regression test.
Verified: 29/29 native tests pass, esp32dev builds clean.

### D2 — `channels` module + full arm gate (safety §6.2) — ✅ DONE 2026-07-02
- `lib/channels/ChannelDecoder`: config-table raw→named mapping (defaults: steering ch1,
  throttle ch3, arm ch5, DRS ch6, gearUp ch7, gearDown ch8 — placeholders, verify at bench),
  piecewise-exact ±1000 normalization, invert flags, switch hysteresis (+250/−250),
  first-decode level seeding (no phantom edges at boot), OFF→ON edge detection.
- `lib/channels/ArmGate`: armed ⇔ switch ON ∧ throttle-seen-neutral since last disarm;
  instant disarm on switch-off or failsafe; recovery requires fresh neutral (closes A3).
- main.cpp: decode on every frame (phantom-edge-proof), steering live while disarmed,
  temporary neutral-latch and rawChannelToNormalized placeholders removed.
- Verified: 45/45 native tests (16 new), esp32dev clean, lib/channels Arduino-free.

### D3 — `gearbox` — S/M, ~1 day
- Pure `(throttle, gear) → output` with per-gear max cap + expo curve (3–4 gears);
  gear up/down on switch edges from channels. Table-driven, heavily tested.

### D4 — CRSF LinkStatistics + receiver facade — S/M, ~1 day
- Parse frame 0x14 (LQ, RSSI, RX failsafe indication where available); route a real
  `rxFailsafeFlag` into the failsafe FSM. `CrsfReceiver` facade exposing
  `channels / linkUp / lastFrameMicros / failsafe` per CLAUDE.md §2.1 (closes A7, A13).

### D5 — `telemetry` — M, ~1–2 days
- `IAdc` seam + battery volts conversion (27k/10k divider, calibration factor constant,
  trimmed on bench with multimeter); Hall rising-edge ISR behind a thin wrapper +
  pure pulses→RPM/ground-speed conversion (`magnetsPerRev=1`, wheel circumference const).
  Conversions unit-tested; ISR/ADC are esp32-only glue.

### D6 — `link2` UART frame to ESP32 #2 — M, ~1–2 days
- Documented frame: start byte, length, payload (throttlePercent, flags: braking/reverse/
  drsOpen/armed/failsafe, gear, rpm, battery mV), CRC8 (reuse 0xD5 impl). Pure encode+decode
  with round-trip tests; esp32 UART1 (GPIO25/26) sender at 115200.
- Restructure loop to fixed cadences (50 Hz control, ~20 Hz link2) — closes A12.

### D7 — Wokwi simulation (Stage 2) — S/M, ~1 day
- `wokwi.toml` + `diagram.json`: servos on 13/14/18, pot on GPIO34, button on GPIO35;
  canned-CRSF feeder into UART2. End-to-end sanity without hardware.

### D8 — Hardware bring-up (Stage 3) — bench days, gated on parts
Checklist: flash/bind RP1 + TX (same ELRS version + bind phrase); **set RP1 failsafe = No
Pulses** (A8); verify CRSF at GPIO16 (420 k, not inverted); **confirm the channel-map defaults
in `lib/channels/ChannelDecoder.hpp` against the actual TX mapping, and verify every switch
traverses BOTH hysteresis thresholds (±250) — especially the ARM switch's OFF direction**;
servo center in firmware BEFORE attaching steering linkage (atlas MECH-02); Hobbywing ESC
neutral/range calibration + sensored mode; wheels-off-ground throttle tests only until
failsafe + arm gate proven on the bench; Hall pulse + ADC calibration (write factor into
config); link2 smoke test with board #2.

### Deferable past 2026-07-21 without hurting the gift
Gimbal pan/tilt (already optional), CRSF telemetry uplink to TX, BX100-style low-voltage
warning polish, gearbox curve tuning (ship conservative defaults), link2 niceties. The car
drives and is gift-ready with D1.5→D4 + D8; sound/light (D6) is the biggest "wow" optional.

### Calendar sketch (19 days)
- Jul 02–03: D1.5 + start D2 · Jul 04–06: D2 + D3 · Jul 07–08: D4 + D7 (Wokwi early — parts
  may still be shipping) · Jul 09–11: D5 + D6 · Jul 12–20: Stage-3 bench + car integration,
  paint/assembly margin (firmware must NOT be the long pole; everything above except D8 needs
  zero hardware).

### Risks
1. **Parts arrival gates D8 only** — everything else is native/Wokwi; keep it that way.
2. **ESC behavior assumptions** (arming, brake-vs-reverse mode) unverified until bench —
   budget one bench session purely for ESC characterization.
3. **RP1 failsafe misconfiguration silently defeats the firmware timeout** (A8) — checklist item.
4. Single-person schedule: D5/D6 are parallelizable in scope but serial in practice; the
   deferable list is the pressure valve.
