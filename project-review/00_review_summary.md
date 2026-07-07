# 00 — Review Summary (W17 RC Project — Independent Skeptical Audit)

**Scope:** `w17-control-fw` (ESP32 #1 control), `w17-soundlight-fw` (ESP32 #2 sound/light),
`w17-ground-station` (Electron HUD) — reviewed pre-hardware, treating all code as guilty until
proven correct, independent of who wrote it.

**Method:** 10 dimension investigations (spec, architecture, hardware, both firmwares, ground
station, protocols, simulation/test, build/deploy, runtime safety) → cross-dimension dedupe →
adversarial re-check of every High/Medium finding against the actual code. Full detail:
`10_risk_register.md` (the key deliverable, 22 `R##` entries), `01–09` per-dimension files,
`11_hardware_validation_plan.md`, `open_questions.md`.

**Baseline (confirmed):** control-fw 147/147 native tests + all 3 ESP32 envs build;
soundlight-fw 40/40 + builds; ground-station 21/21 vitest. Flash/RAM ~22% / ~7%.

---

## Executive verdict

**The project is in genuinely good shape for its stage — better than the raw finding count
suggests — and is READY for first hardware bring-up once four cheap pre-power items are done.**

The safety-critical core (failsafe → arm gate → ESC boot-arm → output clamps) was traced
end-to-end by two independent passes and **holds**: on link loss or at boot, throttle is
forced neutral before any feature code runs, re-arming requires a deliberate fresh-neutral, and
no path (ERS boost, gimbal, telemetry, console) can bypass it. The pure-logic test suites are
strong and test real production code. The documentation (ROADMAP defect trail, D8 bench
runbook, protocol spec) is honest and unusually thorough.

The audit's caveat is equally clear: **everything green is software-green.** Not one line has
run on real silicon. The remaining risk lives almost entirely in (a) hardware truths no code
can enforce (ESC behavior, ELRS telemetry relay, camera codec, EMI), and (b) a handful of
reproducibility/packaging gaps that are trivial to fix now and expensive to discover later.

## No Critical risks survived verification

The initial investigation produced one "Critical" (an unpinned-platform build failure) and
several inflated Highs. Adversarial re-checking **refuted or downgraded them**: the unpinned
platform currently resolves to the same 7.0.1/core 2.0.17 as the pinned repo (latent, not
live); the WheelSpeed "collapse" mechanism was refuted outright; the "LEDC at its limit" claim
was mostly refuted (16-bit sits well under the ~20.6-bit ceiling at 50 Hz). **Nothing in the
final register can damage hardware, cause uncontrolled motion, or block basic operation on its
own.**

## The four High risks (R01–R04) and what they mean

| ID | Risk | What it actually means | Class |
|---|---|---|---|
| **R01** | HUD reads `armed`/`failsafe` the car never transmits; "LINK LOST" fires only in demo | The HUD's most safety-relevant *indicator* is dead on the real path — a genuine link loss shows a live-looking simulated HUD. **Not** a vehicle-safety defect (viewer-only app; the car's own failsafe is independent and sound) — an expectations/display gap | Owner decision + small software fix |
| **R02** | control-fw platform unpinned while its LEDC HAL uses the core-2.x-only channel API | Builds today by luck of resolution (7.0.1). A future espressif32 release shipping core 3.x breaks the gift build on any fresh checkout — potentially days before the deadline. One-line pin removes it | Software fix, now |
| **R03** | Packaged `.exe` never rebuilds serialport for Electron's ABI (`app:rebuild` doesn't exist) | The shippable Windows app would silently run telemetry-disabled — the graceful degrade masks the packaging bug until gift day | Software fix, now |
| **R04** | ESC signal pin (GPIO14) floats high-Z from reset until `escPwm.begin()`; no pull-down | The one High that touches the vehicle: a powered ESC sees a floating line for tens of ms each boot. Mitigated by ESC power-on-neutral arming (likely) and D8 discipline; provable only with a scope | Hardware validation (+ optional pull-down) |

## Strongest parts of the design (evidenced, not flattery)

- **The safety chain**: failsafe applied first and unconditionally every tick; `everReceivedFrame_`
  latch kills the boot-full-lock class; ArmGate fresh-neutral after every episode; `applyBoost(0)==0`
  proven; battery strictly warn-only with no auto-cut path; board #2 has an independent dead-man
  and 500 ms staleness failsafe.
- **The HAL seam is real**: zero Arduino includes in pure logic (grep-verified), triple-guarded
  native env — the 208 native tests genuinely test the shipped logic.
- **Wire protocols**: correct CRC-8/DVB-S2 pinned by a catalog KAT in all three repos; assemblers
  that hard-reject bad lengths and resync; byte order verified on both ends of every path; sims
  feed the real parser, not a shortcut.
- **Honest docs**: ROADMAP records its own past defects with file:line; D8 gates ESC power behind
  proven failsafe; TELEMETRY.md/SETUP.md flag their own biggest risks.
- **Deliberate blast-radius control**: viewer-only ground station; gift firmware compile-stripped
  of console/sim surfaces.

## Highest-risk assumptions (the unknown-unknowns)

1. **ELRS relays the `0xC8`-addressed battery/GPS/FLIGHTMODE frames** (R13) — if not, the whole
   real-telemetry feature silently produces nothing; may need extended addressing.
2. **The QuicRun ESC's actual behavior** (R09, R12): 2 s arm-hold, forward/brake mode,
   neutral-on-signal-loss — all assumed, none from the manual.
3. **The camera emits H.264** (R15) — gates all WebRTC video.
4. **The FT232 COM port can be shared/forwarded** (R14) — gates the telemetry reader entirely.
5. **Shared-rail power integrity** (WS2812 inrush / servo stall spikes vs brownout, R12) and
   **Hall-line EMI** (R18).
6. **The Wokwi sim has never actually been run to a live link** — Stage-2 confidence currently
   rests on an unexecuted checklist (plan A1.6; cheapest win available).

## What must be done BEFORE first power (Phase A)

Desk/software + multimeter — details in `11_hardware_validation_plan.md`:
1. Pin `espressif32 @ 7.0.1` in control-fw (R02).
2. Wire the serialport `app:rebuild` into the ground-station build (R03).
3. Continuity-check every PinMap signal against the soldered board (R08).
4. Confirm ESC BEC red-wire isolation + single common ground (A2.2/A2.3).
5. Run the Wokwi sim once to DRIVING/failsafe=0 (A1.6).
6. Decide gear count + drive-mode labels (R05/R19) and the R01 armed/failsafe question.
7. Add the two missing CI steps (R17).

## What must be done BEFORE motor/ESC power (Phase B)

Logic-only bench first — **the golden rule stands: no ESC motor power until B2 passes**:
- CRSF link up at 420000 non-inverted; LQ=0 latches failsafe (B1.1/B1.2).
- Scope PWM pins before connecting actuators; scope the boot-float window (B1.3/B1.4, R04).
- Prove the full safety chain live: no-CRSF-safe, arm-into-throttle blocked, fresh-neutral
  rearm (B2.1–B2.4).
- ESC arms with the 2 s hold; forward/brake mode confirmed (R09).
- Steering endpoints narrowed to the linkage before full-stick sweeps (R10).

## What can wait

- All Phase-C items: Hall EMI at load, battery calibration, brownout coast test, NVS-corruption
  test, audio underrun soak (R12/R18/CR-series).
- The ground-station video + telemetry-path work (CG-series) — the VLC + elrs-jc fallback means
  gift day survives even total ground-station failure.
- All ~30 carried Low findings (register appendix): doc staleness, label cleanups, diagnostics,
  dead rpm field, volume-curve test, etc.

## Decision classes

- **Owner decisions (no code until decided):** R01 display policy (real armed/failsafe vs
  documented sim-on-loss); canonical gear count (R05); drive-mode label set (R19); link2
  copy-vs-submodule strategy (R06). See `open_questions.md` [OWNER] items.
- **Software fixes, doable now:** R02 pin, R03 rebuild, R17 CI, R06 drift guard, R07 shared
  fixture (or honest comment), plus approved lows.
- **Hardware validation, cannot close earlier:** R04, R08–R15 (bench items), R18, R20 — all
  mapped to Phase A/B/C in `11_hardware_validation_plan.md`.

## Recommended next step

**Fix-approval triage, not automatic fixes.** The audit deliberately changed nothing. Next
session: walk the owner through the [OWNER] decisions and the now-fixable list (R02, R03, R17,
R05/R19, R06, R01), approve a specific subset, and only then write `12_recommended_fixes.md`
and implement — with the usual build/test/commit discipline. Everything else waits for parts,
following the phased plan.
