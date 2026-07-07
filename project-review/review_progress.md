# W17 Project Review — Progress Tracker

**Purpose:** recovery/state file after the review was interrupted by a session token
limit mid-way. It records exactly what was investigated, what was verified, what is
missing, and where duplication exists — so the review can resume without redoing work.

**Last updated:** 2026-07-07 — **AUDIT COMPLETE + BATCH F1 IMPLEMENTED (uncommitted).**

### Implementation status
- **F3 — DONE, awaiting owner review/commit (not committed).** Spans all three repos.
  **Ground-station:** NEW `test/fixtures/crsf_golden.json` (battery 7.9V/72%, GPS 36.1 km/h,
  FLIGHTMODE "G3 M2 E55", LINK_STATISTICS — full on-wire hex + expected decodes), loaded by
  `test/crsf.test.js` (+5 cases) and `test/crsfTelemetry.test.js` (+4 e2e cases); `shared/crsf.js`
  header comment corrected (parallel reimpl pinned by shared fixture, not "reused"). (R07)
  **control-fw:** `test/test_crsf/test_main.cpp` traceability comment → fixture (comment only);
  `.github/workflows/ci.yml` NEW `link2-drift` job (clones w17-soundlight-fw, diffs the 4 link2
  contract files). **soundlight-fw:** `.github/workflows/ci.yml` mirror `link2-drift` job (clones
  w17-control-fw). (R06 — CI-clone approach; both repos verified PUBLIC via `gh`, so no
  private-permission blocker and no manifest fallback needed.) **Validation:** ground 39/39
  vitest (was 30; +9 fixture cases), control 147/147 native, soundlight 40/40 native; local
  drift-guard dry-run confirms the 4 files IDENTICAL across repos → guard passes. No source
  behavior changed (tests/comments/CI only). No F4 gear/label edits. CG4 (live-frame decode)
  still a hardware validation item.
- **F2 — COMMITTED** as ground-station `b880be6` (owner-approved).
- **F2 (pre-commit note kept for record):** Ground-station only. Changes:
  `package.json` adds `app:rebuild` + chains it into `build` (R03); `electron-builder.yml`
  comment aligned; NEW `shared/linkState.mjs` (pure four-state model: sim / live / link-lost /
  telemetry-lost) + NEW `test/linkState.test.js` (9 tests incl. demo-LQ=0 and sticky-staleness);
  `renderer/hud.js` three-state display (LQ=0 → LINK LOST; stale-after-live → TELEMETRY LOST
  holding last real values dimmed, never silent sim fallback; failsafe kept as demo OR-trigger);
  `renderer/hud.css` `.stale` dim; docs honesty pass (`docs/TELEMETRY.md` armed/failsafe marked
  demo-only + link-states table; `README.md`; `shared/telemetry.js` typedef). `ci.yml` adds the
  windows-latest package-smoke job (R17b). **Validation:** 30/30 vitest (21+9);
  `npm run app:rebuild` succeeds locally; local packaging smoke (`electron-builder --dir`,
  macOS analogue) produced a working .app with an Electron-ABI (125) serialport binding
  asar-unpacked + mediamtx resource. Demo GUI not launched (headless env) — the demo LQ=0
  window is covered by the timeline unit test. **Implementation note (honest):**
  electron-builder 24 ALSO auto-rebuilds native deps at package time (observed in the local
  run), so R03's "ships unbuilt" was partially overstated for packaging — the explicit
  app:rebuild chain remains valuable (dev-run `npm start` ABI + CI proof + belt-and-suspenders).
  electron-builder suggests an optional `postinstall: electron-builder install-app-deps`
  (NOT added — beyond approved scope; owner may consider later). CG2 (Windows target machine)
  + CG5 (real link drop) remain hardware/Windows validation items.
- **F1 — COMMITTED** as control-fw `b9cdac5` + learning-manual `157ab97` (owner-approved). Changes (control-fw only, plus
  the learning manual): `platformio.ini` pinned `espressif32 @ ~7.0.1` (R02); `src/main.cpp`
  setup() reordered so actuator PWM attaches first (R04 micro-mitigation — pure reorder, no
  hidden dependency found; applyTuning still runs last as before); `.github/workflows/ci.yml`
  adds the `esp32dev_tuning` build (R17a); `docs/D8_BENCH_BRINGUP.md` stale gimbal line fixed;
  new `README.md` (envs, per-OS flash, driver note, "gift ships plain esp32dev" warning);
  learning-manual `11_build_flash_debug_workflow.md` mirrors the flash-env warning + CI/pin
  accuracy fix. **Validation:** pin resolves to 7.0.1/core 2.0.17; 147/147 native tests;
  esp32dev + esp32dev_sim + esp32dev_tuning all build. Wokwi sim NOT run (no headless wokwi-cli;
  it is the VS Code GUI workflow — plan A1.6 remains a manual owner step). Siblings untouched.
- **F2, F3, F4 — not started** (per `12_recommended_fixes.md`).


Owner approved the fix set (recorded in `fix_approval_triage.md` → `12_recommended_fixes.md`):
R02, R03, R17, R07, R06(CI guard), R04-micro, hygiene subset; decisions R01=Option A (LQ+staleness),
R05=4 gears, R19=TRAINING/RACE/ERS, R10=no console endpoints pre-bench.
**Next step (awaiting go-ahead): IMPLEMENT batches F1 → F2 → F3 → F4** per
`12_recommended_fixes.md`, one commit per batch per repo, full tests+builds before each commit.
Hardware items remain in `11_hardware_validation_plan.md` untouched.

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
| 00 | `00_review_summary.md` | **✅ COMPLETE** | Written 2026-07-07 — final synthesis: verdict (ready for bring-up after 4 pre-power items), no Criticals, the 4 Highs, strongest parts, unknown-unknowns, before-power / before-motor / can-wait, decision classes, next step = fix-approval triage |
| 01 | `01_spec_vs_implementation.md` | **✅ COMPLETE** | Written 2026-07-07 from the spec dimension + verified severities; findings reference R## ids |
| 02 | `02_architecture_review.md` | **✅ COMPLETE** | Written 2026-07-07; R## refs; notes R07 High→Med adjustment |
| 03 | `03_hardware_electronics_review.md` | **✅ COMPLETE** | Written 2026-07-07; R## refs; notes R20 adjustment + R04 severity merge from safety |
| 04 | `04_control_firmware_review.md` | **✅ COMPLETE** | Written 2026-07-07; R## refs; notes R21 refutation + LEDC "near-limit" partial refutation |
| 05 | `05_soundlight_firmware_review.md` | **✅ COMPLETE** | Written 2026-07-07; R## refs; notes resolved I2S/7.0.1 open question |
| 06 | `06_ground_station_review.md` | **✅ COMPLETE** | Written 2026-07-07; R## refs; notes byte-for-byte JS↔fw verification tempering R07 |
| 07 | `07_protocol_review.md` | **✅ COMPLETE** | Written 2026-07-07; R## refs; wire layer clean, semantic-layer divergences + R13 relay question |
| 08 | `08_simulation_test_review.md` | **✅ COMPLETE** | Written 2026-07-07; R## refs; honest confidence statement (math high / silicon low) |
| 09 | `09_build_deploy_review.md` | **✅ COMPLETE** | Written 2026-07-07 from `_build_findings.json` incl. lead-verification corrections |
| 10 | `10_risk_register.md` | **✅ COMPLETE** | Written 2026-07-03 from the verified R## backbone. 22 R## entries (4 High, 13 Med, 5 Low) with all required fields + HW-required flags + a carried-lows appendix. |
| 11 | `11_hardware_validation_plan.md` | **✅ COMPLETE** | Written 2026-07-03. Ordered bench runbook (Phase A before-power / B first-power-logic-only / C later+ground), ~63 items deduped, R##-cross-referenced, keyed to D8 phases; includes both Top-10 lists + unknown-unknowns. |
| 12 | `12_recommended_fixes.md` | **✅ COMPLETE** | Written 2026-07-07 from owner approvals (see `fix_approval_triage.md`). Batches: F1 control-fw reproducibility (R02, R04-micro, R17a, hygiene) · F2 ground packaging+telemetry (R03, R01-A, R17b) · F4/F3 protocol+labels (R07, R06-CI-guard, R05=4 gears, R19=TRAINING/RACE/ERS) · HW list unchanged. **Fixes NOT yet implemented.** |
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
| `11_hardware_validation_plan.md` | **✅ DONE 2026-07-03** | (completed as specified — verified complete 2026-07-07) |
| `01_spec_vs_implementation.md` | **✅ DONE 2026-07-07** | (written from spec dimension + R## refs) |
| `02_architecture_review.md` | **✅ DONE 2026-07-07** | (written from arch dimension + R## refs) |
| `03_hardware_electronics_review.md` | **✅ DONE 2026-07-07** | (written from hw dimension + R## refs) |
| `04_control_firmware_review.md` | **✅ DONE 2026-07-07** | (written from ctrl-fw dimension + R## refs) |
| `05_soundlight_firmware_review.md` | **✅ DONE 2026-07-07** | (written from soundlight dimension + R## refs) |
| `06_ground_station_review.md` | **✅ DONE 2026-07-07** | (written from ground-station dimension + R## refs) |
| `07_protocol_review.md` | **✅ DONE 2026-07-07** | (written from protocol dimension + R## refs) |
| `08_simulation_test_review.md` | **✅ DONE 2026-07-07** | (written from sim/test dimension + R## refs) |
| `09_build_deploy_review.md` | **✅ DONE 2026-07-07** | (written from `_build_findings.json` + lead corrections) |
| `00_review_summary.md` | **✅ DONE 2026-07-07** | (final synthesis written) |
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
