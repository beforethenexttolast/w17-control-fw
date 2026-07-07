# 01 — Specification Compliance & Owner Expectations

Source: "Specification compliance" dimension in `_raw_audit_findings.json`; severities/verdicts
per `_verification_results.md`. Findings reference `10_risk_register.md` (`R##`) rather than
restating full bodies.

## Verdict

The project delivers **far more** than CLAUDE.md's stated first deliverable (which said "stop
and show me" after skeleton + crsf + failsafe + outputs): all modules, a second firmware, a
ground station, telemetry, gimbal, and a tuning console — all heavily native-tested, with an
honest ROADMAP and bench runbook. **But every "DONE" is software-DONE, not hardware-verified**:
CRSF-in, PWM-out, ADC, Hall, link2 UART, the ELRS telemetry backchannel, and WebRTC video are
all bench-gated with zero hardware runs. The concrete owner-surprise gaps: the HUD reads
`armed`/`failsafe` the car never transmits (a real link loss shows "Telemetry: sim", not a
warning); gear counts and drive-mode labels disagree across doc/firmware/HUD; and the whole
telemetry return path hangs on an unsolved exclusive-COM-port question. `npm run demo` masks
all of this by hand-filling every field — a beginner owner would reasonably believe the HUD
shows live car state today. It does not yet.

## What is genuinely well-designed

- **ROADMAP.md is a real audit trail**, not marketing: original adversarial-review defects
  (A1 boot-full-lock, A2 arm gate) recorded with file:line, fixes marked with regression-test
  notes, a triaged A3–A13 table.
- **CLAUDE.md §6 safety order was actually followed**: failsafe FSM with `everReceivedFrame_`
  latch, ArmGate fresh-neutral per episode, ESC boot-arm hold, battery monitoring-only — all in
  `src/main.cpp:311-334` with reasoning inline.
- **D8_BENCH_BRINGUP.md** is a strong 11-phase gated runbook; the golden rule (no ESC power
  until failsafe+arm proven, Phase 5) is an explicit gate.
- **Viewer-only ground station** is consistent everywhere; elrs-joystick-control keeps control;
  the VLC zero-code fallback is documented.
- **link2 is genuinely documented** (worked hex example pinned by a golden-frame test), meeting
  CLAUDE.md §2.7's framed-message requirement.
- **docs/TELEMETRY.md transport reasoning** is thorough and self-aware (why standard CRSF frames
  beat MSP and WiFi/ESP-NOW).

## Findings (→ risk register)

| R## | Sev (verified) | Finding | Spec angle |
|---|---|---|---|
| R01 | High | HUD reads `armed`/`failsafe` the car never transmits; "LINK LOST" is demo-only | TELEMETRY.md documents a contract field the firmware never delivers; real link loss shows "Telemetry: sim" |
| R05 | Med | Gear count 4 (fw) / 6 (link2 doc) / 8 (HUD) | Live gear renders on the wrong ring; three docs disagree |
| R14 | Med | Telemetry return path blocked on the exclusive FT232 COM port | ROADMAP items 5/8 marked "✅ DONE" but the delivered capability hinges on an unanswered COM-sharing question |
| R08 | Med (ADJUSTED) | Pin-map reconciliation obligation | Verification found the atlas has **no GPIO numbers** ("illustrative"); PinMap agrees with CLAUDE.md+BOM — only a physical continuity check remains |
| R19 | Low | driveMode label 1 = "Gearbox" (fw/doc) vs "RACE" (HUD/TELEMETRY.md) | Cosmetic; numbers agree |

**Carried Low (appendix of the register, not re-verified):**
- D8 lists the gimbal as "decoded, unwired" but it is fully wired in main.cpp — internal doc
  contradiction (stale Deferred entry).
- GPS groundspeed km/h×10 integer conversion is coarse at low speed; and whether ELRS relays a
  lat/lon-zeroed GPS frame is unproven (→ relates R13).
- CLAUDE.md §8's "stop and show me" first-deliverable gate was overtaken — everything was built
  in one continuous pass; the intended foundation review happened only via ROADMAP review.

## Open questions (owner-relevant)

- Should HUD `armed`/`failsafe` be made real (encode in FLIGHTMODE) or documented as sim-on-loss? (→ R01)
- Which gear count and which drive-mode labels are authoritative for the delivered car? (→ R05, R19)
- Does elrs-joystick-control expose a telemetry forward, or is com0com required? (→ R14)
- Will real ELRS relay the partially-populated GPS 0x02 + custom FLIGHTMODE 0x21? (→ R13)

## Hardware validation hooks

See `11_hardware_validation_plan.md`: A2.1 (pin continuity), CG2/CG3 (telemetry path + relay),
CG1 (H.264 gate), CG5 (observe real link-loss HUD behavior and decide acceptability).
