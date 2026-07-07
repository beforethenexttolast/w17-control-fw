# 12 — Recommended Fixes (Owner-Approved)

Written from `fix_approval_triage.md` after owner approval (2026-07-07). **Nothing here is
implemented yet** — this is the implementation contract. Fixes are grouped into small, safe
batches; each batch is independently buildable, testable, committable, and revertible.

**Owner decisions locked in:**
- **R01 → Option A**: derive link-loss from LQ + staleness on the ground; car transmits nothing new.
- **R05 → 4 gears everywhere** (firmware is the authority).
- **R19 → TRAINING / RACE / ERS** labels everywhere; naming-only change.
- **R06 → CI drift-guard**, not submodule.
- **R10 → no console-tunable endpoints** before bench; edit+reflash accepted.
- **R04 → software micro-mitigation only** (begin-order); pull-down/RC stays bench-gated.
- R21 rejected (refuted) · R22 accepted trade-off — **no fix**.

Batch order is the recommended implementation order: F1 → F2 → F3 → F4. Each batch =
one commit (or one commit per repo where a batch spans repos).

---

## Batch F1 — Pre-power software/reproducibility (control-fw)

### F1.1 · R02 — Pin the platform  *(priority: highest)*
- **Change:** `platform = espressif32` → `platform = espressif32 @ ~7.0.1` in `[env:esp32dev]`
  (children inherit via `extends`). Add a one-line comment mirroring soundlight's ("pinned so the
  core-2.x channel LEDC API stays a fact; matches w17-soundlight-fw").
- **Files:** `w17-control-fw/platformio.ini`.
- **Steps:** edit line → clean build all envs → verify `pio pkg list` still resolves 7.0.1/core 2.0.17.
- **Tests:** `pio test -e native` (147); `pio run -e esp32dev -e esp32dev_sim -e esp32dev_tuning`.
- **Docs/manual:** none. **HW still required:** no (plan keeps the fresh-machine clean-cache build pre-gift).
- **Rollback risk:** none (today's resolution is already 7.0.1; the pin only freezes it).

### F1.2 · R04 (micro-mitigation) — PWM begins first in setup()  *(priority: high)*
- **Change:** move the five `*Pwm.begin(...)` calls (steering, ESC, DRS, pan, tilt) and the
  five initial `setPosition/setThrottle/setOpen` safe-commands to the **top of `setup()`**,
  before `crsfUart/link2Uart/batteryAdc/hallSensor.begin()`. Shrinks the GPIO13/14 float window
  by the duration of four begins. No dependency exists in the moved code on the UART/ADC/ISR
  inits (verify by inspection during implementation; if any is found — stop and report instead
  of forcing it).
- **Files:** `w17-control-fw/src/main.cpp` (setup() reorder only).
- **Steps:** reorder → build → run the Wokwi sim once to DRIVING (doubles as plan A1.6).
- **Tests:** `pio test -e native`; all three ESP32 builds; Wokwi sim reaches `failsafe=0`.
- **Docs/manual:** none (bench note in D8 unchanged). **HW still required:** **yes** — B1.4
  scope of the remaining float window + A2.5 pull-down decision stay bench-gated.
- **Rollback risk:** very low (pure reorder; revert restores exactly the audited ordering).

### F1.3 · R17a — CI builds the tuning firmware  *(priority: high)*
- **Change:** add step `pio run -e esp32dev_tuning` to control-fw CI.
- **Files:** `w17-control-fw/.github/workflows/ci.yml`.
- **Tests:** push → CI green (it builds locally today, so this is a guard, not a fix-up).
- **Docs/manual:** none. **HW:** no. **Rollback risk:** none (additive CI step).

### F1.4 · Hygiene (approved subset) — docs-only  *(priority: medium)*
- **Change:** (a) fix the stale D8 line "Gimbal pan/tilt (decoded, unwired)" → wired/done,
  pointing at Phase 7b; (b) add `w17-control-fw/README.md` with: the three envs and their
  purpose, per-OS flash commands (`pio run -e <env> -t upload`, macOS `/dev/tty.usbserial*` vs
  Windows `COMx`, CP210x/CH340 note), and a bold **"the gift ships plain `esp32dev` — never
  `_sim`/`_tuning`"** warning.
- **Files:** `docs/D8_BENCH_BRINGUP.md` (1 line), `README.md` (new).
- **Tests:** none (docs); native suite as a smoke.
- **Docs/manual:** **yes** — mirror the flash-env warning into your learning manual.
- **HW:** no. **Rollback risk:** none.

**F1 exit criteria:** 147 native green · 3/3 ESP32 envs build · CI green incl. tuning · Wokwi
sim reaches DRIVING · one commit pushed.

---

## Batch F2 — Ground-station packaging + telemetry truthfulness

### F2.1 · R03 — serialport Electron-ABI rebuild  *(priority: highest in F2)*
- **Change:** add `"app:rebuild": "electron-rebuild -f -w serialport"` to package.json scripts;
  chain `"build": "npm run app:rebuild && electron-builder --win"`; align the stale
  electron-builder.yml comment to the now-real script name.
- **Files:** `w17-ground-station/package.json`, `electron-builder.yml` (comment).
- **Steps:** add script → local `npm run app:rebuild` sanity (macOS dev machine: verify it runs;
  ABI proof is Windows-side) → commit.
- **Tests:** `npm test` (21); CI (after F2.3). Final proof = plan **CG2** on the Windows machine
  (packaged app logs "CRSF serial open").
- **Docs/manual:** none. **HW still required:** target-machine verification only (CG2).
- **Rollback risk:** low (dev `npm start` path untouched; only `build` changes).

### F2.2 · R01 (Option A) — honest link-loss on the HUD  *(priority: high)*
- **Change (behavior):**
  1. While live: `linkQualityPct === 0` → **"LINK LOST"** (red), replacing the dead
     `telem.failsafe` trigger (keep `failsafe` as an additional OR-trigger for the demo).
  2. New **degraded state**: if telemetry *was* live and then goes stale (fresh <1 s fails but
     was live within the last ~10 s) → show **"TELEMETRY LOST"** (red/amber) and do **not**
     silently resume simulated speed/gear/ERS — hold last real values dimmed or blank them.
     Only a source that was *never* live shows the current "Telemetry: sim".
  3. Docs: remove `armed`/`failsafe` from the TELEMETRY.md contract table (or mark
     "demo-only, not transmitted by the car"); fix the README "…and failsafe" line; note the
     LQ+staleness derivation.
- **Files:** `w17-ground-station/renderer/hud.js` (telemetryLive/render), `docs/TELEMETRY.md`,
  `README.md`, `shared/telemetry.js` (typedef comment).
- **Steps:** implement the three-state logic (never-live / live / lost-after-live) → verify in
  `npm run demo` (the demo timeline's LQ=0 window at t=14–17 s must now show LINK LOST via the
  LQ trigger, proving the real-path trigger) → docs.
- **Tests:** `npm test`; if the state logic is extracted into `shared/` (recommended: a small
  pure `linkState(nowMs, lastFreshMs, everLive, lq)` helper), add vitest cases for the three
  states + the LQ=0 case.
- **Docs/manual:** **yes** — the learning manual's HUD section must describe the three states.
- **HW still required:** **yes** — CG5 (deliberate real link drop) validates end-to-end.
- **Rollback risk:** modest — the one behavioral fix in the set. Guard: "TELEMETRY LOST" must
  trigger **only** if previously live, so a bench PC with no source configured still shows
  plain "Telemetry: sim". Revert = restore current render() block.

### F2.3 · R17b — packaging smoke in CI  *(priority: medium)*
- **Change:** add a `windows-latest` CI job: `npm ci` → `npm run app:rebuild` →
  `npx electron-builder --dir` (no installer, no publish). Marks the deliverable buildable.
- **Files:** `w17-ground-station/.github/workflows/ci.yml`.
- **Tests:** push → both CI jobs green.
- **Docs/manual:** none. **HW:** no.
- **Rollback risk:** none (additive job); if the runner proves flaky, mark it non-required
  rather than reverting.

**F2 exit criteria:** 21+ vitest green (plus new link-state tests) · demo shows LINK LOST during
its LQ=0 window · CI green incl. Windows packaging job · one commit pushed.

---

## Batch F3 — Protocol agreement + drift guards

### F3.1 · R07 — shared CRSF golden fixture + honest comment  *(priority: high)*
- **Change:** (a) commit `test/fixtures/crsf_golden.json` in the ground station: exact hex
  frames for BATTERY (7.9 V/72% — the firmware's `test_build_battery_frame_bytes` vector),
  GPS (groundspeed 361 = 36.1 km/h), FLIGHTMODE ("G3 M2 E55"), LINK_STATISTICS — bytes
  copied from / cross-checked against the firmware builder tests; (b) load the fixture in
  `crsf.test.js` + `crsfTelemetry.test.js` and assert decode results; (c) rewrite the
  `shared/crsf.js` header comment to say layouts are *mirrored and pinned by a shared fixture*
  (not "golden vectors are reused"); (d) add a comment in the firmware `test_crsf` pointing at
  the fixture file for traceability.
- **Files:** `w17-ground-station/test/fixtures/crsf_golden.json` (new), `test/crsf.test.js`,
  `test/crsfTelemetry.test.js`, `shared/crsf.js` (comment); `w17-control-fw/test/test_crsf/
  test_main.cpp` (comment only).
- **Tests:** ground `npm test`; control `pio test -e native` (comment-only).
- **Docs/manual:** no. **HW still required:** CG4 remains the live-frame proof.
- **Rollback risk:** none (test-only + comments).

### F3.2 · R06 — link2 CI drift-guard  *(priority: high)*
- **Change:** in **both** firmware repos' CI, add a job step that clones the sibling repo
  (depth 1) and `diff`s the four contract files: `lib/link2/include/link2/Link2Frame.hpp`,
  `lib/link2/include/link2/Link2Codec.hpp`, `lib/link2/src/Link2Codec.cpp`,
  `docs/link2_protocol.md`. Any diff → CI fails with a "link2 copies diverged" message.
  If the repos are private to the CI token, fall back to a committed SHA-256 manifest checked
  in both repos (a protocol change then requires updating both manifests — same guarantee,
  no cross-clone).
- **Files:** `w17-control-fw/.github/workflows/ci.yml`,
  `w17-soundlight-fw/.github/workflows/ci.yml` (+ optional manifest files).
- **Steps:** implement clone+diff → push both → confirm green (copies are identical today) →
  document in both CI files that a deliberate protocol change means pushing both repos
  back-to-back (brief red on one side is expected and desired).
- **Tests:** CI runs on both repos.
- **Docs/manual:** no (CI-internal). **HW still required:** B3.3/B3.4 remain the on-wire proof.
- **Rollback risk:** low; worst case is a false-failing guard, which is removable without
  touching firmware.

**F3 exit criteria:** ground + both firmware CI green with the new guards · fixture asserts pass.

---

## Batch F4 — Owner-decision alignment: gears + labels (docs/UI, cross-repo)

*Implement after F3.2 so the link2-doc edits in both repos are made under the drift-guard.*

### F4.1 · R05 — 4 gears everywhere  *(priority: medium)*
- **Change:** (a) `GEARS: 8` → `GEARS: 4` in `shared/feelConstants.js` (HUD ring/caps/redline
  adapt via `FEEL`); update the hud.js *default* `FEEL = { gears: 8 …}` → 4 for consistency;
  (b) demo timeline gears clamped to ≤4 (`shared/replaySource.js` keyframes currently reach 6–7)
  and **update `replay.test.js` expectations accordingly** (it asserts gear 6 at t=6000 today);
  (c) `docs/link2_protocol.md` gear range "1…6" → "1…4" **identically in both firmware repos**;
  (d) sanity-check no HUD logic assumes >4 (shift clamp, computeCaps loop — both FEEL-driven).
- **Files:** `w17-ground-station/shared/feelConstants.js`, `shared/replaySource.js`,
  `renderer/hud.js` (default), `test/replay.test.js`;
  `w17-control-fw/docs/link2_protocol.md` + `w17-soundlight-fw/docs/link2_protocol.md`.
- **Tests:** ground `npm test`; both firmware `pio test -e native` (docs don't affect them, run
  as smoke); the F3.2 drift-guard proves the two protocol docs stayed identical.
- **Docs/manual:** **yes** — gear behavior description.
- **HW still required:** CG6 later (live gear sweep). **Rollback risk:** low (constants + docs +
  test expectations; no firmware logic change — `numGears=4` already shipped).

### F4.2 · R19 — TRAINING / RACE / ERS labels everywhere  *(priority: low)*
- **Change:** naming-only alignment to the HUD's labels: `docs/link2_protocol.md` driveMode row
  → "0 = TRAINING, 1 = RACE (gearbox), 2 = ERS (gearbox + ERS deploy)" **in both repos**;
  matching comment updates in `Link2Frame.hpp` (both repos, identical), `ChannelDecoder.hpp`,
  and any main.cpp mode comments. `docs/TELEMETRY.md` already matches. **No behavior change, no
  wire change, no enum renames in code identifiers** — comments and docs only.
- **Files:** both repos' `docs/link2_protocol.md` + `lib/link2/include/link2/Link2Frame.hpp`;
  `w17-control-fw/lib/channels/include/channels/ChannelDecoder.hpp`, `src/main.cpp` (comments).
- **Tests:** both firmware `pio test -e native` + builds (comment-only); drift-guard green.
- **Docs/manual:** **yes** — mode names.
- **HW still required:** no. **Rollback risk:** none.

**F4 exit criteria:** all three repos green · drift-guard green after the paired doc edits ·
demo HUD shows ≤4 gears.

---

## HW — remains in the hardware validation plan (no code now)

Unchanged from `11_hardware_validation_plan.md`; listed for completeness of the approval record:

| R## | Item | Plan phase |
|---|---|---|
| R04 (hardware half) | Scope GPIO14/13 boot float; decide pull-down/RC | B1.4 / A2.5 |
| R08 | Pin continuity vs soldered board | A2.1 |
| R09 | ESC arm-hold value + forward/brake mode | B2.3 |
| R10 | Steering endpoint values (edit+reflash accepted) | B3.1 |
| R11 | I2S / WS2812 / HAL bring-up | B3.5–B3.8, B4 |
| R12 | Brownout/reset coast | C1 |
| R13 | ELRS relay of 0xC8 telemetry frames | CG3 |
| R14 | FT232 COM sharing | CG2 |
| R15 | Camera codec / video | CG1 |
| R18 | Hall EMI at load | C2 |
| R20 | WS2812 level shift (prefer 74AHCT125) | A2.4 |

**No fix:** R21 (refuted) · R22 (accepted intentional design; revisit only if bench behavior
changes your preference).

---

## Implementation ground rules (all batches)

- One batch = one commit per touched repo; commit messages reference the R## ids.
- After each batch: full test suite + all builds in every touched repo **before** commit; push;
  confirm CI green before starting the next batch.
- Any surprise (a moved call turns out to have a dependency, a test asserts something
  unexpected) → stop and report, don't improvise around it.
- `review_progress.md` is updated after each batch lands.
