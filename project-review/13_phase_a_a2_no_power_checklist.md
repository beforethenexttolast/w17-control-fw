# W17 Phase A A2 — Desk / No-Power Checklist

> **STATUS — NOT EXECUTED.** This document *defines how A2 will be performed*. A2 has **not
> been run**, there are **no measurements recorded**, and A2 is **NOT closed**. **Phase B
> remains blocked** until this checklist is filled in, pasted back, reviewed, and approved.
> (Prerequisite context: A1.1–A1.6 software/pre-power items are complete; A1.6 Wokwi sim
> passed — see `_verification_results.md`. A2 is the physical desk half of Phase A from
> `11_hardware_validation_plan.md`.)

> **RESTRUCTURED 2026-07-30 — A2 is now the build order, not a single pass at the end.**
> A2 previously assumed a finished harness and verified it in one sweep. As of 2026-07-30
> **nothing is soldered** (owner-confirmed; the ESP32 boards arrived with pre-soldered pin
> headers, and the 3D-printed parts on hand are test/measurement-quality prints, not build
> parts). That made the old shape actively wrong, not merely inconvenient: several expected
> values are **only valid while the subassembly is isolated**, and measuring them on a
> completed harness would produce false failures. The worked example is §S1 — the divider's
> `batt+ → GND ≈ 37 kΩ` row is measured in parallel with two UBEC input stages once the BECs
> are attached, so the expected value is not defensible there. See §S1's warning box.
>
> A2 is therefore **staged**: each gate runs on an isolated subassembly *as you build it*,
> and only §S7/§S8 are true whole-harness gates. This localizes a fault to the subassembly
> you just built instead of hiding it among ~40 joints, and it means no gate is ever read
> against an invalid expected value. The cost is real: A2 is no longer an afternoon with a
> multimeter, it is interleaved with build week.
>
> This restructure also **absorbs what would otherwise be a separate assembly gate.** There
> is no A1.7: the build steps are the S-gates below.

**Golden rules for this session:** battery stays disconnected and out of reach. No USB, no
bench PSU, nothing flashed. Multimeter only. If any measurement is suspicious → **stop,
photograph, report** — don't "try again with power to see."

## 1. Tools

- Multimeter with continuity beeper, resistance, and **diode mode**
- Fine probes or probe clips (DevKit pin headers are 2.54 mm)
- Good lighting + magnifier/phone-macro for solder joints
- Phone for the photo checklist (§10)
- USB cable and bench PSU: **have them, do not connect them** — both are Phase B
- The pin references: `lib/config/include/config/PinMap.hpp` (board #1 authority), soundlight `PinMap.hpp` (board #2), `docs/00_BUILD_SHEET.md` bench fixes

> **Diode mode is load-bearing, not optional.** The WS2812 supply decision is now fixed to
> the 1N5819 (§S5, decision 2026-07-30), and the forward-voltage reading is the *only* check
> that proves the diode is both present and correctly oriented. If your meter lacks diode
> mode, stop and say so — the substitute is a reverse/forward **resistance** asymmetry check,
> which is weaker and needs writing before S5 runs.

**Reference points used below:** "GND" = battery − wire / XT60 − pin (probe from there unless stated). "5 V rail A/B" = the two UBEC outputs (A = clean rail: camera, WiFi, both ESP32s, RP1, LEDs; B = servo rail: steering + 3× MG90S + blower). **Before §S6 the rails exist only as harness wiring, not as energized nodes** — rail rows before S6 verify *where a wire goes*, not a voltage.

## 2. Visual inspection (runs per-gate, plus once at the end)

Do the relevant subset at each S-gate on the subassembly in front of you, then repeat the
whole list once after §S6. Work under strong light, top then bottom of every board:

- [ ] **Solder bridges** — every hand-soldered joint on both ESP32 headers, the divider, the Hall wiring, WS2812 connections; especially adjacent DevKit pins (16/17, 25/26, 18/19)
- [ ] **Polarity** — XT60 orientation; UBEC in/out polarity; electrolytic caps (1000 µF servo-rail, 1000 µF WS2812) stripe = negative; 1N5819 band toward the strip VDD side
- [ ] **Connector orientation** — every 3-pin servo plug: signal–+5–GND order matches its header (MG90S lead: orange=signal, red=+, brown=−; DS3235SG same scheme). Mark verified plugs with a paint dot
- [ ] **Loose wires / strain relief** — tug-test every crimp and solder joint gently
- [ ] **Mechanical shorts** — no bare wire near the ESP32 pin rows, no board standoff touching traces, nothing conductive loose in the tub
- [ ] **Heat-shrink/insulation** — divider resistors, Hall splice, the isolated ESC red wire (must be individually insulated, not just folded back bare)
- [ ] **ESC/BEC wiring** — ESC 3-wire servo lead at board #1: **red visibly not connected**; ESC sensor cable seated; ESC set/labelled sensored
- [ ] **Battery divider** — 27 kΩ from batt+ side, 10 kΩ to GND, tap to GPIO34, 100 nF tap→GND present
- [ ] **Hall sensor** — A3144: VCC to **5 V**, GND common, output to GPIO35 with **10 kΩ pull-up to 3.3 V** (pull-up goes to 3V3, *not* 5 V — check this specifically)
- [ ] **Servo/ESC signal wiring** — five signal wires route to GPIO13 (steer), 14 (ESC), 18 (DRS), 19 (pan), 23 (tilt); none to strapping pins 0/2/12/15

## 3. Measurement conventions (read once, applies to every gate below)

Three rules that resolve ambiguities the single-pass version left open:

1. **Capacitor charging is not a short.** Any resistance reading across a node with a cap on
   it (100 nF divider tap, 1000 µF servo rail, 1000 µF WS2812) **starts low and rises** while
   the meter charges it. Wait for it to settle. A *persistent* ≈0 Ω is a fail; a rising
   reading is not. This applies to §13 hard-stop 1 as much as anywhere else.
2. **Isolation rows are measured with the ESP32 unseated** where the board sits in female
   headers. An installed ESP32 presents internal ESD protection diodes from every GPIO to GND
   and 3V3, which can read as a low resistance and produce a false failure. If a board is
   hard-wired rather than socketed, use **resistance mode, expect ≫ 10 kΩ**, and do not use
   the beeper for that row — record the actual value.
3. **Continuity rows are measured with the plug seated**, at the connector, so the row proves
   the *assembled* path. Isolation and continuity therefore run as two passes over the same
   connector, not one.

---

# Staged gates

Run in order. Each gate is a build step plus its verification. Do not proceed to the next
gate with an unresolved failure — a §13 hard stop halts everything.

## S1 — Battery divider, isolated (before the UBECs exist on the batt+ node)

> **⚠ This gate is only valid here.** All three expected values assume the divider is the
> **only** thing on the battery+ node. Once the UBECs are attached (§S6) you are measuring
> 37 kΩ in parallel with two switching-regulator input stages — input caps charging, plus
> whatever internal path the switcher and any reverse-protection diode present. The composite
> is unpredictable and **will not read 37 kΩ**. Do not re-run S1 after S6 and do not treat a
> post-S6 reading as a failure.

The 100 nF cap makes the ohmmeter reading drift upward for a second while it charges — wait
for it to settle. GPIO34 is input-only/high-impedance, so the ESP32 barely loads these
readings.

| # | Measure | Expected | Hard-stop if |
|---|---|---|---|
| D1 | Divider tap (GPIO34) → GND | **≈ 10 kΩ** (±5%) | ≈0 Ω (short) or open |
| D2 | Tap → battery+ input lead | **≈ 27 kΩ** | ≈20 kΩ → the **old 20k/10k design got built — stop** (clips the top of 2S range) |
| D3 | Battery+ input lead → GND | **≈ 37 kΩ** | anything ≪ 37 kΩ (leak to GND before the divider) |

Reminder from A1.6: the Wokwi run **did not validate the battery ADC** (the sim pot behaved
oddly at boot) — this resistance check is necessary but the volts-per-count calibration is
still a Phase B/C task with a real multimeter reading vs the console `batt` value.

## S2 — Hall sensor, isolated

| # | Measure | Expected | Note |
|---|---|---|---|
| H1 | GPIO35 → **3V3 pin** | **≈ 10 kΩ** (the pull-up) | Hard-stop if it reads to the **5 V** rail instead — that would put 5 V on an input-only pin with no protection |
| H2 | GPIO35 → GND | **not a short** | open-collector output; open/OL or high/diode-ish through the sensor is fine; ≈0 Ω is a fail |
| H3 | A3144 VCC lead → the **rail-A harness node** | beep | it's a 5 V part; pre-S6 this proves *which wire it lands on*, not a voltage |
| H4 | A3144 VCC lead → 3V3 | **no beep** | |
| H5 | Optional 1–10 nF Hall RC (D8 note / R18) | present and to GND, **or** recorded as not populated | "add only if the bench scope shows double-counts" — either state is a valid PASS, but it must be *recorded*, not left blank |

## S3 — link2 pair (board #1 ↔ board #2)

Continuity beeper, both ESP32s unpowered.

| # | Signal | GPIO | Far end | Expect |
|---|---|---|---|---|
| C3 | link2 TX | **25** (#1) | board #2 **GPIO16** | beep; no beep to 26 |
| C4 | link2 RX | **26** (#1) | — | **N/A — verify NO wire is present.** Expect **no beep** from GPIO26 to anything on board #2 |

> **C4 is settled, not "per your build."** link2 is one-way by design and the firmware proves
> it: the HAL is constructed TX-only (`Esp32Link2Uart link2Uart(pinmap::kBoard2UartTxPin)`)
> and RX is explicitly disabled — `Serial1.begin(115200, SERIAL_8N1, /*rxPin=*/-1, txPin_)`.
> `kBoard2UartRxPin = 26` is declared and unused. **Do not wire it.** The row is falsifiable
> as written: a beep here is a *failure*, because it means a wire exists that shouldn't.

## S4 — CRSF pair and each 3-pin actuator lead, individually

Build and verify one lead at a time. For each, two passes per §3: continuity **plug seated**,
then isolation **ESP32 unseated** (or resistance-mode per §3 rule 2).

| # | Signal | GPIO | Far end | Expect |
|---|---|---|---|---|
| C1 | CRSF in | **16** (RX2) | RP1 **TX** pad | beep; no beep to 17 |
| C2 | CRSF telemetry out | **17** (TX2) | RP1 **RX** pad | beep; no beep to 16 |
| C5 | Steering signal | **13** | steering servo plug signal pin | beep |
| C6 | ESC signal | **14** | ESC servo-lead signal (white) pin | beep |
| C7 | DRS signal | **18** | DRS servo plug signal | beep; no beep to 19 |
| C8 | Gimbal pan | **19** | pan servo plug signal | beep; no beep to 18/23 |
| C9 | Gimbal tilt | **23** | tilt servo plug signal | beep |
| C10 | Battery sense | **34** | divider tap | beep |
| C11 | Hall out | **35** | A3144 output pin | beep |

### S4b — cross-signal isolation (all five actuator leads present, UBECs still off)

| # | Check | Expect |
|---|---|---|
| S1r | **Signal isolation matrix** — 13/14/18/19/23 against each other | **no beep between any pair** (no shared/bridged signals) |
| S2r | Each signal (13/14/18/19/23/34/35) → rail-A/rail-B wiring and → GND | **no beep to either** (per §3 rule 2 — unseat the ESP32) |
| S3r | Each 3-pin lead, at the connector: signal↔+5 V within that lead | **no beep** |
| S4r | Each 3-pin lead, at the connector: GND pin → harness ground | beep |

Repeat S3r/S4r deliberately — a reversed 3-pin plug is the single most common crash-cause on
a first bring-up, and it is invisible once the shell is on.

## S5 — WS2812 path (board #2, R20/A2.4)

> **Supply option fixed to A (1N5819 diode) — decision 2026-07-30.** The fork in §8 of the
> old revision is closed. Rationale: the diode is on hand (office stock) and no 74AHCT125 is
> in the inventory or BOM v2, so option B would need ordering; the diode is the
> documented-standard fix; the strip is cassette-scale, not a metre of 144/m; and the failure
> mode is benign and immediately visible (wrong colours or first-LED flicker) on a
> **sound/light board with no control authority**. Recorded honestly: the diode works by
> dropping strip V<sub>DD</sub> to ~4.7 V so V<sub>IH</sub> (0.7 × V<sub>DD</sub>) falls to
> ~3.29 V, just under the ESP32's 3.3 V — a nominal margin of ~10 mV. It is reliable in
> practice because real WS2812B parts switch well below datasheet worst case, but it *is*
> marginal on paper and degrades with temperature and V<sub>DD</sub> sag along the strip.
> **74AHCT125 remains the documented fallback** if the strip misbehaves on the bench; adding
> it later does not touch board #1.

| # | Measure | Expected |
|---|---|---|
| W1 | ESP32 #2 **GPIO4** → strip DIN | **≈ 330 Ω** (the series resistor; a 0 Ω beep means the resistor got bypassed) |
| W2 | **Diode mode**, red probe on rail-A 5 V node, black on strip VDD | **≈ 0.15–0.35 V** (1N5819 Schottky forward) |
| W3 | Same, probes reversed | **OL** |
| W4 | 1N5819 band orientation | **toward strip VDD** (visual, §2) |
| W5 | 1000 µF across strip 5V/GND | ohms mode shows charging (rising R), **not** a persistent ≈0 Ω |

## S6 — Attach the UBECs

Build step, not a measurement gate. After this point:

- §S1 (D1–D3) is **no longer re-runnable** — see the S1 warning box.
- Rail A and rail B become real nodes.
- Re-run the full §2 visual list once, now that everything is together.

## S7 — Common ground, whole harness (A2.3)

One probe on battery −, beep/≤1 Ω to each:

- [ ] G1 ESP32 #1 GND pin
- [ ] G2 ESP32 #2 GND pin
- [ ] G3 ESC servo-lead GND
- [ ] G4–G7 Each servo connector GND (steering, DRS, pan, tilt)
- [ ] G8 RP1 receiver GND
- [ ] G9 UBEC A output GND and G10 UBEC B output GND
- [ ] G11 Camera / WiFi module GND — **or recorded as not yet wired**
- [ ] G12 WS2812 strip GND
- [ ] G13 MAX98357A GND

Any of these **not** common = hard stop (the link2 UART and CRSF both depend on it; a floating
ground makes UARTs "work sometimes," the worst failure mode).

## S8 — ESC BEC red-wire isolation (build-sheet fix #1 — **hard gate**)

Genuinely needs the harness together, which is why it lives here and not earlier.

- [ ] E1 ESC servo-lead **red** wire → 5 V rail A: **OPEN (no beep)**
- [ ] E2 ESC red → GPIO14 signal: **OPEN**
- [ ] E3 ESC red → rail B: **OPEN**
- [ ] E4 ESC servo-lead **GND** → common ground: **beep** (ground stays connected — only +5 V is isolated)
- [ ] E5 The cut/lifted red end is insulated (visual, §2)

Why hard: the QuicRun's ~6 V BEC back-feeding the UBEC rail damages the ESC and can over-volt
the rail.

---

## 10. Photo checklist (capture regardless of pass/fail — these are *reviewed*, see §12)

Photograph at the gate where the subassembly is still accessible, not all at the end.

1. Board #1 top + bottom (pin rows legible)
2. Board #2 top + bottom
3. ESC servo lead at board #1 — **the isolated red wire clearly visible**
4. Battery divider + 100 nF close-up *(at S1)*
5. Hall sensor wiring + pull-up close-up *(at S2)*
6. RP1 receiver wiring (16/17) *(at S4)*
7. All five actuator connectors seated, orientation visible *(at S4b)*
8. WS2812 data resistor + **diode band direction** + bulk cap *(at S5)*
9. The common-ground junction / harness *(at S7)*
10. Whole-bench overview (proves battery not connected)

## 11. Measurement table template

Record the gate each row was taken at — a row measured at the wrong gate is not valid
evidence.

```
| #  | Gate | Item                          | Expected        | Measured | P/F | Photo # |
|----|------|-------------------------------|-----------------|----------|-----|---------|
| D1 | S1   | GPIO34 tap -> GND             | ~10 kΩ          |          |     |         |
| D2 | S1   | tap -> batt+ lead             | ~27 kΩ          |          |     |         |
| D3 | S1   | batt+ lead -> GND             | ~37 kΩ          |          |     |         |
| H1 | S2   | GPIO35 -> 3V3                 | ~10 kΩ          |          |     |         |
| H2 | S2   | GPIO35 -> GND                 | not short       |          |     |         |
| H3 | S2   | A3144 VCC -> rail-A node      | beep            |          |     |         |
| H4 | S2   | A3144 VCC -> 3V3              | no beep         |          |     |         |
| H5 | S2   | Hall RC populated?            | present or N/A  |          |     |         |
| C3 | S3   | GPIO25 -> #2 GPIO16           | beep            |          |     |         |
| C4 | S3   | GPIO26 -> anything on #2      | NO beep (unwired)|         |     |         |
| C1 | S4   | GPIO16 -> RP1 TX              | beep            |          |     |         |
| C2 | S4   | GPIO17 -> RP1 RX              | beep            |          |     |         |
| C5 | S4   | GPIO13 -> steering signal     | beep            |          |     |         |
| C6 | S4   | GPIO14 -> ESC signal          | beep            |          |     |         |
| C7 | S4   | GPIO18 -> DRS signal          | beep            |          |     |         |
| C8 | S4   | GPIO19 -> pan signal          | beep            |          |     |         |
| C9 | S4   | GPIO23 -> tilt signal         | beep            |          |     |         |
| C10| S4   | GPIO34 -> divider tap         | beep            |          |     |         |
| C11| S4   | GPIO35 -> A3144 out           | beep            |          |     |         |
| S1r| S4b  | signal matrix 13/14/18/19/23  | all OPEN pairs  |          |     |         |
| S2r| S4b  | signals -> rails / GND        | all OPEN        |          |     |         |
| S3r| S4b  | per-lead signal <-> +5V       | all OPEN        |          |     |         |
| S4r| S4b  | per-lead GND -> common        | beep each       |          |     |         |
| W1 | S5   | GPIO4(#2) -> strip DIN        | ~330 Ω          |          |     |         |
| W2 | S5   | rail A -> strip VDD (diode)   | 0.15-0.35 V fwd |          |     |         |
| W3 | S5   | same, reversed                | OL              |          |     |         |
| W4 | S5   | 1N5819 band toward strip      | visual pass     |          |     |         |
| W5 | S5   | strip cap                     | charging, not 0 |          |     |         |
| G1..G13 | S7 | grounds (S7 list)           | beep each       |          |     |         |
| E1 | S8   | ESC red -> rail A             | OPEN            |          |     |         |
| E2 | S8   | ESC red -> GPIO14             | OPEN            |          |     |         |
| E3 | S8   | ESC red -> rail B             | OPEN            |          |     |         |
| E4 | S8   | ESC GND -> common GND         | beep            |          |     |         |
| E5 | S8   | red end insulated             | visual pass     |          |     |         |
```

## 12. PASS criteria — a **two-part** gate

A2 closes only when **both** parts below are satisfied. They attest different things and
neither substitutes for the other.

### Part 1 — Reviewer check (Claude Code session)

Scope, stated honestly because it bounds what this review is worth:

- [ ] **Completeness** — every row in §11 filled, none silently skipped; every conditional row (H5, G11) explicitly recorded as present *or* N/A rather than blank
- [ ] **Gate attribution** — each row taken at its listed gate, especially D1–D3 at S1 (a post-S6 divider reading is not valid evidence)
- [ ] **Arithmetic and tolerance** — divider within ±5 % of 10/27/37 kΩ, Hall pull-up ≈10 kΩ **to 3V3**, W1 ≈330 Ω, W2 in 0.15–0.35 V
- [ ] **Internal consistency** — no row contradicting another
- [ ] **Cross-reference** against `11_hardware_validation_plan.md`
- [ ] **Direct inspection of the §10 photos** — solder bridges, connector orientation, cap stripe polarity, **1N5819 band direction**, and the isolated ESC red wire are all visually checkable from an image. This is the one part of the review that is independent observation rather than trust in the transcription, so it is mandatory, not optional.

**What this review cannot attest.** The reviewer cannot see the hardware. If a probe lands on
the wrong pin, a range is misread, or 37 kΩ is transcribed as 3.7 kΩ, there is no independent
signal — Part 1 validates that the *record* is complete, coherent, and photo-corroborated. It
does **not** and cannot certify that the car is safe.

### Part 2 — Owner attestation

- [ ] A signed one-line statement that you physically performed each §11 measurement on the
      actual assembly, at the gate recorded, with the meter in the stated mode.

**A2 closed** therefore means *"the no-power checklist is complete, self-consistent, and
photo-corroborated, and the owner attests the measurements are real."* It does **not** mean
*"the hardware is safe."* Opening Phase B is the owner's call on the owner's bench, informed
by A2 — not a verdict the reviewer is competent to issue.

Minor deviations (resistor tolerance, an unpopulated *optional* Hall RC) are PASS-with-note.
Anything in §13 = FAIL, full stop.

## 13. Hard stop conditions — do not proceed, do not power, report

1. **Any rail ↔ GND short** — a *persistent* ≈0 Ω. Per §3 rule 1, a reading that starts low
   and rises is a charging capacitor, **not** a short
2. **ESC BEC red wire not isolated** (any continuity to a rail or signal)
3. **Divider wrong at S1** — top leg ≈20 kΩ (old design), tap shorted, or values far off.
   A post-S6 divider reading is not evidence either way
4. **Any GPIO with continuity to the battery+ line** (pre-divider) — instant board-killer at 8.4 V
5. **No common ground** between any two boards/devices in §S7
6. **Reversed polarity anywhere** (XT60, UBEC, cap, diode band)
7. **Uncertain connector orientation** — if you cannot positively identify a plug's signal/+/− order, stop and trace it; never "probably right"
8. GPIO35 pull-up found tied to 5 V instead of 3V3 (input-only pin, no clamp headroom)
9. **A wire present at GPIO26** (C4) — link2 RX is unwired by design; a beep means something got built that shouldn't exist

## 14. What Phase B needs (only after A2 is filled, reviewed, and approved)

Bench PSU or battery via the XT60 split (ESC **motor leads still disconnected**),
oscilloscope/logic analyzer (GPIO13/14 boot-float scope = B1.4/R04, PWM widths B1.3), the RP1
bound to the TX with **failsafe mode "No Pulses"**, `elrs-joystick-control` on the PC, and
flashing `esp32dev_tuning` — which is also the *first* moment the USB cable gets used. **None
of that is part of A2; Phase B stays blocked until A2 is reviewed and approved.**

Also still outstanding before the battery is trusted: the ZEEE pack's main lead has **not**
been re-terminated to XT60, and the XT90-S anti-spark master switch + XT60→XT90 adapter were
still in transit as of 2026-07-29 (`../HARDWARE_INVENTORY.md` §E). S7's "probe from battery −"
reference point depends on the XT60 termination existing.

## 15. What to paste back after actually running the checks

- the filled §11 measurement table (real readings + PASS/FAIL marks + gate + photo #),
- the photo set, identified — Part 1 of §12 **inspects** these, so an unlabelled dump is not sufficient,
- any PASS-with-note deviations,
- the exact reading + a photo for anything that hit a §13 hard stop,
- and the §12 Part 2 owner attestation line.

The reviewer then runs §12 Part 1 against `11_hardware_validation_plan.md`. A2 closes only on
a clean Part 1 **and** a recorded Part 2.
