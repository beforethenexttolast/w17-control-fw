# Phase B — First Power (standalone runbook)

> **Gate — read before doing anything else.** Phase B is **BLOCKED** until A2
> (`project-review/13_phase_a_a2_no_power_checklist.md`) is filled in with real bench
> readings, its §12 two-part PASS is recorded, and the result is reviewed and approved
> (`../../CURRENT_STATUS.md` carries the live status). **As of this revision A2 is
> NOT-EXECUTED and Phase B is BLOCKED** — no battery, no USB, no bench PSU, nothing
> flashed. This document is the standalone entry point for the moment that changes; it
> keeps the ledger (§Phase B in `project-review/11_hardware_validation_plan.md`) as the
> source ordering and links out to the phase-by-phase detail rather than duplicating it.

## What "first power" means here

**Logic only. ESC motor power stays disconnected for the whole of this document.** The car
gets its first battery and its first flashed firmware in this phase, but the motor does not
turn — that is Phase C (`11_hardware_validation_plan.md` §Phase C), gated on everything below
passing first. Wheels off the ground for all of it regardless.

Corresponds to `D8_BENCH_BRINGUP.md` Phases 1–6: this document is the safety-ordered summary
of *why* those phases run in that order; D8 is where the checkbox-level detail lives. Two-board
flashing detail: `COORDINATED_FLASH.md`. BT/showcase boot modes: `D8_BENCH_BRINGUP.md` Phase 3b
and `BT1_BENCH_GATE.md` — out of scope for this document, which covers CRSF/Drive-mode first
power only.

## Preconditions

- A2 closed, Phase B approved (above).
- Wheels off the ground. Car on a stand or bench fixture that cannot roll.
- An observer present — not a solo unattended session (workspace rule: no unattended
  flashing/powering).
- **The battery lead must be pullable at the master switch / XT60 split without reaching past
  any moving or hot part.** Know where your hand goes before you need it there.
- ESC signal and both servo/actuator leads physically disconnected until B3 below explicitly
  reconnects them one at a time.
- Tools ready: multimeter, oscilloscope or logic analyzer, serial monitor at 115200, the
  `elrs-joystick-control` PC setup, and a way to spin the rear axle by hand.

## B1 — Links & signals (`D8_BENCH_BRINGUP.md` Phases 2–3)

| # | Do |
|---|---|
| B1.1 | Confirm RP1 CRSF output: **420000 baud, 8N1, NOT inverted**, `NewRcFrame` decodes, channels move on the TX. |
| B1.2 | Antenna-off / RX-down event drives `uplinkLinkQuality → 0` and **latches `rxSignalsFailsafe`**. |
| B1.3 | **Scope GPIO13/14/18** pulse widths (1000/1500/2000 µs) + 50 Hz period on the real ESP32 **before** connecting ESC/servos — confirm LEDC did not silently reduce 16-bit resolution. |
| B1.4 | **Scope GPIO14 + GPIO13 from power-on through first `setup()` write** — confirm no ESC arm/twitch or servo kick in the pre-`ledcAttachPin` float window. This is the boot-float exposure A2's PD1 row records the harness side of; this is the firmware-timing side. |

**Expected observations:** channels track the TX within the normal CRSF update rate; a link
drop is visible in the decoded state within up to ~540 ms worst-case detection against the
500 ms budget (`src/main.cpp:1026`); the GPIO13/14/18 scope traces
show clean 50 Hz PWM with no glitch or transient pulse before `setup()` finishes attaching the
LEDC channels.

**Stop conditions:** no CRSF frames at all (check RP1 binding + baud/inversion before
suspecting firmware); any pulse or twitch on GPIO13/14 before the boot-float window closes with
actuators connected (do not proceed to B2 with actuators attached until this traces clean —
reconnect actuators only after B1.4 passes with them disconnected).

## B2 — The safety chain (`D8_BENCH_BRINGUP.md` Phase 5 — THE GATE)

**Do not skip this section and do not proceed past it on a partial pass.** This is the whole
point of gating power at all.

| # | Do |
|---|---|
| B2.1 | Power up with **no CRSF** → expect ESC neutral, DRS closed, steering centered. |
| B2.2 | Bring the link up **with the throttle stick forward** → motor command stays off until the stick returns to neutral (ArmGate) and the arm conditions are met. |
| B2.3 | Confirm the ESC arms every boot with the neutral-hold sequence (motor still disconnected); reconcile `bootArmHoldMs=2000` against the ESC's own manual; confirm **forward/brake** ESC mode (not forward/reverse). |
| B2.4 | Confirm worst-case failsafe detection latency at the chosen RP1 packet rate + LQ=0 burst stays within up to ~540 ms worst-case detection against the 500 ms budget (`src/main.cpp:1026`). |
| B2.5 | **Re-arm invariant** (`D8_BENCH_BRINGUP.md` Phase 5, full detail): a failsafe episode latches a disarm. Recovery with the arm switch left ON through the episode **must stay disarmed**; only an OFF→ON toggle with the stick centered re-arms; a boot with the switch already ON needs the same toggle. Contract: `lib/channels/include/channels/ArmGate.hpp`. |

**Expected observations:** every one of the above holds exactly as stated — there is no
"mostly passes" state for this section. A fresh neutral alone appearing to re-arm after a
failsafe episode is a **regression**, not a bench quirk; stop and report rather than continuing
to B3.

**Stop conditions:** ESC arms or accepts throttle before the boot-arm hold elapses; the arm
gate ever allows arm-into-full-throttle; a failsafe episode fails to latch a disarm; recovery
re-arms without the required switch toggle; failsafe detection exceeds ~540 ms worst-case
against the 500 ms budget. Any
one of these is a hard stop — pull the battery lead, do not power-cycle-and-retry, report the
exact reading first.

## B3 — Actuators (bench, unloaded) & board #2 (`D8_BENCH_BRINGUP.md` Phases 6–7b, 9)

Only after B2 passes completely. Reconnect actuators one at a time, not all at once.

| # | Do |
|---|---|
| B3.1 | Narrow steering endpoints to the linkage's mechanical travel; sweep full L/R with the linkage fitted — no bind/stall on the servo. |
| B3.2 | Confirm DRS 1000 µs = wing closed (the failsafe-safe position); swap open/closed in config if reversed. |
| B3.3 | **link2 on the wire**: capture control TX (GPIO25 → board #2 GPIO16) at 115200 8N1; board #2's `Link2Monitor` reports `FrameReady`, not `BadVersion`/`FrameInvalid` — proves the two copied `lib/link2` trees agree in the flashed firmware, not just in source. See `COORDINATED_FLASH.md` if this fails. |
| B3.4 | Decode a live link2 frame on board #2 identical to sender intent (throttle%, brake bit, ERS-deploy bit, gear, driveMode). |
| B3.5 | I2S audio through the legacy driver on the pinned core; check `i2s.begin()` return codes; confirm no fail-silent/block. |
| B3.6 | WS2812 `show()` does not glitch while audio DMA runs (dual-core + DMA/RMT interaction). |
| B3.7 | Board #2 with link2 RX disconnected at boot → confirm the calm "never-connected" idle is acceptable; then cut the wire mid-run → confirm Lost→hazard within 500 ms. |
| B3.8 | Board #2 mid-frame power-on ordering (board #1 already transmitting) → boots NeverConnected, syncs on the next start byte. |
| B3.9 | Camera gimbal (right stick, ch9/ch10): fit the two pan/tilt servos, power up disarmed — they center; move the right stick, confirm axes track and aren't inverted; on a link drop, confirm the smooth ~2 s glide to center (`gimbal.decay`), not a snap and not a hold; confirm it glides back on recovery. |

**Expected observations:** each actuator moves only as commanded, within its mechanical
travel, with no bind or stall; link2 decodes cleanly on board #2 with sound/light responding to
board #1 state; the gimbal's failsafe behavior is a glide, never a snap or a frozen hold.

**Stop conditions:** any servo binds or the ESC/servo stalls audibly — back off immediately,
this is a mechanical limit, not a tuning target; link2 reports anything other than
`FrameReady` in steady state — do not proceed to B4 with a link2 mismatch unresolved; a gimbal
snap or hold on link loss instead of a glide.

## B4 — Sensors (bench)

| # | Do |
|---|---|
| B4.1 | ADC battery divider extremes: open/disconnected divider and full 8.4 V → sane `batteryMv`, no spurious low-voltage latch; log which eFuse cal type the ADC reports; check the 8.4 V point isn't compressed by the 11 dB attenuation ceiling. |
| B4.2 | Hall (GPIO35) counts on a rolling bench test (single magnet); baseline the reading before any motor EMI is present (motor is not yet connected in this phase). |
| B4.3 | **Battery-sense plausibility (OD-10):** with the pack at a known voltage, lift the divider's **lower** leg, then the **upper** leg, one at a time. Expect: no CRSF battery frame at all (GS shows stale/"--", phone placeholders), **no** low-battery warning newly latched, and `batteryMv` 0 on link2 — not a fabricated number and not a 100 %-full reading. A warning that was ALREADY latched is held for `warnDelayMs` (3 s) of continuous implausibility and only then dropped, so watch for that too. Restore each leg → the reading returns exact on the next sample. Record the converted mV each fault actually produces and confirm it lands outside 4000–9000 mV (`BatteryConfig::implausibleBelowMv/AboveMv`). |
| B4.4 | **Hall interrupt-rate margin, motor-free half (OD-11):** motor still disconnected (this document's rule above): hand-spin the rear axle and log `hallSensor.isrEntries()`, `lastWindowEntries()`, and `guardFaults()` over a ~10 s roll — the bench console's `status` line prints all three — then repeat with the GPIO35 10 kΩ pull-up lifted (scope the pin). Record entries per 100 ms for both the plain roll and the lifted pull-up, and whether `guardFaults()` ever increments. The guard's bound is 180 entries/100 ms (20× the 5000 rpm maximum); confirm the hand-spun roll sits far below it. The full-throttle/ESC-EMI half of this measurement — motor connected, wheels off the ground — is `D8_BENCH_BRINGUP.md` **Phase 8 — Telemetry sensors**, not this document. |

**Expected observations:** battery reading tracks a real multimeter reading within the
calibration tolerance once `batt.ppt` is set (full two-point calibration is Phase 8/D8); Hall
counts increment cleanly on a hand-spun wheel with no double-counting at low speed. B4.3/B4.4
are the two rows whose numbers nothing has measured yet — the firmware's plausibility band and
interrupt-rate bound are derived from circuit reasoning and native tests only, so what you
record here is the first real evidence either way.

**Stop conditions:** a divider reading that suggests a wiring fault rather than a calibration
offset (e.g., a reading that doesn't move at all as voltage changes) — stop and re-check the
harness against A2's divider rows before trusting any calibration built on top of it.

## After Phase B passes

Phase C (`11_hardware_validation_plan.md` §Phase C, `D8_BENCH_BRINGUP.md` Phases 7–11) is
motor-connected, on-the-car, and calibration work — only after every item above is a recorded
PASS. Delivery hand-off (bench build → console-free gift firmware) is `D8_BENCH_BRINGUP.md`
Phase 11a. Nothing in this document authorizes connecting the ESC's motor leads; that stays
gated on B2 passing in full, per the golden rule this whole runbook exists to protect.
