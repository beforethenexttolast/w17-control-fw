# W17 Phase A A2 — Desk / No-Power Checklist

> **STATUS — NOT EXECUTED.** This document *defines how A2 will be performed*. A2 has **not
> been run**, there are **no measurements recorded**, and A2 is **NOT closed**. **Phase B
> remains blocked** until this checklist is filled in, pasted back, reviewed, and approved.
> (Prerequisite context: A1.1–A1.6 software/pre-power items are complete; A1.6 Wokwi sim
> passed — see `_verification_results.md`. A2 is the physical desk half of Phase A from
> `11_hardware_validation_plan.md`.)

**Golden rules for this session:** battery stays disconnected and out of reach. No USB, no
bench PSU, nothing flashed. Multimeter only. If any measurement is suspicious → **stop,
photograph, report** — don't "try again with power to see."

## 1. Tools

- Multimeter with continuity beeper, resistance, and **diode mode** (for the 1N5819 check)
- Fine probes or probe clips (DevKit pin headers are 2.54 mm)
- Good lighting + magnifier/phone-macro for solder joints
- Phone for the photo checklist (§10)
- USB cable and bench PSU: **have them, do not connect them** — both are Phase B
- The pin references: `lib/config/include/config/PinMap.hpp` (board #1 authority), soundlight `PinMap.hpp` (board #2), `docs/00_BUILD_SHEET.md` bench fixes

**Reference points used below:** "GND" = battery − wire / XT60 − pin (probe from there unless stated). "5 V rail A/B" = the two UBEC outputs (A = clean rail: camera, WiFi, both ESP32s, RP1, LEDs; B = servo rail: steering + 3× MG90S + blower).

## 2. Visual inspection (before any probing)

Work under strong light, top then bottom of every board:

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

## 3. Pin continuity (board #1) — the R08 closure

Continuity beeper, ESP32 **unpowered**. Probe the DevKit pin (silkscreen) ↔ the far end (connector pin / pad it should reach). Then an **adjacency check**: confirm NO continuity to each physical neighbor pin.

| # | Signal | GPIO | Far end | Expect |
|---|---|---|---|---|
| C1 | CRSF in | **16** (RX2) | RP1 **TX** pad | beep; no beep to 17 |
| C2 | CRSF telemetry out | **17** (TX2) | RP1 **RX** pad | beep; no beep to 16 |
| C3 | link2 TX | **25** | board #2 **GPIO16** | beep; no beep to 26 |
| C4 | link2 RX (optional/reserved) | **26** | board #2 GPIO17 (if wired at all) | per your build; no beep to 25 |
| C5 | Steering signal | **13** | steering servo plug signal pin | beep |
| C6 | ESC signal | **14** | ESC servo-lead signal (white) pin | beep |
| C7 | DRS signal | **18** | DRS servo plug signal | beep; no beep to 19 |
| C8 | Gimbal pan | **19** | pan servo plug signal | beep; no beep to 18/23 |
| C9 | Gimbal tilt | **23** | tilt servo plug signal | beep |
| C10 | Battery sense | **34** | divider tap | beep |
| C11 | Hall out | **35** | A3144 output pin | beep |
| C12 | **Signal isolation matrix** | 13/14/18/19/23 | each other | **no beep between any pair** (no shared/bridged signals) |
| C13 | Each signal (13/14/18/19/23/34/35) | → 5 V rails and → GND | **no beep to either** | |

Board #2 (only what crosses boards or feeds the strip): link2 wire → GPIO16 on #2; WS2812 data → GPIO4 on #2 *through the 330 Ω* (so expect ≈330 Ω, not a beep — see §8).

## 4. Battery divider (GPIO34) — resistance, battery disconnected

The 100 nF cap makes the ohmmeter reading **drift upward for a second** while it charges — wait for it to settle. GPIO34 is input-only/high-impedance, so the ESP32 barely loads these readings.

| Measure | Expected | Hard-stop if |
|---|---|---|
| Divider tap (GPIO34) → GND | **≈ 10 kΩ** (±5%) | ≈0 Ω (short) or open |
| Tap → battery+ input lead | **≈ 27 kΩ** | ≈20 kΩ → the **old 20k/10k design got built — stop** (clips the top of 2S range) |
| Battery+ input lead → GND | **≈ 37 kΩ** | anything ≪ 37 kΩ (leak to GND before the divider) |

Reminder from A1.6: the Wokwi run **did not validate the battery ADC** (the sim pot behaved oddly at boot) — this resistance check is necessary but the volts-per-count calibration is still a Phase B/C task with a real multimeter reading vs the console `batt` value.

## 5. Hall pull-up / cap (GPIO35)

- [ ] GPIO35 → **3V3 pin**: **≈ 10 kΩ** (the pull-up). Hard-stop if it reads to the **5 V** rail instead — that would put 5 V on an input-only pin with no protection.
- [ ] GPIO35 → GND: **not a short** (open-collector output; open/OL or high/diode-ish through the sensor is fine; ≈0 Ω is a fail).
- [ ] A3144 VCC pin → 5 V rail A: beep (it's a 5 V part). VCC → 3V3: no beep.
- [ ] If you populated the optional 1–10 nF Hall RC (D8 note): present and to GND; if not populated, note it — it's an "add only if the bench scope shows double-counts" part (R18).

## 6. ESC BEC red-wire isolation (build-sheet fix #1 — **hard gate**)

- [ ] ESC servo-lead **red** wire → ESP32 5 V rail A: **OPEN (no beep)**.
- [ ] ESC red → GPIO14 signal: **OPEN**.
- [ ] ESC red → rail B: **OPEN**.
- [ ] ESC servo-lead **GND** → common ground: **beep** (ground stays connected — only +5 V is isolated).
- The cut/lifted red end is insulated (visual, §2).

Why hard: the QuicRun's ~6 V BEC back-feeding the UBEC rail damages the ESC and can over-volt the rail.

## 7. Common ground (A2.3) — one probe on battery −, beep/≤1 Ω to each:

- [ ] ESP32 #1 GND pin
- [ ] ESP32 #2 GND pin
- [ ] ESC servo-lead GND
- [ ] Each servo connector GND (steering, DRS, pan, tilt)
- [ ] RP1 receiver GND
- [ ] UBEC A output GND and UBEC B output GND
- [ ] Camera / WiFi module GND (if already wired)
- [ ] WS2812 strip GND
- [ ] MAX98357A GND

Any of these **not** common = hard stop (the link2 UART and CRSF both depend on it; a floating ground makes UARTs "work sometimes," the worst failure mode).

## 8. WS2812 level path (board #2, R20/A2.4)

- [ ] Data: ESP32 #2 **GPIO4** → strip DIN: **≈ 330 Ω** (the series resistor; a 0 Ω beep means the resistor got bypassed).
- [ ] Supply option A (diode): **diode mode**, red probe on 5 V rail, black on strip VDD: **≈ 0.15–0.35 V** (1N5819 Schottky forward); reversed: OL. Band toward strip.
- [ ] Supply option B (74AHCT125): shifter present, VCC to 5 V, GND common, data routed through it — then the strip VDD connects directly to 5 V (no diode expected).
- [ ] 1000 µF across strip 5V/GND: ohms mode shows charging (rising R), **not** a persistent ≈0 Ω.

## 9. Servo/ESC signal sanity (repeat deliberately, it's the crash-preventer)

- [ ] No signal↔+5 V short on any of the five 3-pin headers (C13 covered it; re-verify at the *connector* side).
- [ ] No reversed 3-pin connector (visual §2 + this: with a plug seated, its signal pin must beep to the correct GPIO, its GND to common ground).
- [ ] No accidental shared signals (C12 matrix passed).
- [ ] Servo rail (B) 1000 µF cap present, correct polarity, not shorted (charging behavior, not 0 Ω).

## 10. Photo checklist (capture regardless of pass/fail)

1. Board #1 top + bottom (pin rows legible)
2. Board #2 top + bottom
3. ESC servo lead at board #1 — **the isolated red wire clearly visible**
4. Battery divider + 100 nF close-up
5. Hall sensor wiring + pull-up close-up
6. RP1 receiver wiring (16/17)
7. All five actuator connectors seated, orientation visible
8. WS2812 data resistor + diode/shifter + bulk cap
9. The common-ground junction / harness
10. Whole-bench overview (proves battery not connected)

## 11. Measurement table template

```
| # | Item                        | Expected        | Measured | P/F | Notes / photo # |
|---|-----------------------------|-----------------|----------|-----|-----------------|
| C1 | GPIO16 -> RP1 TX           | beep            |          |     |                 |
| ...all C-rows from §3...     |                 |          |     |                 |
| D1 | GPIO34 tap -> GND          | ~10 kΩ          |          |     |                 |
| D2 | tap -> batt+ lead          | ~27 kΩ          |          |     |                 |
| D3 | batt+ lead -> GND          | ~37 kΩ          |          |     |                 |
| H1 | GPIO35 -> 3V3              | ~10 kΩ          |          |     |                 |
| H2 | GPIO35 -> GND              | not short       |          |     |                 |
| H3 | A3144 VCC -> 5V rail A     | beep            |          |     |                 |
| E1 | ESC red -> 5V rail         | OPEN            |          |     |                 |
| E2 | ESC red -> GPIO14          | OPEN            |          |     |                 |
| E3 | ESC GND -> common GND      | beep            |          |     |                 |
| G1..G9 | grounds (§7 list)      | beep each       |          |     |                 |
| W1 | GPIO4(#2) -> strip DIN     | ~330 Ω          |          |     |                 |
| W2 | 5V -> strip VDD (diode)    | 0.15-0.35 V fwd |          |     |                 |
| W3 | strip cap                  | charging, not 0 |          |     |                 |
| S1 | signal matrix 13/14/18/19/23 | all OPEN pairs |         |     |                 |
| S2 | signals -> 5V/GND          | all OPEN        |          |     |                 |
```

## 12. PASS/FAIL criteria

**PASS** = every C/D/H/E/G/W/S row green: all mapped continuities beep, all isolation rows open, divider within ±5% of 10/27/37 kΩ, Hall pull-up ≈10 kΩ **to 3V3**, ESC red fully isolated, all grounds common, WS2812 path per build sheet. Minor deviations (resistor tolerance, an unpopulated *optional* Hall RC) are PASS-with-note. Anything in §13 = FAIL, full stop.

## 13. Hard stop conditions — do not proceed, do not power, report

1. **Any 5 V/3V3 rail ↔ GND short** (≈0 Ω)
2. **ESC BEC red wire not isolated** (any continuity to a rail or signal)
3. **Divider wrong** — top leg ≈20 kΩ (old design), tap shorted, or values far off
4. **Any GPIO with continuity to the battery+ line** (pre-divider) — instant board-killer at 8.4 V
5. **No common ground** between any two boards/devices in §7
6. **Reversed polarity anywhere** (XT60, UBEC, cap, diode)
7. **Uncertain connector orientation** — if you cannot positively identify a plug's signal/+/− order, stop and trace it; never "probably right"
8. GPIO35 pull-up found tied to 5 V instead of 3V3 (input-only pin, no clamp headroom)

## 14. What Phase B needs (only after A2 is filled, reviewed, and approved)

Bench PSU or battery via the XT60 split (ESC **motor leads still disconnected**), oscilloscope/logic analyzer (GPIO13/14 boot-float scope = B1.4/R04, PWM widths B1.3), the RP1 bound to the TX with **failsafe mode "No Pulses"**, `elrs-joystick-control` on the PC, and flashing `esp32dev_tuning` — which is also the *first* moment the USB cable gets used. **None of that is part of A2; Phase B stays blocked until A2 is reviewed and approved.**

## 15. What to paste back after actually running the checks

- the filled §11 measurement table (real readings + PASS/FAIL marks),
- the photo set (or a note of which photo shows what),
- any PASS-with-note deviations,
- and the exact reading + a photo for anything that hit a §13 hard stop.

The reviewer then checks it against `11_hardware_validation_plan.md`; only on a clean review is A2 closed and Phase B gated open.
