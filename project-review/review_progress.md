# W17 Project Review — Progress Tracker

**Purpose:** recovery/state file after the review was interrupted by a session token
limit mid-way. It records exactly what was investigated, what was verified, what is
missing, and where duplication exists — so the review can resume without redoing work.

**Last updated:** 2026-07-03 (after audit workflow `w0m0miapi` completed investigation
but was cut off during verification).

---

## 0. Recovery assets

| asset | location | contents |
|---|---|---|
| Raw audit findings | `project-review/_raw_audit_findings.json` | Full structured output of the audit workflow: 8 completed dimension investigations, ~55 draft findings, well-designed notes, open questions, hardware-validation items. **UNVERIFIED.** |
| Safety findings | `project-review/_safety_findings.json` | Category-10 (safety) dimension, run separately after recovery. 8 findings (1H/3M/4L), 9 well-designed, 11 hw-validation items. Self-verified (agent re-read cited lines); NOT adversarially verified. |
| Build findings | `project-review/_build_findings.json` | Category-9 (build/deploy) dimension, run separately after recovery. 10 findings (2H/3M/5L after lead correction), 8 well-designed, 6 hw-validation items. **3 findings lead-verified** (see file's `_lead_verification`): critical→high (unpinned platform is latent not live — currently resolves to 7.0.1/core 2.0.17), high→medium (boards not currently divergent), serialport-rebuild CONFIRMED. |
| **Verification results** | `project-review/_verification_results.md` | **The authoritative risk backbone.** All High/Medium findings deduped + adversarially re-checked (lead, no agents). 22 stable `R##` entries with verdict (CONFIRMED/ADJUSTED/REFUTED/PLAUSIBLE) + post-verification severity. Post-verification High count = 4 (R01–R04). Lows carried as-authored in the JSON files. |

Everything below is derived from that file. To resume a dimension, read its slice of the
JSON rather than re-running agents.

---

## 1. What actually ran

- **Method:** 10 parallel dimension investigators + per-finding adversarial verifiers, over
  all 3 repos, read-only. Authoritative build/test baseline captured inline first.
- **Baseline (confirmed by lead, still valid):** control-fw 147/147 native tests + esp32dev/
  esp32dev_sim/esp32dev_tuning build; soundlight-fw 40/40 + esp32dev/esp32dev_sim build;
  ground-station 21/21 vitest. Flash/RAM low (~22% / ~7%).
- **Interruption:** session token limit hit during the **verify** phase. Result:
  - Investigation complete for **8/10** dimensions.
  - **`safety` and `build` investigations did NOT run** (agents failed).
  - **Verification: 0/~55 findings verified** — the adversarial-refute pass never landed.
- **Post-recovery progress:**
  - **`safety` investigation COMPLETE** (2026-07-03, separate agent) → `_safety_findings.json`.
  - **`build` investigation COMPLETE** (2026-07-03, separate agent) → `_build_findings.json`,
    with 3 findings lead-verified/severity-corrected inline.
  - **Investigation is now 10/10 complete.**
  - **Verification pass COMPLETE** (2026-07-03, lead inline) → `_verification_results.md`.
    All High/Medium findings deduped + adversarially re-checked. 1 refuted mechanism, 1
    Critical→High, several down-severitied; post-verification High count = 4 (R01–R04).
  - **Remaining: author the report files** (00–12 + open_questions) from the verified backbone.

---

## 2. Output-file status (the 14 target report files)

| # | file | status | notes |
|---|---|---|---|
| 00 | `00_review_summary.md` | **not started** | write last, after verification |
| 01 | `01_spec_vs_implementation.md` | **partial (data only)** | investigation done; findings unverified |
| 02 | `02_architecture_review.md` | **partial (data only)** | investigation done; findings unverified |
| 03 | `03_hardware_electronics_review.md` | **partial (data only)** | investigation done; findings unverified |
| 04 | `04_control_firmware_review.md` | **partial (data only)** | investigation done; findings unverified |
| 05 | `05_soundlight_firmware_review.md` | **partial (data only)** | investigation done; findings unverified |
| 06 | `06_ground_station_review.md` | **partial (data only)** | investigation done; findings unverified |
| 07 | `07_protocol_review.md` | **partial (data only)** | investigation done; findings unverified |
| 08 | `08_simulation_test_review.md` | **partial (data only)** | investigation done; findings unverified |
| 09 | `09_build_deploy_review.md` | **partial (data only)** | investigation done → `_build_findings.json`; 3 findings lead-verified |
| 10 | `10_risk_register.md` | **✅ COMPLETE** | Written 2026-07-03 from the verified R## backbone. 22 R## entries (4 High, 13 Med, 5 Low) with all required fields + HW-required flags + a carried-lows appendix. |
| 11 | `11_hardware_validation_plan.md` | **not started** | ~63 hw-validation items already collected across the JSONs + the R## "how to verify" lines to seed it |
| 12 | `12_recommended_fixes.md` | **not started** | DO NOT write until specific fixes are approved by the owner |
| — | `open_questions.md` | **✅ COMPLETE** | Written 2026-07-03. ~42 questions grouped (owner-decisions / protocol / hardware / firmware / sim-CI), HW/OWNER/RESOLVED tagged, linked to R## ids. |

"partial (data only)" = structured findings exist in `_raw_audit_findings.json`; the
human-readable `.md` file has not been authored, and the findings are **not yet verified**.

---

## 3. Review-category status

Legend — Investigation: done / **failed** / n/a. Verification: **none** (nothing verified yet).

| category | investigation | verification | # findings (H/M/L) | notes |
|---|---|---|---|---|
| 1. Spec compliance | done | none | 8 (1/3/4) | HUD reads armed/failsafe the car never sends; gear/mode label drift |
| 2. Architecture | done | none | 6 (2/2/2) | link2 duplication no CI guard; CRSF C++↔JS drift claim |
| 3. Hardware/electronics | done | none | 5 (0/1/4) | WS2812 level-shift; boot-window pin float; ADC headroom |
| 4. Control firmware | done | none | 8 (2/2/4) | LEDC PWM never scoped; ESC boot-arm timing a guess; Hall/WheelSpeed edge cases |
| 5. Soundlight firmware | done | none | 6 (0/1/5) | dead-board-1 stays NeverConnected (no escalation); i2s return codes ignored |
| 6. Ground station | done | none | 9 (1/4/4) | LINK LOST can't fire from real telem; sim mistakable for real; video path unproven |
| 7. Protocol | done | none | 6 (1/4/1) | golden-vector-sharing claim; driveMode/gear drift; link2 no sync-check |
| 8. Simulation/test | done | none | 7 (2/3/2) | soundlight has NO Wokwi sim; all *_hal_esp32 untested; main.cpp orchestration untested |
| 9. Build/deploy | **done** | 3 lead-verified | 10 (2/3/5) | `_build_findings.json`; unpinned platform+LEDC (latent High), serialport rebuild not wired (High, CONFIRMED), CI never packages/tuning-builds |
| 10. Runtime safety & RC risk | **done** | self-only | 8 (1/3/4) | `_safety_findings.json`; failsafe/arm/boot-arm strong; residual risks are HW-config (ESC pin float, arm-hold guess, steering endpoints, brownout) |

Totals: **10 dimensions, ~73 raw findings** (High ≈ 12, Medium ≈ 26, Low ≈ 35),
plus ~74 well-designed notes, ~42 open questions, ~63 hardware-validation items.
**All 10 investigations COMPLETE; verification pass COMPLETE** — High/Medium deduped +
adversarially re-checked into 22 `R##` entries (`_verification_results.md`); post-verification
**High = 4** (R01 armed/failsafe demo-only, R02 unpinned+LEDC latent, R03 serialport ABI,
R04 ESC pin float). Lows carried as-authored, not individually re-verified.

---

## 4. Evidence already checked (per category)

- **Spec:** all 3 CLAUDE.md, ROADMAP.md, D8_BENCH_BRINGUP.md, build sheet, BOM, print spec,
  SIMULATION.md, ground docs; both `src/main.cpp` skimmed for wired-vs-documented.
- **Architecture:** lib/ layouts + library.json across repos; shared/ in ground station;
  confirmed `lib/link2` + `link2_protocol.md` byte-identical across firmware repos (lead-verified).
- **Hardware:** both `PinMap.hpp`, the wiring atlas HTML, build sheet, BOM, the esp32 HALs.
- **Control fw:** `src/main.cpp` + crsf/channels/gearbox/failsafe/ers/outputs/telemetry/
  settings/console + esp32 HALs (Hall ISR, LEDC, ADC).
- **Soundlight fw:** `src/main.cpp` + enginesim/soundsynth/lights/link2/link2monitor + audio/
  lights HALs; cross-core atomic design.
- **Ground station:** main.js, CrsfSerialSource.js, mediamtx.js, hud.js, whep.js, shared/*.js,
  scripts, package.json, electron-builder.yml, docs.
- **Protocol:** lib/crsf (+ new GPS 0x02 / FLIGHTMODE 0x21 builders), link2 both repos,
  ground shared/crsf.js + crsfTelemetry.js + tests.
- **Sim/test:** all native test suites (3 repos), wokwi.toml/diagram.json, both sim feeders.

## 5. What still needs to be checked

1. ~~Safety dimension (category 10)~~ — **DONE 2026-07-03** → `_safety_findings.json`.
   Verdict: architecture strong; residual risks are hardware-config truths (ESC pin float at
   boot, 2s arm-hold guess, steering endpoints vs linkage, brownout coast, forward/brake mode).
2. ~~Build/deploy dimension (category 9)~~ — **DONE 2026-07-03** → `_build_findings.json`.
   Verdict: unpinned platform+LEDC is a latent (not live) reproducibility hazard; serialport
   rebuild is not wired so the packaged .exe likely runs telemetry-disabled; CI never packages
   the app nor builds esp32dev_tuning.
3. ~~Verification pass~~ — **DONE 2026-07-03** → `_verification_results.md` (22 R## entries,
   High/Medium deduped + adversarially re-checked). Lows carried as-authored.

## 6. Known duplication / cross-dimension overlap (dedupe before the risk register)

Same root issue raised by multiple investigators — collapse into one register entry each:

- **armed/failsafe not transmitted → HUD "LINK LOST" can never fire from the real car**:
  spec(H), ground-station(H), ground-station(L README). ← likely the top functional finding.
- **driveMode label mismatch (Gearbox vs RACE)**: spec(L), architecture(M), ground(M), protocol(M).
- **gear-count 4 (fw) / 6 (link2 doc) / 8 (HUD)**: spec(M), architecture(M), ground(M), protocol(M).
- **link2 duplicated with no CI drift-guard**: architecture(H), protocol(M), sim/test(M).
- **CRSF decoder C++↔JS, "shared golden vectors" claim**: architecture(H), protocol(H).
  ⚠️ **SUSPECT** — needs verification; the ground tests may in fact reconstruct the firmware
  byte layouts (arguably "shared" in spirit). Do not report as-is until checked.
- **platform espressif32 unpinned (control) vs pinned (soundlight)**: architecture(M); also a
  build-dimension item (not yet investigated).
- **all *_hal_esp32 untested/unsimulated**: sim/test(H); overlaps ctrl-fw & soundlight-fw hw items.

## 7. Recommended next step

Order that maximizes value under the token budget:

1. ~~Run the missing `safety` investigation~~ — **DONE**.
2. ~~Run the missing `build` investigation~~ — **DONE**.
3. ~~Run the verification pass~~ — **DONE** → `_verification_results.md`.
4. ~~Author `10_risk_register.md`~~ — **DONE**.
5. ~~Author `open_questions.md`~~ — **DONE**.

## ► REMAINING TO WRITE (after usage limit resets)

All investigation + verification is finished; the data to write every remaining file already
exists in the recovery assets — **no more agents/analysis needed, only authoring.** Sources per
file are noted so a fresh session can resume cold.

| file | status | write it from |
|---|---|---|
| `11_hardware_validation_plan.md` | **TODO — highest value next** | the `hw_validation_items` arrays in `_safety_findings.json` (11) + `_build_findings.json` (6) + `_raw_audit_findings.json` (~46 across 8 dims), plus the "how to verify" + `when` (before_power / first_power / later) fields. Organize as an ordered bench runbook keyed to the D8 phases; cross-reference R## ids. Include the "top-10 to verify before power" and "top-10 to test when parts arrive" lists the original brief asked for. |
| `01_spec_vs_implementation.md` | TODO | dimension "Specification compliance" in `_raw_audit_findings.json` (summary + well_designed + findings). |
| `02_architecture_review.md` | TODO | dimension "Repository-level architecture" in `_raw_audit_findings.json`. |
| `03_hardware_electronics_review.md` | TODO | dimension "Hardware / electronics" in `_raw_audit_findings.json`. |
| `04_control_firmware_review.md` | TODO | dimension "Control firmware" in `_raw_audit_findings.json`. |
| `05_soundlight_firmware_review.md` | TODO | dimension "Sound/light firmware" in `_raw_audit_findings.json`. |
| `06_ground_station_review.md` | TODO | dimension "Ground station" in `_raw_audit_findings.json`. |
| `07_protocol_review.md` | TODO | dimension "All communication protocols" in `_raw_audit_findings.json`. |
| `08_simulation_test_review.md` | TODO | dimension "Simulation and automated-test coverage" in `_raw_audit_findings.json`. |
| `09_build_deploy_review.md` | TODO | `_build_findings.json` (whole file, incl. `_lead_verification`). |
| `00_review_summary.md` | TODO — **write LAST** | synthesize from `10_risk_register.md` + `_verification_results.md`: exec summary, the 4 Highs, bring-up-readiness verdict, "what to check before powering anything," "top-10 before hardware / top-10 first-power," unknown-unknowns. |
| `12_recommended_fixes.md` | **BLOCKED** | do NOT write until the owner approves specific fixes. Candidate now-fixes (software, no hardware): R02 pin platform, R03 wire serialport rebuild, R17 add CI steps, R05/R19 reconcile gear-count/labels, R06 add link2 drift-guard, R01 decide + implement armed/failsafe display. |

**Per-dimension files (01–09):** each should carry that dimension's `summary`, `well_designed`
list, and its findings — but reference the deduped `R##` id from `10_risk_register.md` instead of
re-stating full finding bodies, to avoid divergence. Keep `_verification_results.md` as the single
source of truth for severity/verdict.

**Nothing above requires re-reading source or re-running agents** — it is transcription +
organization from the four `_*.json/_*.md` recovery assets in this folder.
4. **Author `10_risk_register.md`** (the key deliverable) from verified, deduped findings.
5. **Author `11_hardware_validation_plan.md` + `open_questions.md`** (seed data already collected).
6. **Author the per-dimension files 01–09**, then **`00_review_summary.md`** last.
7. `12_recommended_fixes.md` only after you approve specific fixes.

**Do NOT** re-run the 8 completed investigations — their data is in `_raw_audit_findings.json`.
