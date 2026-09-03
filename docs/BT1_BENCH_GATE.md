# BT1 — BT show-off / showcase bench gate

> **Gate.** BT1 is a Phase B item: it can only begin once powered bench work is legal at all
> (A2 closed, Phase B approved — `../../CURRENT_STATUS.md`, `D8_BENCH_BRINGUP.md` Phase -1,
> `PHASE_B_FIRST_POWER.md`) and it runs **car on stand, wheels off the ground**; ESC
> motor-power rules are the existing ones, unchanged by anything in this document. **No BT
> code runs on powered hardware before BT1 is opened by the owner.** This gate is promoted
> from `docs/bt_showoff_design.md` §5/§7/§9 (design ratified 2026-08-17; see that document's
> own status line for its merge history) — it did not previously exist as a standalone
> runbook, which is the gap this document closes.

## What BT1 covers

Two boot modes share one physical selector and one bench gate: **BT_SOLO** (a paired
DualShock-class pad drives the car directly, camera off, `docs/bt_showoff_design.md`) and
**Showcase** (stationary demo, cannot arm by any input — `lib/bootmode`). `D8_BENCH_BRINGUP.md`
Phase 3b is this gate's boot-mode-resolution slice (each selector position → the right mode,
ambiguous → Drive); BT1 is everything Phase 3b does not cover — the parts that need a real
pad, a real BT radio, or sustained power to observe at all.

**Builds involved:**
- `esp32dev_btshowoff` — the real gate build: Bluepad32/BTstack on the pinned custom core
  (3.10.2), `huge_app.csv` partition, needs the physical SP3T selector wired (A2 §S4c) and a
  real DualShock-class pad.
- `esp32dev_simbt` — same `W17_BT_SHOWOFF` main wiring, scripted `SimPadFeeder` session, stock
  core, no Bluetooth hardware at all. Useful for exercising the boot-mode/arm-gate wiring (as
  in Phase 3b) without a pad, but it does **not** exercise anything on the bench-only list
  below — that list is specifically the things a script cannot stand in for.

## Pre-conditions specific to this gate

- A2's §S4c selector rows are recorded (continuity/isolation for GPIO27/GPIO32), or the
  selector is recorded NOT-ASSEMBLED and this gate is deferred until it is wired.
  `PinMap.hpp:36-56`, `project-review/13_phase_a_a2_no_power_checklist.md` §S4c.
  `esp32dev_simbt` does not need the physical switch and may be exercised independent of this
  precondition (it boots straight into BT_SOLO with no strap read at all).
- `D8_BENCH_BRINGUP.md` Phase 3b **is BT1 item 0** — not a precondition BT1 waits on, its
  first action, done under this same gate (each selector position resolves to the mode it
  should, ambiguous → Drive, Showcase cannot arm, BT_SOLO never opens the CRSF UART). Do the
  rest of this gate's items only after Phase 3b passes.
- Wheels off the ground. Car on a stand. Observer present. Battery lead pullable at the star
  switch/master disconnect, same as every other Phase B item.
- A pad: genuine Sony DualShock-class hardware is the spec — clone controllers are known-flaky
  (design §5, bluepad32 issue #127 cited there) and a clone failing this gate is not evidence
  the firmware is wrong.

## The bench-only list (from design §5/§7 — these ARE the gate items)

None of these are provable without real hardware running for real time; that is why they are
gated here rather than closed by native tests. Record a PASS/FAIL/NOT-RUN and evidence for
each — this table is the gate's own §11-style ledger, kept in this document rather than
duplicating A2's format.

| Item | What to observe | Why it can't be a native test |
|---|---|---|
| Actual flash fit | `pio run -e esp32dev_btshowoff` size report | Measurable at build time, not bench time — run once and record; see current numbers below |
| Free-heap watermark | Log/print free heap at boot, during pairing, while driving, and on a pad disconnect storm (rapid connect/disconnect) | Real BTstack heap use, not a native-testable path |
| Control-tick jitter with BT active | Scope/instrument the 50 Hz control tick while `BP32.update()` runs on core 0 | Needs the real BT controller task running |
| TWDT margin on both cores | Confirm `esp_task_wdt_init(2, true)`'s 2 s deadline is never approached on core 0 under BT load or core 1 under the control loop | Needs sustained real load |
| Pairing / reconnect reliability | Multiple pair/unpair/reconnect cycles | Inherently a real-radio behavior |
| `enableNewBluetoothConnections(false)` lockout | Confirm the post-pairing-window lockout actually holds — there is a filed upstream report of it not functioning in at least one Bluepad32 version (design §5, bluepad32 #130) | Upstream library behavior, not this firmware's code |
| Real disconnect → outputs-safe latency | Walk-away case and pad-power-off case, both | Real RF/radio timing |
| DS4 idle report-rate assumption | Confirm the pad's actual idle report cadence matches design §3.1's assumption | Real pad behavior |
| DS4 auto-sleep behavior | Confirm what happens to arming/failsafe when the pad auto-sleeps mid-session | Real pad behavior |
| **Reconnect-without-input probe (review F1, 2026-08-17)** | Power the pad back on, or walk back into range, and touch **nothing**. The car must stay in failsafe until genuine post-connect reports flow — the wrapper seeds its freshness baseline at connect time, so a bare stack reconnect claim alone must yield zero drive-affecting frames | The custom-core wrapper path has no native-test seam; this is the one item design §7 calls out as needing hardware proof specifically (the sim stage's reconnect-CLAIM-only beat is the wiring-level twin of this, already native-pinned — this probe is the hardware-side confirmation that the real stack behaves the same way) |
| 3.3 V rail draw with BT active | Measure current draw with BT active vs. inactive | Real electrical measurement |
| ELRS-RX-powered coexistence | RP1 stays powered on the UBEC rail even in BT mode (nothing reads it, but it is transmitting/receiving); confirm no RF interference with the BT radio at 2.4 GHz close range | Real RF coexistence, not simulatable |
| Genuine-vs-clone DS4 behavior | If only a clone pad is on hand, note it explicitly and treat this gate as provisional | Design §5 flags clones as known-flaky; a clone PASS is weaker evidence than a genuine-pad PASS |

**Resource numbers, measured this revision (re-run `pio run -e esp32dev_btshowoff` /
`-e esp32dev` if the code has changed since):**
- `esp32dev_btshowoff`: RAM 26.6% (87012/327680 B), Flash 23.6% (741121/3,145,728 B of the
  `huge_app.csv` 3 MB app slot it requires — no OTA slot on this build, by design, since the
  project uses no OTA).
- `esp32dev` (plain delivery, for comparison): RAM 7.0% (22884/327680 B), Flash 23.1%
  (302837/1,310,720 B of the default app slot).
- ELF quarantine spot-check (same command as `D8_BENCH_BRINGUP.md` Phase 11a step 7):
  `esp32dev` reports `0` for `console::|btpad|luepad|btstack` combined; `esp32dev_btshowoff`
  reports `216` (`btpad` alone: 27; the remainder is `luepad`/`btstack` library symbols) as the
  **positive control** proving the check itself is not vacuous; `esp32dev_tuning` reports `6`
  (`console::` only) as the tuning-console positive control.

## What this gate does NOT decide

Whether the shipped car ever runs `esp32dev_btshowoff` at all is the `OWNER-DECISION(SHIP-IMAGE)`
question recorded at `D8_BENCH_BRINGUP.md` Phase 11a step 7 — BT1 passing proves the mode
*works*, not that it *ships*. If the SP3T selector is wired but the ship image stays plain
`esp32dev`, the selector is electrically inert on the delivered car (`src/main.cpp:147-157`
never reads GPIO27/32) and BT1 stays a bench-only demonstration.

## Relationship to A2 / Phase B (unchanged)

A2 and Phase B are untouched by anything in this gate or in the BT show-off design — nothing
here changes their scope, order, or wording (`docs/bt_showoff_design.md` §9). The only contact
point is additive: the SP3T selector's pins joined A2's continuity-matrix scope as §S4c. Branch
discipline for any future change to this mode follows the same precedent the design set in
2026-08-17: design first, prototype behind a compile flag with native tests only, nothing
flashed or merged before the owner reviews it.
