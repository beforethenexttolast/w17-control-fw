# Fix-Approval Triage

Input for the owner's approval pass. Every register item (`10_risk_register.md`, R01–R22) that
might require action is classified below. **Nothing here is implemented** — `12_recommended_fixes.md`
is written only after the owner approves specific items.

Categories: **APPROVE_NOW_SOFTWARE** · **OWNER_DECISION_REQUIRED** · **HARDWARE_VALIDATION_ONLY**
· **DEFER** · **REJECT_FALSE_POSITIVE_OR_ACCEPTED_TRADEOFF**

Note on "learning manual": no `learning-manual/` directory exists in any of the three repos
(verified during recon). The flag below means *your external learning manual + the repo docs*
that describe the affected behavior.

---

## High risks

### R01 — HUD link-loss / armed / failsafe policy
- **Category:** OWNER_DECISION_REQUIRED (then a small software fix)
- **Why:** The car never transmits `armed`/`failsafe`; "LINK LOST" fires only in demo. Two valid
  policies exist and only you can pick: (A) derive link-loss ground-side, (B) transmit real flags.
- **Decision you must make:** Pick one —
  **(A) Recommended:** drive "LINK LOST" from `linkQualityPct == 0` + telemetry staleness (data
  the real path already has; no firmware change), and drop `armed`/`failsafe` from the
  TELEMETRY.md contract/README;
  **(B)** extend the FLIGHTMODE string (e.g. `"G3 M2 E55 F1 A1"`) to carry real armed/failsafe,
  parse ground-side — firmware + ground change, more truthful, slightly more to verify at bench;
  **(C)** accept sim-on-loss and only fix the docs.
- **Minimal fix if (A):** hud.js LINK-LOST branch keys off LQ+staleness; TELEMETRY.md/README wording.
- **Files:** `w17-ground-station/renderer/hud.js`, `docs/TELEMETRY.md`, `README.md`; plus
  `w17-control-fw/src/main.cpp` + `lib/crsf/CrsfFrameBuilder.hpp` + `shared/crsf.js` if (B).
- **Tests after:** ground `npm test`; if (B) also control `pio test -e native` + all ESP32 builds.
- **Learning manual:** yes (what the HUD shows on link loss).
- **Hardware still required:** yes — CG5 (deliberate link drop, observe HUD) regardless of choice.

### R02 — Pin `espressif32` in control-fw
- **Category:** APPROVE_NOW_SOFTWARE
- **Why:** Latent build break (core-3.x LEDC API removal); one line removes it; zero behavior change
  (currently resolves to 7.0.1 anyway — lead-verified).
- **Decision:** none (approve/reject).
- **Minimal fix:** `platform = espressif32 @ ~7.0.1` in `w17-control-fw/platformio.ini` (matches
  soundlight; provides both the channel LEDC API and, for board #2, the legacy I2S driver).
- **Files:** `w17-control-fw/platformio.ini` (1 line).
- **Tests after:** `pio test -e native`; `pio run` (esp32dev + sim + tuning). Plan item: one
  clean-cache build on a fresh machine before gift day.
- **Learning manual:** no. **Hardware still required:** no.

### R03 — Wire the serialport Electron-ABI rebuild
- **Category:** APPROVE_NOW_SOFTWARE
- **Why:** Confirmed dead wiring (`app:rebuild` referenced but nonexistent); the packaged `.exe`
  silently ships telemetry-disabled. Cheap, no behavior change for dev runs.
- **Decision:** none.
- **Minimal fix:** add `"app:rebuild": "electron-rebuild -f -w serialport"` to package.json and
  chain it: `"build": "npm run app:rebuild && electron-builder --win"` (or a beforeBuild hook);
  align the electron-builder.yml comment.
- **Files:** `w17-ground-station/package.json`, `electron-builder.yml` (comment).
- **Tests after:** `npm test`; full proof is Windows-side (CG2: packaged app logs "CRSF serial open").
- **Learning manual:** no. **Hardware still required:** partially — final proof on the target
  Windows machine (plan CG2), but the fix itself is verifiable by inspecting the packaged ABI.

### R04 — ESC signal pin (GPIO14) floats through the boot window
- **Category:** HARDWARE_VALIDATION_ONLY (with one optional micro-mitigation you may approve now)
- **Why:** Whether the powered QuicRun twitches on a floating line is only knowable with a scope
  (plan B1.4); the definitive fix (signal pull-down/RC) is a hardware addition.
- **Decision (optional):** approve the software micro-mitigation — move `escPwm.begin()` (and
  `steeringPwm.begin()`) to the first lines of `setup()` to shrink the float window by the four
  preceding `begin()` calls. Harmless, small, but hardware-unverifiable today.
- **Files if approved:** `w17-control-fw/src/main.cpp` (reorder in setup()).
- **Tests after:** `pio test -e native`; all ESP32 builds.
- **Learning manual:** no (bench note only). **Hardware still required:** yes — B1.4 scope +
  decide on the pull-down at A2.5.

---

## Medium risks

### R05 — Canonical gear count (fw 4 / link2 doc 6 / HUD 8)
- **Category:** OWNER_DECISION_REQUIRED (then a small software/doc fix)
- **Why:** Only you can pick the shipped feel. The firmware is the physical authority (4), but you
  may *want* more display gears.
- **Decision you must make:** the canonical count. **Recommended: 4 everywhere** (match the car;
  honest mirror). Alternative: raise firmware `numGears` (gearbox supports it) if you want 6/8 —
  that changes drive feel and needs bench re-tuning.
- **Minimal fix if "4":** `GEARS: 4` in `shared/feelConstants.js` (HUD adapts via FEEL); fix
  `link2_protocol.md` gear range to 1..4 **in both repos** (keep the copies identical); check
  demo timeline gears ≤4.
- **Files:** `w17-ground-station/shared/feelConstants.js`, `shared/replaySource.js` (demo gears),
  `w17-control-fw/docs/link2_protocol.md` + `w17-soundlight-fw/docs/link2_protocol.md`.
- **Tests after:** ground `npm test`; control + soundlight `pio test -e native` (doc-only there).
- **Learning manual:** yes (gear behavior). **Hardware still required:** later sanity check CG6.

### R06 — link2 duplication drift strategy
- **Category:** OWNER_DECISION_REQUIRED (then software)
- **Why:** Two valid strategies with different maintenance costs; the copies are identical today.
- **Decision you must make:** **(A) Recommended:** a CI drift-guard — a job step in each firmware
  repo that fetches the sibling repo (or a checked-in hash manifest) and fails if
  `lib/link2/{Link2Frame.hpp,Link2Codec.hpp,Link2Codec.cpp}` + `docs/link2_protocol.md` differ.
  Cheap, keeps repos independent. **(B)** promote link2 to a git submodule/shared package —
  cleaner long-term, more workflow friction before a deadline.
- **Files if (A):** both repos' `.github/workflows/ci.yml` (+ optionally a small hash manifest).
- **Tests after:** CI run on both repos (green today since copies are identical).
- **Learning manual:** no. **Hardware still required:** B3.3/B3.4 remain the on-wire proof.

### R07 — C++↔JS CRSF agreement (overstated golden-vector claim)
- **Category:** APPROVE_NOW_SOFTWARE
- **Why:** Byte-for-byte agreement was human-verified today, but nothing machine-enforces it and a
  comment claims otherwise. Cheap to make honest + durable.
- **Decision:** none (approve/reject scope: comment-only vs comment+fixture — **recommend both**).
- **Minimal fix:** (1) correct the `shared/crsf.js` header comment; (2) commit a small shared
  golden-fixture file (hex frames for battery/GPS/flightmode/link-stats, values copied from the
  firmware tests) and load it in `crsf.test.js`/`crsfTelemetry.test.js`; optionally mirror the
  same hex strings in a firmware test comment for traceability.
- **Files:** `w17-ground-station/shared/crsf.js` (comment), `test/fixtures/crsf_golden.json`
  (new), `test/crsf.test.js`, `test/crsfTelemetry.test.js`.
- **Tests after:** ground `npm test`.
- **Learning manual:** no. **Hardware still required:** CG4 remains the live-frame proof.

### R08 — Pin continuity vs the soldered board — **HARDWARE_VALIDATION_ONLY**
  Docs are internally consistent (atlas is illustrative by design). Bench A2.1. *(Optional
  approve-now doc line: note in PinMap.hpp/CLAUDE.md that PinMap+BOM are the authority.)*
  Learning manual: no. Hardware: yes.

### R09 — ESC arm-hold + forward/brake mode — **HARDWARE_VALIDATION_ONLY**
  The values are ESC truths (manual/bench, B2.3); `bootArmHoldMs` is already tunable in the
  tuning build. No code change until measured. Learning manual: afterwards (arming procedure).
  Hardware: yes.

### R10 — Steering endpoints vs linkage travel — **HARDWARE_VALIDATION_ONLY**
  Correct endpoint values are only knowable with the linkage fitted (B3.1). *(Optional software
  convenience you may approve now: expose steering min/max in the tuning console so bench
  narrowing doesn't need reflash cycles — touches `lib/settings`, `lib/console`, tests; medium
  effort. If declined, bench uses edit+reflash.)* Learning manual: afterwards. Hardware: yes.

### R11 — HAL layer unproven; no soundlight Wokwi — **HARDWARE_VALIDATION_ONLY** (sim: DEFER)
  The HALs close only on the bench (B3.5–B3.8, B4). Building a soundlight Wokwi sim is real
  effort for modest return this close to hardware arrival → **DEFER** unless parts slip.
  Learning manual: no. Hardware: yes.

### R12 — Brownout/reset coast behavior — **HARDWARE_VALIDATION_ONLY** (C1). Hardware: yes.

### R13 — ELRS relay of 0xC8 telemetry frames — **HARDWARE_VALIDATION_ONLY** (CG3 — the single
  most important telemetry bench question; if it fails, the fix is an addressing change, decided
  then). Learning manual: no. Hardware: yes.

### R14 — Exclusive FT232 COM port — **HARDWARE_VALIDATION_ONLY** (CG2; tooling question, not
  code). Hardware: yes.

### R15 — Camera codec / video path — **HARDWARE_VALIDATION_ONLY** (CG1). Hardware: yes.

### R16 — main.cpp orchestration untested; sim not asserted in CI
- **Category:** DEFER (with one carve-out already in Phase A)
- **Why:** Writing orchestration tests / CI sim assertions is real effort; the cheap 80% is simply
  **running the Wokwi sim once to DRIVING** — already plan item **A1.6, do it now, no approval
  needed**. The named gap tests (NVS-on-flash, ADC rails…) largely need hardware anyway.
- **Learning manual:** no. **Hardware still required:** partially (CR1, B4.1).

### R17 — CI coverage gaps
- **Category:** APPROVE_NOW_SOFTWARE
- **Why:** Two cheap CI additions close real blind spots: the bench firmware (`esp32dev_tuning`)
  can rot; the deliverable `.exe` is never packaged.
- **Decision:** none.
- **Minimal fix:** (1) add `pio run -e esp32dev_tuning` to control-fw CI; (2) add a ground-station
  packaging smoke job (windows-latest: `npm run app:rebuild && electron-builder --dir`) — pairs
  with R03.
- **Files:** `w17-control-fw/.github/workflows/ci.yml`, `w17-ground-station/.github/workflows/ci.yml`.
- **Tests after:** the CI runs themselves (push → green).
- **Learning manual:** no. **Hardware still required:** no.

---

## Low risks

### R19 — Drive-mode label set (Gearbox vs RACE)
- **Category:** OWNER_DECISION_REQUIRED (then docs-only)
- **Decision you must make:** which labels ship. **Recommended:** keep the HUD's user-facing
  TRAINING/RACE/ERS (the recipient reads the HUD) and align the protocol doc + code comments.
- **Minimal fix:** wording in `link2_protocol.md` (both repos), `Link2Frame.hpp` comment,
  `ChannelDecoder.hpp` comment, `docs/TELEMETRY.md`. No wire change, no logic change.
- **Tests after:** both firmware `pio test -e native` (comments only), ground `npm test`.
- **Learning manual:** yes (mode names). **Hardware still required:** no (CG6 sanity later).

### R18 — Hall EMI — **HARDWARE_VALIDATION_ONLY** (C2; telemetry-only path). Hardware: yes.
### R20 — WS2812 level shift — **HARDWARE_VALIDATION_ONLY** (A2.4/B-scope; BOM choice: prefer the
  already-documented 74AHCT125). Hardware: yes.
### R21 — WheelSpeed decay "collapse" — **REJECT_FALSE_POSITIVE_OR_ACCEPTED_TRADEOFF**
  (mechanism refuted on re-check; benign sub-40 rpm telemetry floor accepted). No action.
### R22 — Board #2 "breathe forever" when board #1 never connects —
  **REJECT_FALSE_POSITIVE_OR_ACCEPTED_TRADEOFF** (intentional, benign; the dangerous case —
  cut wire mid-run — correctly escalates). Optional escalation animation = DEFER.

### Carried-Low hygiene bundle (register appendix) — **DEFER by default**, approve as a bundle if wanted
Cheap, zero-risk cleanups you can approve as one batch or skip entirely:
stale D8 "gimbal decoded, unwired" line (worth fixing — it contradicts Phase 7b); soundlight
`library.json` stale sender description; `crsf.js`/`feelConstants.js` stale source-path comments
(partly covered by R07); NeoPixel pin tightening (`^1.12.0` → `~1.12.0`); ground `engines` field
+ `.nvmrc`; remove/wire the inert `allowScripts` key; a short control-fw README flash section
with a "gift ships plain esp32dev" warning (this last one has real bench-day value).
- **Files:** docs + package.json + platformio.ini touches only. **Tests:** existing suites.
- **Learning manual:** only the README/flash item. **Hardware:** no.

---

## 1) Fixes to approve immediately (recommended)

1. **R02** — pin `espressif32 @ ~7.0.1` (1 line; removes the deadline time-bomb).
2. **R03** — wire `app:rebuild` into the ground build (the `.exe` otherwise ships broken).
3. **R17** — two CI additions (tuning build + packaging smoke; pairs with R03).
4. **R07** — honest comment + shared golden fixture (makes the C++↔JS agreement durable).
5. *(Optional, low risk)* **R04 micro-mitigation** — `escPwm.begin()` first in setup().
6. *(Optional bundle)* the carried-Low hygiene batch — at minimum the stale D8 gimbal line and
   the control-fw README flash section.

## 2) Decisions you must make as owner

1. **R01** — HUD link-loss policy: (A) derive from LQ+staleness (recommended), (B) transmit real
   armed/failsafe in FLIGHTMODE, or (C) docs-only.
2. **R05** — canonical gear count (recommended: 4 everywhere, matching the car).
3. **R19** — drive-mode label set (recommended: HUD's TRAINING/RACE/ERS, align docs to it).
4. **R06** — link2 drift strategy: CI drift-guard (recommended) vs submodule.
5. **R10 (optional)** — spend effort making steering endpoints console-tunable before the bench,
   or accept edit+reflash cycles during Phase B.

## 3) Stays in the hardware validation plan only

**R04** (scope the boot float; pull-down decision) · **R08** (pin continuity) · **R09** (ESC
arm/mode) · **R10** (endpoint values) · **R11** (HAL/I2S/WS2812 bring-up) · **R12** (brownout
coast) · **R13** (ELRS relay — key telemetry gate) · **R14** (COM port) · **R15** (camera codec)
· **R18** (Hall EMI) · **R20** (WS2812 level) — all mapped to Phases A/B/C in
`11_hardware_validation_plan.md`. No action now; no code until measured.
