# 03 — Hardware & Electronics Review

Source: "Hardware / electronics correctness" dimension in `_raw_audit_findings.json`;
severities per `_verification_results.md`. Findings reference `10_risk_register.md` (`R##`).

## Verdict

The pin maps in both firmwares are **internally consistent, match CLAUDE.md §1/§7 exactly, and
are electrically sound**: no strapping pins (0/2/12/15/5) driven as outputs, input-only
GPIO34/35 used correctly (analog sense; externally-pulled Hall), and the 5-servo LEDC
allocation (channels 0–4, all 50 Hz/16-bit) has no timer conflict because shared timers carry
identical config. Battery-divider math is correct with margin proofs. The wiring atlas turned
out to be **illustrative-only** (no GPIO numbers, footer disclaims them), so the effective
pin authority is CLAUDE.md + BOM — which agree with PinMap. The real risks are the classic
bench-only ones: the WS2812 3.3 V→5 V data level rides a documented-but-marginal diode-drop;
ESC/servo signal pins float high-Z for tens of ms at boot; Hall EMI double-counting near the
brushless motor is anticipated but unproven. **No hardware-damaging or unsafe-motion defect was
found in this dimension** — everything critical is procedural (D8) and bench-gated.

## What is genuinely well-designed

- **PinMap.hpp matches CLAUDE.md pin-for-pin** with per-pin provenance comments; board #2's map
  explicitly notes "all non-strapping, none input-only". No strapping pin driven on either board.
- **LEDC allocation is conflict-free by construction**: 5 servos on channels 0–4, all the same
  50 Hz/16-bit, so Arduino's channel-pair timer sharing cannot conflict.
- **`Esp32LedcPwm::begin(initialPulseMicros)` commands a known-safe pulse on attach** and
  setup() passes center/neutral/closed per channel — closing the "LEDC idles at duty 0" gap (A4).
- **Battery divider math is correct and overflow-guarded**: 37/10 ratio, 8.4 V → ~2.27 V pin
  under the 11 dB ~2.45 V ceiling; `valid()` proves the integer conversions can't overflow;
  eFuse-calibrated `analogReadMilliVolts` with a 4-read burst against the ~7.3 k source impedance.
- **Hall ISR done properly**: IRAM_ATTR ISR + trampoline, lock-free atomics, 2 ms EMI lockout,
  GPIO35 as plain INPUT relying on the external 10 k (per CLAUDE.md §7).
- **UART plan is non-conflicting**: UART0 console (tuning build only), UART1 remapped to GPIO25
  for link2 (avoiding flash-adjacent defaults), UART2 CRSF at 420000 8N1 non-inverted.
- **D8 Phase 0 enumerates every pre-power electrical fix** (ESC red-wire isolation, divider,
  100 nF on GPIO34, Hall pull-up, WS2812 330R+1000µF+1N5819, servo-rail 1000µF, common ground)
  and gates ESC power behind proven failsafe.
- **WS2812 power budget validated at compile time** (~517 mA amber worst-case vs 900 mA budget);
  MAX98357A straps documented as non-driven with duplicated-stereo rationale.

## Findings (→ risk register)

| R## | Sev (verified) | Finding | Electronics angle |
|---|---|---|---|
| R20 | Low (ADJUSTED Med→Low) | WS2812 3.3 V data vs 5 V strip logic-high threshold | Verification: the build sheet **documents** "1N5819 diode *or* 74AHCT125" — a mitigated choice, not an oversight; the diode route is still marginal at current, prefer the shifter |
| R04 | High (merged from safety) | ESC (GPIO14) + steering (GPIO13) float high-Z through the boot window | This dimension rated it Low ("most ESCs treat absent PWM as disarmed-neutral"); the safety dimension raised it to High because both boards power together on the finished car — the register carries the higher rating |
| R08 | Med (ADJUSTED) | Atlas cannot serve as pin authority | Atlas has no GPIO numbers; PinMap+CLAUDE.md+BOM agree — only a physical continuity check remains |

**Carried Low (appendix of the register, not re-verified):**
- `maxPlausibleRpm=5000` ("~55 rev/s") and the Hall lockout comment ("≥18 ms at top speed")
  encode **two different top speeds** (83 vs 55 rev/s) — no functional break (the 2 ms lockout is
  far looser than the clamp), but reconcile to the measured top speed at bench (→ relates R18).
- WS2812 compile-time budget models only 2-channel amber as worst case; 3-channel white (~776 mA)
  is unmodeled — still under the 900 mA budget at 30 px, but the `valid()` proof doesn't bound it.
- ADC headroom: 8.4 V → 2.27 V leaves ~180 mV to the ~2.45 V ceiling; 1% divider tolerance plus a
  hot-off-charger pack can compress the top-of-charge reading (calibrate two-point at bench).

## Open questions

- Does the QuicRun treat a floating/absent PWM at boot as disarmed-neutral? (→ R04)
- Real minimum Hall edge spacing at full throttle — ~12 ms or ~18 ms? Tighten the lockout? (→ R18)
- Does the 1N5819 drop reliably clear the WS2812 threshold at real strip current, or is the
  74AHCT125 needed? (→ R20)
- Accept CLAUDE.md + BOM (which agree with PinMap) as the pin authority, given the atlas
  disclaims its pin numbers? (→ R08)

## Hardware validation hooks

See `11_hardware_validation_plan.md`: A2.1–A2.5 (continuity, BEC isolation, common ground,
level-shift, signal pull-downs — all **before power**), B1.3/B1.4 (scope PWM + boot-float
window), B4.1/B4.2 (ADC extremes, Hall baseline), C2/C3 (Hall EMI at load, battery two-point
calibration), CG-series for the ground side.
