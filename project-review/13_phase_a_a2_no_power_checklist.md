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
> and only the late gates are true whole-harness gates. This localizes a fault to the
> subassembly you just built instead of hiding it among ~40 joints, and it means no gate is
> ever read against an invalid expected value. The cost is real: A2 is no longer an
> afternoon with a multimeter, it is interleaved with build week.
>
> This restructure also **absorbs what would otherwise be a separate assembly gate.** There
> is no A1.7: the build steps are the S-gates below.

> **REVISED 2026-08-04 — closing the adversarial review
> (`14_a2_staged_gates_adversarial_review.md`).** The 2026-08-03 review found the
> restructure had been executed as a *subtraction*: the old whole-harness screens were
> correctly invalidated but never re-issued in staged form. §13 hard stops 1 and 4 had no
> generating measurement anywhere; S8 was unexecutable as sequenced and its electrical rows
> unfalsifiable as built; S7's reference point did not exist on hand; and the gate order
> contradicted the PDB soldering guide. This revision closes those findings:
>
> - **SF (PDB frame)** opens the sequence — the nodes every later gate probes are now built
>   and verified before they are consumed (F8), and the pre-S6 rail isolation rows live
>   there (F2). *(Proposed as "S0"; renamed **SF** on 2026-08-04 — see the naming note below.)*
> - **S7 is now the whole-harness composite gate**: grounds, the post-S6 batt+→GND and
>   rail→GND screens (F1/F2), the mated master-switch pigtail chain (F1), and the ESC
>   power-feed rows (F13.6). Its reference point is **harness-side** — no battery (F5).
> - **S8 is split into S8a (at the cut — executes during S4, before insulation) and S8b
>   (whole harness)**, with rows that can actually fail (F4).
> - **§13 carries an explicit stop → generating-row map** — every hard stop is reachable
>   from a measurement, checked stop by stop (F1/F3).
> - **§3 gains rule 4**: every pre-S6 row is single-shot evidence (F14).
> - Four **owner decisions 2026-08-03** are folded in: the IP2326 charger is **not fitted
>   during A2 build week** and taps the **pack side** of the XT90-S (F9a/F9b); the Hall
>   pull-up lives **at the ESP32 end** (F11); the boards are **socketed** — with an owed
>   caliper verification, see §3 rule 2 (F12).
>
> Gate order is now **SF → S1 → S2 → S3 → S4/S4b (S8a executes here) → S5 → S6 → S7 → S8b**.
> Finding-by-finding edit map: review doc §"Revision pass 2026-08-04". **A2 remains
> NOT-EXECUTED — this revision makes it executable, not executed.**

> **CLOSURE PASS 2026-08-04 (later) — F15/F16 closed, F17/F18 recorded, the `S0` name
> collision settled.** The revision above generated two findings of its own (F15, F16); both
> are now closed, and closing them generated two more, recorded rather than quietly fixed:
> **F17** (F15's own exceptions list omits the 13↔14 pair, which reads ≈2× the fitted
> pull-down in resistance mode → false FAIL at S1r) and **F18** (PD1's pull-downs have no
> stated location and no fit moment — the F16 shape, one row over). Both are closed here; the
> register carries all four. Also: photo item 14 (C1 at S6 — the one PDB electrolytic with no
> frame) and S1r's annotation.
>
> **Naming — there is no gate S0.** The frame gate is **SF**. The token `S0` in this project
> means one thing only: the **ZK cassette clearance `S0 ≥ 9.82 mm`** (§3 rule 2, §SF). The
> frame gate was proposed as "S0" by review finding F8 and renamed on 2026-08-04, before
> anything was published, because two live `S0`s in one build week is how a commit subject
> ends up reading as if it closed a gate it never touched. The review register's F8/F2/F12
> **finding bodies keep the original wording as written history**; every pointer into *this*
> document says SF. Row IDs are unchanged (P1–P7).

**Golden rules for this session:** battery stays disconnected and out of reach. No USB, no
bench PSU, nothing flashed. Multimeter only. If any measurement is suspicious → **stop,
photograph, report** — don't "try again with power to see."

## 1. Tools

- Multimeter with continuity beeper, resistance, and **diode mode**
- Fine probes or probe clips (ESP32 board headers are 2.54 mm; the build boards are **MH-ET
  Live D1-Minis** — the DevKit V1 clones are TEST/SPARE, owner decision 2026-07-24)
- Good lighting + magnifier/phone-macro for solder joints
- Phone for the photo checklist (§10)
- USB cable and bench PSU: **have them, do not connect them** — both are Phase B
- The pin references: `lib/config/include/config/PinMap.hpp` (board #1 authority), soundlight `PinMap.hpp` (board #2), `docs/00_BUILD_SHEET.md` bench fixes

> **Diode mode is load-bearing, not optional.** The WS2812 supply decision is now fixed to
> the 1N5819 (§S5, decision 2026-07-30), and the forward-voltage reading is the *only* check
> that proves the diode is both present and correctly oriented. If your meter lacks diode
> mode, stop and say so — the substitute is a reverse/forward **resistance** asymmetry check,
> which is weaker and needs writing before S5 runs.

**Reference points used below (redefined 2026-08-04, F5):** **"GND" = the star-ground node,
probed at the PDB input XT60 male − pin.** This is harness-side — it exists from SF onward,
needs no battery, and no in-envelope pack exists anyway (`../../HARDWARE_INVENTORY.md` §E;
the golden rule keeps any pack away from this bench regardless). **"batt+" = the switched
battery + node, probed at the PDB input XT60 male + pin.** "5 V rail A/B" = the two UBEC
outputs (A = clean rail: camera, WiFi, both ESP32s, RP1, LEDs; B = servo rail: steering +
3× MG90S + blower). **Before §S6 the rails exist only as harness wiring, not as energized
nodes** — rail rows before S6 verify *where a wire goes*, not a voltage.

## 2. Visual inspection (runs per-gate, plus once at the end)

Do the relevant subset at each S-gate on the subassembly in front of you, then repeat the
whole list once after §S6. Work under strong light, top then bottom of every board:

- [ ] **Solder bridges** — every hand-soldered joint on both ESP32 sockets and headers, the divider, the Hall wiring, WS2812 connections. ⚠ **Adjacency placeholder (F12):** the old call-outs (16/17, 25/26, 18/19) were derived from the **DevKit V1** single-row layout; the build boards are **MH-ET Live D1-Minis** and the true adjacent-pair list is **owed from the MH-ET silkscreen** — a bench job with the board in hand, due **before SF's socket soldering**. Until it is done, beeper-check and inspect **every** header joint, not a favored subset.
- [ ] **Polarity** — XT60 orientation; UBEC in/out polarity; **ESC 12 AWG power input: + to batt+, − to star** (F13.6 — a reversed ESC power input is destroyed at first connect; S7's PW rows measure it); electrolytic caps (1000 µF servo-rail, 1000 µF WS2812) stripe = negative; 1N5819 band toward the strip VDD side
- [ ] **Connector orientation** — every 3-pin servo plug: signal–+5–GND order matches its header (MG90S lead: orange=signal, red=+, brown=−; DS3235SG same scheme). Mark verified plugs with a paint dot
- [ ] **Loose wires / strain relief** — tug-test every crimp and solder joint gently
- [ ] **Mechanical shorts** — no bare wire near the ESP32 pin rows, no board standoff touching traces, nothing conductive loose in the tub
- [ ] **Heat-shrink/insulation** — divider resistors, Hall splice, the isolated ESC red wire (must be individually insulated, not just folded back bare — and insulated **only after §S8a's rows and photos are taken**)
- [ ] **ESC/BEC wiring** — ESC 3-wire servo lead at board #1: **red visibly not connected** (cut at S8a, both ends insulated); ESC sensor cable seated; ESC set/labelled sensored
- [ ] **Battery divider** — 27 kΩ from batt+ side, 10 kΩ to GND, tap to GPIO34. **C3 (100 nF) lives at the GPIO34 pin end, not at the divider** (F13.3 — guide §4 wins; verified at S4, not S1)
- [ ] **Hall sensor** — A3144: VCC to **5 V**, GND common, output to GPIO35 with **10 kΩ pull-up to 3.3 V at the ESP32 #1 end** (owner decision 2026-08-03/F11 — the sensor lead has no 3V3 conductor, so a sensor-side pull-up is unbuildable and invites exactly §13 stop 8). Pull-up to 3V3, *not* 5 V — check this specifically; H1/H1b measure it
- [ ] **Servo/ESC signal wiring** — five signal wires route to GPIO13 (steer), 14 (ESC), 18 (DRS), 19 (pan), 23 (tilt); none to strapping pins 0/2/12/15
- [ ] **Boot-float pull-downs (if fitted)** — GPIO13 and GPIO14 only, **harness side at the ESP32 #1 socket positions**, each to the star ground, fitted at §S4 with the board-end signal wiring (F18). Not fitted is equally valid — PD1 records which, and §2 has nothing to inspect in that case

## 3. Measurement conventions (read once, applies to every gate below)

Four rules that resolve ambiguities the single-pass version left open:

1. **Capacitor charging is not a short.** Any resistance reading across a node with a cap on
   it (100 nF at the GPIO34 pin, 1000 µF servo rail, 1000 µF WS2812) **starts low and rises**
   while the meter charges it. Wait for it to settle. A *persistent* ≈0 Ω is a fail; a rising
   reading is not. This applies to §13 hard-stop 1 as much as anywhere else — and it applies
   in **diode mode** too where a cap sits behind the junction: W2's forward reading must
   charge the 1000 µF through the 1N5819 before it settles (F14).
2. **Isolation rows are measured with the ESP32 unseated.** The boards are **SOCKETED** —
   female headers on the PDB, boards lift out (owner decision 2026-08-03/F12), so unseating
   is always available. ⚠ **That decision carries an owed verification:** the socket stack
   height has never been calipered against the ZK study's cassette clearance **`S0` ≥
   9.82 mm** (the *only* thing `S0` names in this project — the frame gate is **SF**). If the
   measurement breaks the clearance, the socketing decision **reopens**, the boards go
   hard-wired, and every isolation row in this document falls back to the resistance-mode
   variant — so do the caliper check **before SF's first socket joint**. Rationale for
   unseating: an installed ESP32 presents internal ESD protection diodes from every GPIO to GND and 3V3, which can
   read as a low resistance and produce a false failure. If a board ends up hard-wired, use
   **resistance mode**, do not use the beeper for that row, and record the actual value.
   Resistance-mode expectations are ≫ 10 kΩ **except the documented exceptions** — a blanket
   "≫ 10 kΩ everywhere" would manufacture false FAILs on a correctly built board:
   GPIO34 → GND **≈ 10 kΩ** (divider bottom leg — F13.1); GPIO34 → batt+ **≈ 27 kΩ** (top
   leg — F3); GPIO35 → 3V3 **≈ 10 kΩ** (Hall pull-up — H1); GPIO13/14 → GND **= the fitted
   pull-down value** if PD1 recorded pull-downs populated (F15); **GPIO13 ↔ GPIO14 ≈ 2× the
   fitted pull-down value** in that same case (F17 — both legs return to the star node, so
   the pair reads through them in series; e.g. two 4.7 kΩ pull-downs put 13↔14 at ≈ 9.4 kΩ,
   which a strict "≫ 10 kΩ" reading calls a bridged pair on a correctly built board).
   **The exceptions list is closed by construction, not by memory:** it must name every pair
   this document itself legitimizes a non-open reading on. If a later revision adds a
   deliberate resistance anywhere in the signal set, it belongs here in the same edit — that
   omission is the defect F13.1, F15 and F17 each are.
3. **Continuity rows are measured with the plug seated**, at the connector, so the row proves
   the *assembled* path. Isolation and continuity therefore run as two passes over the same
   connector, not one.
4. **Every pre-S6 row is single-shot evidence, valid only at its own gate** (F14). After S6,
   only §S7 and §S8b rows — the composite screens — are meaningful; a pre-S6 row re-measured
   on the composite harness is not evidence either way, pass *or* fail. The S1 warning box,
   the S5 W2/W3 note, and the S4b S6r/S7r notes are instances of this rule, not the only
   exceptions anyone happened to think of.

---

# Staged gates

Run in numeric order. The one deliberate exception is **§S8a, which executes during §S4's
ESC-lead step** — its rows are only probeable at the moment the red wire is cut, before the
mandated insulation goes on (F4). Each gate is a build step plus its verification. Do not
proceed to the next gate with an unresolved failure — a §13 hard stop halts everything.

## SF — PDB frame (added 2026-08-04 — F8; home of the pre-S6 rail rows — F2)

> **SF, not S0.** Proposed as gate "S0" by F8 and renamed 2026-08-04 so that `S0` keeps a
> single meaning in this project — the ZK cassette clearance below. Row IDs stay P1–P7.

**Before the first joint, two owed bench measurements** (both no-power, both with parts on
hand). **Neither is closed by this document, and neither may be closed on paper:**

1. **Caliper the female-header socket stack** against the ZK cassette clearance `S0` ≥
   9.82 mm — the socketing decision is conditional on it (§3 rule 2). **OWED.**
2. **Derive the adjacent-pair list from the MH-ET silkscreen** (§2 placeholder). **OWED.**

Build: the XT60 input connector, the **star-ground node**, both ESP32 female header sockets
(boards **not** seated), the rail A/B output headers, and the rail branch looms. This is
the frame every later gate probes against — the old revision consumed these nodes (H3's
"rail-A harness node", S4r's "harness ground") without ever sequencing or verifying their
construction.

**C1 (1000 µF, rail B) is NOT fitted here — it goes on at §S6**, so every pre-S6 rail
reading below is a clean OPEN, not a charging transient (F16). The IP2326 charger is not
fitted anywhere in A2 (§14).

| # | Measure | Expected | Note |
|---|---|---|---|
| P1 | Star node ↔ XT60 input − pin | **beep** | the ground reference every later gate uses |
| P2 | batt+ (XT60 + pin) ↔ GND | **OPEN** | nothing lives on batt+ yet; a beep or low Ω = §13 stop 1 |
| P3 | rail-A wiring ↔ rail-B wiring | **no beep** | bridged rails = two switching UBEC outputs hard-paralleled at first power (F2) |
| P4 | rail-A wiring ↔ batt+ | **no beep** | a rail loom landed on raw batt+ = 8.4 V into every 5 V load on that rail (F2) |
| P5 | rail-B wiring ↔ batt+ | **no beep** | same |
| P6 | rail-A wiring ↔ GND | **no beep** | |
| P7 | rail-B wiring ↔ GND | **no beep** | C1 is absent by construction (F16) — this must be a clean OPEN here |

Plus the §2 visual subset — the ESP32 sockets are the densest hand-soldering on the PDB.
P2–P7 are single-shot (§3 rule 4): §S4b's S6r/S7r re-screen rail↔rail and rails↔batt+ once
more just before S6; after S6, none of these pairs are cleanly measurable again.

## S1 — Battery divider, isolated (before the UBECs exist on the batt+ node)

> **⚠ This gate is only valid here.** All three expected values assume the divider is the
> **only** thing on the battery+ node. Once the UBECs are attached (§S6) you are measuring
> 37 kΩ in parallel with two switching-regulator input stages — input caps charging, plus
> whatever internal path the switcher and any reverse-protection diode present. The composite
> is unpredictable and **will not read 37 kΩ**. Do not re-run S1 after S6 and do not treat a
> post-S6 reading as a failure. (This box is an instance of §3 rule 4, not an exception.)

The divider is bare at this gate: **C3 (100 nF) lives at the GPIO34 pin end and is not in
circuit yet** (F13.3 — the old "wait for the cap to charge" preamble described a cap that
isn't built until S4), so D1–D3 settle immediately; any slow drift means something
unexpected is on the node. §SF must already have passed — P2 proved batt+ ↔ GND OPEN before
the divider existed, so D3's ≈37 kΩ is the first legitimate non-open reading on that pair.

| # | Measure | Expected | Hard-stop if |
|---|---|---|---|
| D1 | Divider tap (GPIO34) → GND | **≈ 10 kΩ** (±5%) | ≈0 Ω (short) or open |
| D2 | Tap → battery+ input lead | **≈ 27 kΩ** | ≈20 kΩ → the **old 20k/10k design got built — stop** (clips the top of 2S range) |
| D3 | Battery+ input lead → GND | **≈ 37 kΩ** | anything ≪ 37 kΩ (leak to GND before the divider) |

Reminder from A1.6: the Wokwi run **did not validate the battery ADC** (the sim pot behaved
oddly at boot) — this resistance check is necessary but the volts-per-count calibration is
still a Phase B/C task with a real multimeter reading vs the console `batt` value.

## S2 — Hall sensor, isolated

**The 10 kΩ pull-up lives at the ESP32 #1 end** — wired on the harness/socket side between
the GPIO35 header position and the 3V3 header position (owner decision 2026-08-03/F11).
The sensor lead stays a standard 3-wire run (sig / +5 V / GND): it has no 3.3 V conductor,
so a sensor-side pull-up was unbuildable as documented, and the "obvious" improvisation —
pulling up to the +5 that *is* there — is verbatim §13 stop 8 on an input-only pin. Build
the board-end pull-up and the sensor lead in this gate.

| # | Measure | Expected | Note |
|---|---|---|---|
| H1 | GPIO35 → **3V3 pin** | **≈ 10 kΩ** (the pull-up) | Hard-stop if it reads to the **5 V** rail instead — that would put 5 V on an input-only pin with no protection |
| H1b | GPIO35 → **rail-A / 5 V wiring** | **no beep** (resistance mode: ≫ 10 kΩ) | **the generating row for §13 stop 8** — it previously had none (F11). A beep or ≈10 kΩ here means the pull-up landed on 5 V |
| H2 | GPIO35 → GND | **not a short** | open-collector output; open/OL or high/diode-ish through the sensor is fine; ≈0 Ω is a fail |
| H3 | A3144 VCC lead → the **rail-A harness node** | beep | it's a 5 V part; pre-S6 this proves *which wire it lands on*, not a voltage (rail-A loom exists from §SF) |
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
> (The link2 lead has no 5 V conductor, so its isolation coverage is GPIO25's membership in
> S2r/S5r — F6.)

## S4 — CRSF pair and each 3-pin actuator lead, individually

Build and verify one lead at a time. For each, two passes per §3: continuity **plug seated**,
then isolation **ESP32 unseated** (or resistance-mode per §3 rule 2).

**The ESC red-wire cut and gate §S8a execute during this gate's ESC-lead step** — cut, run
S8a's rows and photos on the bare ends, *then* insulate (F4). Do not build the ESC lead and
defer the cut.

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

**Divider tap follow-through (F13.3):** the board-end tap wiring built in this gate is where
**C3 (100 nF)** finally enters the circuit — fit it at the GPIO34 pin end per guide §4, then:

| # | Measure | Expected | Note |
|---|---|---|---|
| C10b | Tap node at the GPIO34 pin end → GND | **≈ 10 kΩ** after a brief settle (§3 rule 1) | proves the *assembled* tap path still reads the divider; persistent ≈0 Ω = §13 stop 3. The reading alone cannot prove C3 exists — its **presence** is §2 visual + photo (§10 item 12) |

**CRSF-lead internal isolation (F6)** — at the JST-XH connector. The lead carries rail-A
5 V one crimp position from both UART pins; the S4b rationale ("a reversed plug is the
single most common crash-cause") applies here with more force, because a 5V↔TX whisker
kills the failsafe's input path at the moment it is first trusted:

| # | Measure | Expected |
|---|---|---|
| K1 | GPIO16 ↔ lead 5 V pin | **no beep** |
| K2 | GPIO17 ↔ lead 5 V pin | **no beep** |
| K3 | GPIO16 ↔ lead GND pin | **no beep** |
| K4 | GPIO17 ↔ lead GND pin | **no beep** |

**Boot-float pull-downs (old A2.5 / R04 — F7):** the two safety-critical outputs, recorded
either way per the H5 pattern.

**Where they live and when they go on (decided 2026-08-04 — F18; neither document said
before).** *If* fitted, each pull-down lives **harness side, at the ESP32 #1 socket
position** for GPIO13 / GPIO14, returning to the star ground — the same placement rule the
Hall pull-up (F11) and C3 (F13.3) already follow, and it is fitted **here, at §S4, with the
board-end signal wiring**, before §S4b's matrices read it. Two reasons this placement is not
arbitrary: a harness-side pull-down is **present with the board unseated**, which is the only
reason §3 rule 2's exception ("13/14 → GND = the fitted value") is measurable at all — a
board-side pull-down would vanish on unseating and make PD1 and S2r contradict each other on
a correctly built car; and it holds the ESC/steering line low even with **no board seated**,
which is exactly the R04 boot-float window the part exists for.

**The expected A2-time state is NOT POPULATED.** R04's evidence is a Phase-B scope (B1.4) that
has not run, so "fit nothing yet, record that" is the honest default, not a lapse. Adding the
pull-downs after A2 invalidates no A2 row — but it does move 13/14 into §3 rule 2's exceptions
list, so a later re-measure reads the fitted value and the 13↔14 pair reads ≈2× it (F17), not
OPEN.

| # | Measure | Expected | Note |
|---|---|---|---|
| PD1 | GPIO13 / GPIO14 boot-float pull-down/RC | **populated** — measure each: ≈ the fitted value, signal node → GND, record it, **and record the placement** (harness-side at the socket position — anything else is a build deviation, note it) — **or explicitly recorded as not populated** | either is a PASS, **blank is not**. "Not populated" records that the R04 boot-float exposure on the ESC + steering lines was accepted deliberately, pending the B1.4 scope. If populated: S2r and §3 rule 2 read the fitted value on 13/14 → GND, and S1r reads ≈2× it on 13↔14 — those are the expected readings, not faults (F15, F17) |

### S4b — cross-signal isolation (all five actuator leads present, UBECs still off)

| # | Check | Expect |
|---|---|---|
| S1r | **Signal isolation matrix** — 13/14/18/19/23 against each other | **no beep between any pair** (no shared/bridged signals). Annotated exception (F17): if PD1 recorded pull-downs **populated**, **13 ↔ 14 reads ≈2× the fitted value** — both legs return to the star node, so the pair reads through them in series. That is the expected reading; the fault signature is a **beep / ≈0 Ω**, not a finite resistance. All other pairs stay open either way |
| S2r | Each signal (**13/14/16/17/18/19/23/25/34/35**) → rail-A/rail-B wiring and → GND | **no beep to either** (per §3 rule 2 — unseat the ESP32). Annotated exceptions per §3 rule 2: 34 → GND ≈ 10 kΩ in resistance mode (F13.1); 13/14 → GND = fitted pull-down value if PD1 populated (F15) |
| S3r | Each 3-pin lead, at the connector: signal↔+5 V within that lead | **no beep** |
| S4r | Each 3-pin lead, at the connector: GND pin → harness ground | beep (the star node exists from §SF) |
| S5r | Each signal (**13/14/16/17/18/19/23/25/35**) → **batt+ wiring** | **no beep** — §13 stop 4's generating row (F3). **GPIO34 → batt+: no beep in beeper mode; ≈ 27 kΩ in resistance mode** (the divider top leg — expected, not a fault); ≈0 Ω or a beep on any pin = §13 stop 4 |
| S6r | rail-A wiring ↔ rail-B wiring | **no beep** — last clean look before §S6 (F2; single-shot, §3 rule 4) |
| S7r | Each rail ↔ batt+ wiring | **no beep** — same (F2; single-shot, §3 rule 4) |

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
| W2 | **Diode mode**, red probe on rail-A 5 V node, black on strip VDD | **≈ 0.15–0.35 V** (1N5819 Schottky forward) — the reading charges the 1000 µF through the diode first; wait for it to settle (§3 rule 1) |
| W3 | Same, probes reversed | **OL** |
| W4 | 1N5819 band orientation | **toward strip VDD** (visual, §2) |
| W5 | 1000 µF across strip 5V/GND | ohms mode shows charging (rising R), **not** a persistent ≈0 Ω |

> **W2/W3 are single-shot (§3 rule 4, F14).** Post-S6, rail A ties to UBEC-A's unpowered
> output stage, which sits in parallel with the diode's forward path and may present an
> alternate reverse path. Do not re-run after S6 and do not treat a post-S6 reading as
> evidence either way.

## S6 — Attach the batt+ consumers: UBECs, ESC 12 AWG feed, C1

Build step, not a measurement gate. Everything that lives on the raw battery node goes on
here, **in one sitting**, so the clean-before / composite-after boundary stays a single
moment: both UBEC inputs, the ESC 12 AWG direct power feed, and **C1 (1000 µF) across rail
B** — C1 waits until now so every pre-S6 rail reading stayed a clean OPEN (F16).

**NOT fitted here or anywhere in A2: the IP2326 charger** (owner decision 2026-08-03/F9a).
The charge path is not built during A2 build week; it taps the **pack side** of the XT90-S
(F9b), lives off-PDB, and owns its own gate — see §14.

After this point:

- §S1 (D1–D3) is **no longer re-runnable** — see the S1 warning box / §3 rule 4.
- §S5 W2/W3 are **no longer re-runnable** (F14).
- §S4b S6r/S7r (rail↔rail, rails↔batt+) are **no longer re-runnable** — the measurement
  would read through unpowered UBEC internals, same class as D3: diode-ish and
  unpredictable (F2).
- Rail A and rail B become real nodes; rail B now carries C1, so every later rail-B
  resistance reading shows the §3 rule 1 charging signature.
- **Photograph C1 with its stripe visible** (§10 item 14) — no later measurement can tell a
  correctly oriented 1000 µF from a reversed one, so this shot plus the §2 visual is all the
  evidence §13 stop 6 gets for C1.
- Re-run the full §2 visual list once, now that everything is together.

## S7 — Whole-harness composite gate (A2.3 + the post-S6 screens)

**Reference point (redefined 2026-08-04, F5):** one probe stays on the **PDB input XT60
male − pin — the star-ground node**. Harness-side; exists since SF; needs no battery, and
none may be on this bench (golden rule).

### Grounds — beep / ≤ 1 Ω to each

- [ ] G1 ESP32 #1 GND pin
- [ ] G2 ESP32 #2 GND pin
- [ ] G3 ESC servo-lead GND
- [ ] G4–G7 Each servo connector GND (steering, DRS, pan, tilt)
- [ ] G8 RP1 receiver GND
- [ ] G9 UBEC A output GND and G10 UBEC B output GND
- [ ] G11 Camera / WiFi module GND — **or recorded as not yet wired**
- [ ] G12 WS2812 strip GND
- [ ] G13 MAX98357A GND
- [ ] G14 Blower connector GND — **or recorded as not yet wired** (rail-B load with a ground return; previously skipped — F13.2)

Any of these **not** common = hard stop (the link2 UART and CRSF both depend on it; a floating
ground makes UARTs "work sometimes," the worst failure mode).

### Battery-node and rail screens (F1/F2 — §13 stop 1's post-S6 generating rows)

The batt+ node acquired its highest-risk joints at S6 — two UBEC inputs, the ESC 12 AWG
feed — and these are the only rows that ever look at the assembled configuration:

| # | Measure | Expected | Note |
|---|---|---|---|
| M1 | batt+ (PDB input XT60 + pin) → GND, **ohms mode** | reading may start low and **rise** (UBEC + ESC input caps charging — §3 rule 1); **record the settled value, no numeric expectation** | a **persistent ≈0 Ω = §13 stop 1**. This is the row that catches the strand whisker soldered in at S6 — the exact false-PASS the review's F1 scenario walks into a wire fire |
| M2 | rail A → GND, **ohms mode** | settles to a recorded value (board/module decoupling may show a brief transient) | persistent ≈0 Ω = §13 stop 1 |
| M3 | rail B → GND, **ohms mode** | **a rising reading is the expected signature** — C1 (1000 µF) lives here | persistent ≈0 Ω = §13 stop 1 |

rail↔rail and rails↔batt+ are **not re-runnable here** (§S6 list); their evidence is
SF/S4b, single-shot per §3 rule 4.

### Master-switch pigtail chain + ESC power feed (F1 / F13.6)

Mate the full pigtail chain to the PDB input — pack XT60-female end left dangling, **no
pack anywhere**: (a) XT90-S-female→XT60-male + (b) XT90H-male with the **owner-made
XT60-female tail** (`../../HARDWARE_INVENTORY.md` §E). These rows test the owner-made joint
before it ever carries pack current:

| # | Measure | Expected | Note |
|---|---|---|---|
| CP1 | Pack-facing XT60 male **+** pin → PDB batt+ node | **beep** | pin-for-pin polarity through the whole chain, anti-spark half fully seated |
| CP2 | Pack-facing XT60 male **−** pin → star GND | **beep** | |
| CP3 | Pack-facing XT60 male **+** pin → star GND, ohms | **no beep; settles to ≈ the M1 value** (same composite, read through the chain) | persistent ≈0 Ω = §13 stop 1; a beep = polarity fault = §13 stop 6 |
| PW1 | ESC **+** power input (12 AWG) → PDB batt+ node | **beep** | F13.6 — a reversed ESC power input is destroyed at first connect; "never 'probably right'" (§13 stop 7) |
| PW2 | ESC **−** power input → star GND | **beep** | |

If the owner-made XT60-female tail is not yet made up when S7 runs, record CP1–CP3 as
**NOT-ASSEMBLED** (H5 pattern) — A2 may close with that note, but the rows become a **hard
precondition to the first pack connection** (§14), not an optional leftover.

## S8 — ESC BEC red-wire isolation (build-sheet fix #1 — **hard gate**, two moments)

The 2026-08-03 review (F4) found the single-moment S8 **unexecutable as sequenced** (both
cut ends are under §2-mandated heat-shrink by the time S8 arrives) and its electrical rows
**unfalsifiable as built** (the connector spec removes the +5 pin, so E1–E3 read OPEN
whether or not the cut was ever made — the rows passed on a car where the one thing they
exist to verify was skipped). The gate is therefore split: **S8a runs at the cut — during
§S4's ESC-lead step, before any insulation** — and S8b runs on the finished harness at the
old S8 position. The numbering stays with S8 because the two moments are halves of one hard
gate.

### S8a — at the cut (executes during §S4; before heat-shrink)

Cut the red (+5 V BEC) wire at the ESC's servo lead. **Both cut ends are bare and probeable
right now — this is the only moment that is true.** Every row probes the **ESC-side end** —
the end that becomes a live ~6 V BEC output when drivetrain power exists, the one that
matters (F4.4) — except E0, which uses both ends.

- [ ] E0 ESC-side red end ↔ connector-side red stub: **OPEN** — the row that falsifies "the
      cut was made"; an intact wire beeps (F4.2)
- [ ] E1 ESC-side red end → rail-A wiring: **OPEN** (the rail looms exist from §SF)
- [ ] E2 ESC-side red end → GPIO14 signal (white wire): **OPEN**
- [ ] E3 ESC-side red end → rail-B wiring: **OPEN**
- [ ] E6 ESC-side red end → common GND (star node, and the lead's own GND wire): **OPEN** —
      an insulation failure against ground dead-shorts the ESC's internal ~6 V BEC the
      moment drivetrain power exists, and no other row looks for it (F4.3)
- [ ] **Photo of the severed conductor before shrink**, and a second photo of the insulated
      result after (§10 item 3 — the pre-shrink shot is what makes the photo evidence
      rather than decoration, F10)

Then insulate both ends individually (§2). From this moment E0–E3/E6 are **no longer
re-runnable** — §3 rule 4.

### S8b — whole harness (the old S8 position)

- [ ] E4 ESC servo-lead **GND** → common ground: **beep** (ground stays connected — only
      +5 V is isolated)
- [ ] E5 The cut/lifted red end is insulated (visual, §2 — corroborated by the two S8a
      photos)
- [ ] E7 ESC header **+5 position** at board #1 → rail A / rail B / GPIO14 / GND: **all
      OPEN** — verifies the pin-removal defense from the side that is still probeable at
      S8b-time (F4). If the +5 position carries **no metal at all** (pin clipped / contact
      never populated), record **"no metal in position"** as the PASS, with a photo (§10
      item 13) — do not leave the row blank, and do not probe empty air and log it as OPEN

Why hard: the QuicRun's ~6 V BEC back-feeding the UBEC rail damages the ESC and can over-volt
the rail.

---

## 10. Photo checklist (capture regardless of pass/fail — these are *reviewed*, see §12)

Photograph at the gate where the subassembly is still accessible, not all at the end.

1. Board #1 top + bottom (pin rows legible)
2. Board #2 top + bottom
3. ESC servo lead red-wire cut — **two shots, both at §S8a: the severed conductor before
   shrink, and the insulated result after** (retagged per F10 — an after-shrink photo alone
   is indistinguishable from an intact wire under a sleeve)
4. Battery divider close-up *(at S1 — no 100 nF in this frame; C3 is at the pin end, F13.3)*
5. Hall sensor wiring + **board-end pull-up** close-up *(at S2)*
6. RP1 receiver wiring (16/17) *(at S4)*
7. All five actuator connectors seated, orientation visible *(at S4b)*
8. WS2812 data resistor + **diode band direction** + bulk cap *(at S5)*
9. The common-ground junction / harness *(at S7)* — corroborates only that **a junction
   exists**; the S7 G-row beeps are the sole evidence for topology (F10)
10. Whole-bench overview (proves battery not connected)
11. PDB frame: XT60 input, star node, ESP32 sockets, output headers *(at SF)*
12. C3 (100 nF) at the GPIO34 pin end *(at S4)*
13. ESC header +5 position at board #1 *(at S8b — feeds E7 either way: metal or no metal)*
14. **C1 (1000 µF) across rail B, stripe visible** *(at S6, immediately after it goes on —
    F16's fit moment is what makes a gate-tagged shot possible)*. C1 is the only electrolytic
    on the PDB and it had no frame in this list: §12 Part 1 claims cap stripe polarity is
    photo-checkable, §13 stop 6 names reversed caps, and **no no-power measurement
    distinguishes a correctly oriented 1000 µF from a reversed one** — M3 reads a charging
    cap either way. The photo and the §2 visual are the whole of stop 6's evidence for C1

## 11. Measurement table template

Record the gate each row was taken at — a row measured at the wrong gate is not valid
evidence.

```
| #   | Gate | Item                            | Expected            | Measured | P/F | Photo # |
|-----|------|---------------------------------|---------------------|----------|-----|---------|
| P1  | SF   | star node <-> XT60 - pin        | beep                |          |     |         |
| P2  | SF   | batt+ <-> GND                   | OPEN                |          |     |         |
| P3  | SF   | rail-A wiring <-> rail-B wiring | no beep             |          |     |         |
| P4  | SF   | rail-A wiring <-> batt+         | no beep             |          |     |         |
| P5  | SF   | rail-B wiring <-> batt+         | no beep             |          |     |         |
| P6  | SF   | rail-A wiring <-> GND           | no beep             |          |     |         |
| P7  | SF   | rail-B wiring <-> GND           | no beep (C1 absent) |          |     |         |
| D1  | S1   | GPIO34 tap -> GND               | ~10 kΩ              |          |     |         |
| D2  | S1   | tap -> batt+ lead               | ~27 kΩ              |          |     |         |
| D3  | S1   | batt+ lead -> GND               | ~37 kΩ              |          |     |         |
| H1  | S2   | GPIO35 -> 3V3                   | ~10 kΩ              |          |     |         |
| H1b | S2   | GPIO35 -> rail-A/5V wiring      | no beep / >>10 kΩ   |          |     |         |
| H2  | S2   | GPIO35 -> GND                   | not short           |          |     |         |
| H3  | S2   | A3144 VCC -> rail-A node        | beep                |          |     |         |
| H4  | S2   | A3144 VCC -> 3V3                | no beep             |          |     |         |
| H5  | S2   | Hall RC populated?              | present or N/A      |          |     |         |
| C3  | S3   | GPIO25 -> #2 GPIO16             | beep                |          |     |         |
| C4  | S3   | GPIO26 -> anything on #2        | NO beep (unwired)   |          |     |         |
| C1  | S4   | GPIO16 -> RP1 TX                | beep                |          |     |         |
| C2  | S4   | GPIO17 -> RP1 RX                | beep                |          |     |         |
| C5  | S4   | GPIO13 -> steering signal       | beep                |          |     |         |
| C6  | S4   | GPIO14 -> ESC signal            | beep                |          |     |         |
| C7  | S4   | GPIO18 -> DRS signal            | beep                |          |     |         |
| C8  | S4   | GPIO19 -> pan signal            | beep                |          |     |         |
| C9  | S4   | GPIO23 -> tilt signal           | beep                |          |     |         |
| C10 | S4   | GPIO34 -> divider tap           | beep                |          |     |         |
| C10b| S4   | tap at GPIO34 pin -> GND        | ~10 kΩ (settled)    |          |     |         |
| C11 | S4   | GPIO35 -> A3144 out             | beep                |          |     |         |
| K1  | S4   | GPIO16 <-> CRSF lead 5V pin     | no beep             |          |     |         |
| K2  | S4   | GPIO17 <-> CRSF lead 5V pin     | no beep             |          |     |         |
| K3  | S4   | GPIO16 <-> CRSF lead GND pin    | no beep             |          |     |         |
| K4  | S4   | GPIO17 <-> CRSF lead GND pin    | no beep             |          |     |         |
| PD1 | S4   | GPIO13/14 pull-downs            | value+where, or N/A |          |     |         |
| E0  | S8a  | ESC red: ESC end <-> stub       | OPEN (cut proven)   |          |     |         |
| E1  | S8a  | ESC-side red end -> rail A      | OPEN                |          |     |         |
| E2  | S8a  | ESC-side red end -> GPIO14      | OPEN                |          |     |         |
| E3  | S8a  | ESC-side red end -> rail B      | OPEN                |          |     |         |
| E6  | S8a  | ESC-side red end -> GND         | OPEN                |          |     |         |
| S1r | S4b  | signal matrix 13/14/18/19/23    | OPEN; 13-14 per PD1 |          |     |         |
| S2r | S4b  | signals+16/17/25 -> rails / GND | all OPEN (see §3.2) |          |     |         |
| S3r | S4b  | per-lead signal <-> +5V         | all OPEN            |          |     |         |
| S4r | S4b  | per-lead GND -> common          | beep each           |          |     |         |
| S5r | S4b  | signals -> batt+ wiring         | all OPEN (34: 27k)  |          |     |         |
| S6r | S4b  | rail-A <-> rail-B wiring        | no beep             |          |     |         |
| S7r | S4b  | each rail <-> batt+ wiring      | no beep             |          |     |         |
| W1  | S5   | GPIO4(#2) -> strip DIN          | ~330 Ω              |          |     |         |
| W2  | S5   | rail A -> strip VDD (diode)     | 0.15-0.35 V fwd     |          |     |         |
| W3  | S5   | same, reversed                  | OL                  |          |     |         |
| W4  | S5   | 1N5819 band toward strip        | visual pass         |          |     |         |
| W5  | S5   | strip cap                       | charging, not 0     |          |     |         |
| G1..G14 | S7 | grounds (S7 list; G14 blower) | beep each           |          |     |         |
| M1  | S7   | batt+ -> GND (composite)        | settles, not ~0     |          |     |         |
| M2  | S7   | rail A -> GND (composite)       | settles, not ~0     |          |     |         |
| M3  | S7   | rail B -> GND (composite)       | rising (C1), not ~0 |          |     |         |
| CP1 | S7   | pigtail + pin -> PDB batt+      | beep (or N/A: note) |          |     |         |
| CP2 | S7   | pigtail - pin -> star GND       | beep (or N/A: note) |          |     |         |
| CP3 | S7   | pigtail + pin -> star GND       | ~M1 value, no beep  |          |     |         |
| PW1 | S7   | ESC + power input -> batt+      | beep                |          |     |         |
| PW2 | S7   | ESC - power input -> star GND   | beep                |          |     |         |
| E4  | S8b  | ESC GND -> common GND           | beep                |          |     |         |
| E5  | S8b  | red end insulated               | visual pass         |          |     |         |
| E7  | S8b  | ESC header +5 pos -> A/B/14/GND | all OPEN, or "no    |          |     |         |
|     |      |                                 | metal in position"  |          |     |         |
```

## 12. PASS criteria — a **two-part** gate

A2 closes only when **both** parts below are satisfied. They attest different things and
neither substitutes for the other.

### Part 1 — Reviewer check (Claude Code session)

Scope, stated honestly because it bounds what this review is worth:

- [ ] **Completeness** — every row in §11 filled, none silently skipped; every conditional row (H5, G11, G14, PD1, CP1–CP3, E7's no-metal case) explicitly recorded as present *or* N/A rather than blank
- [ ] **Gate attribution** — each row taken at its listed gate, especially D1–D3 at S1 and E0–E3/E6 at S8a (a post-S6 divider reading, or an E-row probed after insulation, is not valid evidence — §3 rule 4)
- [ ] **Arithmetic and tolerance** — divider within ±5 % of 10/27/37 kΩ, Hall pull-up ≈10 kΩ **to 3V3** (board end), W1 ≈330 Ω, W2 in 0.15–0.35 V
- [ ] **Internal consistency** — no row contradicting another (e.g. CP3 vs M1)
- [ ] **Cross-reference** against `11_hardware_validation_plan.md` (§A2 there maps A2.1–A2.5 onto these S-gates)
- [ ] **Direct inspection of the §10 photos** — solder bridges, connector orientation, cap stripe polarity (**including C1, item 14 — the only evidence stop 6 gets for it**), **1N5819 band direction**, and the **pre-shrink severed red conductor** (item 3) are all visually checkable from an image. **Scoped honestly (F10): a photo can show bridges, orientation, polarity marks, band direction, and the pre-shrink cut. It cannot show harness topology** — that all G-row conductors reach one junction, or that no second ground path exists, is attested by the §S7/§S8b electrical rows alone; item 9 corroborates only that a junction exists. Within that scope this is the one part of the review that is independent observation rather than trust in the transcription, so it is mandatory, not optional.

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

1. **Any rail ↔ GND or batt+ ↔ GND short** — a *persistent* ≈0 Ω. Per §3 rule 1, a reading
   that starts low and rises is a charging capacitor, **not** a short
2. **ESC BEC red wire not isolated** (any continuity to a rail, signal, or ground)
3. **Divider wrong at S1** — top leg ≈20 kΩ (old design), tap shorted, or values far off.
   A post-S6 divider reading is not evidence either way
4. **Any GPIO with continuity to the battery+ line** (pre-divider) — instant board-killer at 8.4 V
5. **No common ground** between any two boards/devices in §S7
6. **Reversed polarity anywhere** (XT60, UBEC, ESC power input, cap, diode band)
7. **Uncertain connector orientation** — if you cannot positively identify a plug's signal/+/− order, stop and trace it; never "probably right"
8. GPIO35 pull-up found tied to 5 V instead of 3V3 (input-only pin, no clamp headroom)
9. **A wire present at GPIO26** (C4) — link2 RX is unwired by design; a beep means something got built that shouldn't exist

**Every stop above must be reachable from a measurement** (the review's F1/F3 lesson: a
stop with no generating row is an aspiration, not a gate). The generating rows, checked
stop by stop at revision time:

| Stop | Generating rows (gate) |
|---|---|
| 1 | P2/P6/P7 (SF) · W5 (S5) · **M1–M3, CP3 (S7)** |
| 2 | E0–E3/E6 (S8a) · E4/E5/E7 (S8b) |
| 3 | D1–D3 (S1) · C10b (S4) |
| 4 | **S5r (S4b)** |
| 5 | S4r (S4b) · G1–G14 (S7) |
| 6 | §2 visual at every gate · W2–W4 (S5) · CP1/CP2, PW1/PW2 (S7) · **C1 stripe: §2 + photo 14 (S6) only** — no no-power reading distinguishes a reversed 1000 µF |
| 7 | §2 + S3r/S4r (S4b) |
| 8 | **H1b (S2)** |
| 9 | C4 (S3) |

## 14. What Phase B needs (only after A2 is filled, reviewed, and approved)

Bench PSU or battery via the XT60 split (ESC **motor leads still disconnected**),
oscilloscope/logic analyzer (GPIO13/14 boot-float scope = B1.4/R04, PWM widths B1.3), the RP1
bound to the TX with **failsafe mode "No Pulses"**, `elrs-joystick-control` on the PC, and
flashing `esp32dev_tuning` — which is also the *first* moment the USB cable gets used. **None
of that is part of A2; Phase B stays blocked until A2 is reviewed and approved.**

**Battery reality (corrected 2026-08-04, F5):** there is **no in-envelope car pack** — the
1500 mAh ZEEE this section previously referenced **never arrived** (inventory correction
2026-07-31; the "not re-terminated" claim referred to a pack that never existed), and the
only battery on hand is the out-of-envelope 5200 mAh, bench-only. Sourcing spec: the
2026-07-31 entry in `../../HARDWARE_INVENTORY.md` (buy to dimensions, not capacity). The
XT90-S master-switch pigtail set **arrived 2026-07-30** — no longer in transit — and the
owner-made XT60-female termination on its XT90H tail is tested at §S7 (CP1–CP3); if those
rows were recorded NOT-ASSEMBLED, they are a **hard precondition to the first pack
connection**, not an optional leftover. §S7 no longer references a battery at all (the
reference point is the PDB input XT60 − pin), so none of this blocks A2 itself.

**The charge path is OUT of A2's scope and owns its own gate (owner decisions 2026-08-03,
F9a/F9b).** The IP2326 is **not fitted during A2 build week**; nothing charger-related
exists on the harness at any A2 gate (§S6). When it is built, it taps the **pack side** of
the XT90-S — the placement that makes the "pull the master = safe to charge" interlock real
— and it gets its own no-power checklist (polarity/diode asymmetry at the IP2326 output,
balance-lead pin-for-pin continuity, isolation from the rails) before it ever meets a pack.
That checklist does not exist yet; writing it is a tracked prerequisite of building the
charge path, and charging *activity* remains Phase-B-class powered work regardless.

## 15. What to paste back after actually running the checks

- the filled §11 measurement table (real readings + PASS/FAIL marks + gate + photo #),
- the photo set, identified — **including both §S8a red-wire shots**; Part 1 of §12 **inspects** these, so an unlabelled dump is not sufficient,
- any PASS-with-note deviations,
- the exact reading + a photo for anything that hit a §13 hard stop,
- and the §12 Part 2 owner attestation line.

The reviewer then runs §12 Part 1 against `11_hardware_validation_plan.md`. A2 closes only on
a clean Part 1 **and** a recorded Part 2.
