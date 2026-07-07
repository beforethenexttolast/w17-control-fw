# 08 — Simulation & Test Coverage Review (what is actually proven)

Source: "Simulation and automated-test coverage" dimension in `_raw_audit_findings.json`;
severities/verdicts per `_verification_results.md`. Findings reference `10_risk_register.md`.

## Verdict

The pure-logic suites (147 + 40 + 21, all green) are **genuinely strong**: they exercise the
real production code (same headers main.cpp uses — no shadow logic), use injected mocks that
assert commanded microseconds/bytes rather than tautologies, and pin exact byte vectors,
boundary transitions, and named regression findings (A1, A3, A5…). The control-fw Wokwi sim is
the best part: `esp32dev_sim` runs the **byte-identical** setup()/loop() as the gift firmware
with CRSF through a real Serial2 loopback. **But the green suite proves almost nothing about
hardware**: every `*_hal_esp32` file (Hall ISR, LEDC, ADC, NVS, I2S) is untested and
unsimulated; the soundlight repo has **no Wokwi sim at all** (its feeder injects past the UART);
and the telemetry return path is bench-gated. Honest confidence statement: **high** that the
decode/state-machine/scaling math is correct; **low** that the assembled firmware behaves
correctly on real silicon — which is exactly what the hardware validation plan exists to close.

## What is genuinely well-designed

- **Tests exercise production logic, not a reimplementation** — crsf tests build with the same
  `CrsfFrameBuilder.hpp` the sim uses and decode with the production receiver.
- **`esp32dev_sim` is the real firmware, verified**: it only `extends esp32dev` +
  `-DW17_SIM_CRSF_FEEDER`; main.cpp guards only the feeder + prints, so the 50 Hz control tick
  is byte-identical; diagram.json loops TX2→RX2 so the genuine UART driver runs.
- **Failsafe has the deepest coverage** (as it should): boots-Safe, never-Active-with-no-frame
  (A1), inclusive timeout boundary, RX-flag-wins-over-fresh-frames, latch-despite-one-good-tick,
  rearm-window chatter reset.
- **The hold-position/misconfigured-RX hazard is both unit-tested and reproduced end-to-end**
  in the Wokwi HOLD_POSITION_FAILSAFE phase at 50% throttle.
- **ArmGate pinned from multiple angles**: arm-with-displaced-stick blocked, fresh neutral
  required after switch-off AND after failsafe (A3), boot-arm anchored to first command (A5).
- **Assemblers hardened against real serial garbage**, including the clever
  0xA5-inside-payload resync case.
- **Numeric edges covered where they matter**: full 0..2047 channel clamp, gearbox monotonicity
  sweep, exact micro-permille ERS accumulator with dt-stall clamp, WheelSpeed first-edge /
  implausible-period / decay/timeout.
- **CRC agreement pinned four ways** (crsf, link2, settings, JS — all reproduce 0xBC).
- **soundlight `test_integration` is a real hardware-free end-to-end chain**: link2 bytes →
  monitor → enginesim → synth render (peak-checked) → lights, including a link-loss phase.
- **Ground replay tests pin enum-vs-interpolate** (driveMode steps, never 1.5) and hard-assert
  the feel constants against drift.

## Findings (→ risk register)

| R## | Sev (verified) | Finding | Coverage angle |
|---|---|---|---|
| R11 | Med (ADJUSTED High→Med) | soundlight has no Wokwi sim; all `*_hal_esp32` code untested/unsimulated | The "zero board-#2 validation" framing was adjusted (it has a native e2e test + feeder), but the feeder **bypasses the real UART**, and the dual-core audio race / I2S / WS2812 / real serial RX have no pre-hardware coverage |
| R16 | Med | main.cpp orchestration has no test; the Wokwi sim is not asserted in CI; named gaps (NVS-on-flash, ADC rails, Hall bounce, board-#2 boot-staleness) | The modules are proven; their **composition** (ordering, boost-after-gate, Safe-branch-still-transmits) is covered only by a manual, unasserted sim |
| R06 | Med | Cross-repo golden vectors are duplicated with no CI drift guard | Each repo's tests stay green after a one-sided edit — the same root as the link2 duplication |

**Carried Low (register appendix):**
- **The Wokwi 420000-baud loopback is itself unverified** — SIMULATION.md's first-run checklist
  boxes are all unchecked; if Wokwi can't clock 420 k, the sim silently proves nothing
  (firmware would sit in permanent failsafe). Cheap to close: run it once (plan A1.6).
- **Ground-station renderer/main are entirely untested** (hud.js, whep.js, CrsfSerialSource,
  main.js, mediamtx.js) — tests cover only `shared/`; the HUD labels, source selection, and
  serial wiring have no automated coverage.

## Open questions

- Has the control-fw Wokwi sim **ever been run** to a live link (failsafe=0), or only built?
  (→ R16; plan A1.6 — the single cheapest confidence win available today)
- Must the Hall `read()` snapshot be coherent? The two-independent-atomics design permits a
  torn {count, period} pair — does WheelSpeed's math tolerate it? (telemetry-only)
- Was a soundlight `diagram.json` intended and dropped? `esp32dev_sim` building in CI without
  wokwi files can read as a "sim" that doesn't exist. (→ R11)
- Is there any orchestration that could catch link2/constants drift across the three repos?
  (→ R06 — today: none)

## Hardware validation hooks

`11_hardware_validation_plan.md`: **A1.6** (run the Wokwi sim to DRIVING — before power),
**B3.5/B3.6** (I2S + show() coexistence — the untested board-#2 hardware layer), **B4.1**
(ADC rails), **C2** (Hall EMI), **CR1** (NVS corruption on real flash), **B3.8** (mid-frame
boot ordering), **CG4** (JS decode vs real frames).
