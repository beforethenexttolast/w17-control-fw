# A2 staged gates — adversarial review (2026-08-03)

> **STATUS — REVIEW ONLY. Nothing built, powered, flashed, or connected.** This document
> reviews `13_phase_a_a2_no_power_checklist.md` (the 2026-07-30 staged restructure, never
> executed) *before* the first solder joint. **A2 stays NOT-EXECUTED and Phase B stays
> BLOCKED regardless of anything below — a review cannot open a gate.** Precedent: the
> 2026-07-30 FIRST_ACTIVE adversarial pass, which found gamepad-device-loss uncovered by
> R1–R14 / I1–I9 / Groups A–D and produced I10, R15, D19–D22. This is the A2 equivalent.

**Sources read:** `13_phase_a_a2_no_power_checklist.md` (target, line refs below),
`../../w17-pdb-build-and-connector-guide.md` ("the guide"), `lib/config/include/config/PinMap.hpp`,
soundlight `PinMap.hpp`, `../../HARDWARE_INVENTORY.md` §E + 2026-07-30/31 log entries,
`../../CURRENT_STATUS.md` → Hardware gates, `11_hardware_validation_plan.md` §A2 (the
pre-restructure shape).

**Verdict up front: A2 is NOT safe to execute as written. It needs a revision pass before
the first joint.** Grounds, in one paragraph: §13 hard stops 1, 3-adjacent and 4 have **no
generating measurement anywhere in the staged flow** — the restructure correctly invalidated
the old whole-harness readings but never wrote their staged replacements, so a batt+→GND
short introduced at or after S6, a bridged rail pair, or a signal wire on the battery node
all pass A2 clean and are discovered at first power (findings 1–3). S8 — the hard gate this
review was told to attack hardest — is **unexecutable as sequenced and its three electrical
rows are unfalsifiable as built** (finding 4). S7's reference point does not exist on hand
and cannot be produced without violating the session golden rule (finding 6). And the build
order A2 mandates **directly contradicts the soldering guide the builder will have open on
the bench** (finding 8). Every one of these is fixable on paper; none is fixable after the
harness exists.

---

## Findings — most severe first

Severity logic: false-PASS-into-powered-hardware first, then unexecutable/unfalsifiable
gates, then contradictions that steer the builder into error, then staleness/minor.
CONFIRMED = verifiable from the documents alone. PLAUSIBLE = depends on component behavior
not provable from paper.

### F1 — CONFIRMED. After S6, nothing ever measures batt+ → GND again. §13 hard stop 1 has no generating row for the battery node.

**Where:** §S1 warning box (13_…:90–97), §S6 (:190–196), §S7 (:198–213), §13 stop 1 (:325).

The S1 box is right that `batt+ → GND ≈ 37 kΩ` is invalid post-S6, and it correctly forbids
re-running D3. But it **forbids the reading without replacing it**: the batt+ node acquires
its highest-risk joints *at and after* S6 — two UBEC inputs, the ESC 12 AWG direct feed, the
XT60 input, the XT90-S pigtail chain including the **owner-made XT60-female-onto-XT90H tail
joint** (HARDWARE_INVENTORY §E, 2026-07-30: made at the office from §5 stock, tested
nowhere), and per the guide step 8 the IP2326 — and not one gate measures across batt+→GND
after those joints exist. S7 sweeps grounds only; S8 sweeps the ESC red wire only.

**Failure scenario:** careful builder passes S1–S5, solders the UBECs at S6, and a strand
whisker bridges batt+ to the adjacent star-ground node. S7 passes (all grounds beep — the
bridge *helps*). S8 passes. A2 closes clean, Part 1 and Part 2 both honestly satisfied.
Phase B: XT60 mated through the XT90-S — the anti-spark resistor masks the first instant,
then full mate puts a dead short across a 2S pack. Wire fire. This is the exact false-PASS
class the restructure note says it was built to avoid, reintroduced one node over.

**Minimal fix:** add to S7 (or a new S7 row block) — `batt+ → GND, ohms mode: reading may
start low and rise (UBEC/ESC/charger input caps charging, §3 rule 1); a persistent ≈0 Ω is
a §13-stop-1 fail; record the settled value, no numeric expectation`. Falsifiable, valid at
its stage, and consistent with the doc's own capacitor convention. Add the same for the
mated switch-pigtail path: pack-side XT90 female pin → PDB batt+ (beep, correct polarity
pin-for-pin), so the owner-made joint is tested before it ever carries pack current.

### F2 — CONFIRMED. The rail-level isolation matrix does not exist: rail A ↔ rail B, rails ↔ batt+, and post-S6 rails ↔ GND are measured nowhere.

**Where:** §S4b (:155–165) checks *signals* against rails/GND; §S7 checks grounds; no gate
checks the rails themselves. §13 stop 1 (:325) names "any rail ↔ GND short" but no row can
produce that reading — the only rail↔GND measurement in the whole document is W5, which is
strip-local at S5, pre-S6.

Three distinct uncovered faults, each a first-power event:
- **Rail A ↔ rail B bridged** (adjacent output headers on the PDB, guide §5 step 6): two
  switching UBEC outputs hard-paralleled fight each other at power-on. No row detects it.
- **A rail branch mislanded on raw batt+** (XT30 branch crimped to the wrong PDB header):
  8.4 V onto every 5 V load on that rail — both ESP32s, camera, RP1 on rail A. No row.
- **Rail ↔ GND short introduced at/after S6** (the S4b S2r pass ran pre-S6 and only from
  the signal side): nothing re-screens it.

**Minimal fix:** add to S4b (pre-S6, while the readings are clean): `rail-A wiring ↔ rail-B
wiring: no beep`, `each rail ↔ batt+ wiring: no beep`. Add to S7 (post-S6 composite):
`rail A → GND` and `rail B → GND`, ohms mode, §3-rule-1 charging language (rail B carries
the 1000 µF — a rising reading is the *expected* signature), persistent ≈0 Ω = stop 1.
Note in S6 that rails↔batt+ is **not re-runnable** post-S6 (the measurement would read
through unpowered UBEC internals — same class as D3, diode-ish and unpredictable).

### F3 — CONFIRMED. §13 hard stop 4 (GPIO ↔ batt+, "instant board-killer") has no generating measurement.

**Where:** §13 stop 4 (:330); S4b S2r (:160) measures signals → rails and → GND only.
batt+ is not in any isolation matrix for pins 13/14/18/19/23/35 (pin 34 gets D2's 27 kΩ,
and C10 would catch a tap/feed swap — 34 is the only covered pin).

**Failure scenario:** the divider's batt+ feed wire runs in the same loom as the five
signal wires to the cassette. A crimp lands one position off and double-lands on GPIO13's
node. C5 still beeps (steering signal is still there), S1r passes (batt+ is not in the
matrix), S2r passes (batt+ wiring is neither rail nor GND — pre-S6 batt+→GND is 37 kΩ, no
beep). A2 closes. First power: 8.4 V into GPIO13 — the checklist's own words for this are
"instant board-killer at 8.4 V," and its own gate order guarantees the condition is never
looked for.

**Minimal fix:** extend S2r: `each signal (13/14/18/19/23/34/35) and 16/17/25 → batt+
wiring: no beep` (34 annotated: ≈27 kΩ via the divider top leg is the expected non-beep
reading, not a fault).

### F4 — CONFIRMED. S8, the hard gate, is unexecutable as sequenced, and E1–E3 are unfalsifiable as built. Attacked hardest, as instructed; it did not hold.

**Where:** §S8 (:215–226); §2 heat-shrink bullet (:60); guide §2 connector row "ESC signal —
3-pin servo, **+5V pin removed**" (:51) and §5 step 7 (:112–114).

Four independent defects:

1. **Unexecutable when its turn comes.** The red-wire cut happens at guide step 7, during
   the PDB build — early, around S1-time. From that moment §2 (run per-gate) requires the
   cut end "individually insulated, not just folded back bare." By S8, both cut ends are
   under heat-shrink and **there is no conductive access to the red conductor at all** —
   E1/E2/E3 cannot be probed without piercing insulation the checklist itself mandated.
2. **Unfalsifiable as built.** The guide's connector spec *removes the +5 pin* from the ESC
   header. With no metal in the +5 position, E1 (red→rail A), E2 (red→GPIO14), E3
   (red→rail B) read OPEN **whether or not the cut was ever made** — an intact red wire
   terminates at an empty connector position and beeps to nothing. The rows pass on a car
   where the one thing they exist to verify was skipped. This is the same defect class as
   the two "per your build" rows the restructure fixed (WS2812, C4), surviving in the
   hardest gate.
3. **Missing measurement: ESC red → GND is checked nowhere.** E1–E3 cover rails and signal.
   If the cut end's insulation fails against ground (or the "insulation" is tape over a
   folded-back bare end touching the ground braid), the ESC's internal ~6 V BEC is
   dead-shorted the moment drivetrain power exists. E4 doesn't catch it (E4 *wants* a beep
   on the GND wire); nothing else looks.
4. **Ambiguous probe target.** The cut leaves two ends — the ESC-side end (live BEC output
   when powered; the one that matters) and the connector-side stub. E1–E3 say "ESC servo-lead
   red wire" without saying which. A builder probing the dead stub gets OPEN everywhere and
   learns nothing.

**Minimal fix — restage S8 as a two-moment gate:**
- **S8a, at the cut (guide step 7 / before insulation):** E0 `ESC-side red end ↔
  connector-side red stub: OPEN` — this is the row that actually falsifies "the cut was
  made"; E6 `ESC-side red end → common GND: OPEN`; E1–E3 as today, probed on the ESC-side
  end, explicitly *before* heat-shrink; **photo of the severed conductor before shrink,
  photo of the insulated result after** (feeds F10). Then insulate.
- **S8b, whole-harness (current S8 position):** E4 (unchanged), E5 (visual), plus E7
  `ESC header +5 position at board #1 → rail A / rail B / GPIO14 / GND: OPEN` — verifies
  the pin-removal defense from the accessible side, which *is* probeable at S8-time.

### F5 — CONFIRMED. S7's reference point does not exist, and producing it would violate the session golden rule.

**Where:** §S7 preamble "One probe on battery −" (:200); golden rule "battery stays
disconnected **and out of reach**" (:29); §14 (:345–349); HARDWARE_INVENTORY §E.

Battery reality, stated plainly as requested: **there is no XT60-terminated pack, and there
is no in-envelope pack at all.** The 1500 mAh ZEEE recorded on 2026-07-29 never arrived
(inventory correction 2026-07-31); the only battery on hand is the out-of-envelope 5200,
bench-only, lead termination not established as XT60. §14's supporting text is stale twice
over: the XT90-S set it calls "in transit as of 2026-07-29" arrived 2026-07-30, and "the
ZEEE pack's main lead has not been re-terminated" refers to a pack that never existed.

So S7 as written either stalls (no battery −to probe), or — worse — prompts the builder to
fetch the 5200 onto the bench for a reference point, which is precisely what the golden rule
exists to prevent. A rule that can only be satisfied by breaking a bigger rule will be
broken in the direction of convenience.

**Minimal fix:** redefine the S7 reference as **the PDB input XT60 male − pin** (equivalently
the star-ground node) — harness-side, exists after the PDB build, needs no pack, and is what
§1's own "GND = battery − wire / XT60 − pin" already half-says. Rewrite §14's battery
paragraph against inventory reality (no car pack exists; sourcing spec in the 2026-07-31
inventory entry).

### F6 — CONFIRMED. The CRSF lead carries rail-A 5 V, and its in-lead isolation is measured nowhere; 16/17/25 are excluded from every isolation matrix.

**Where:** S4b scope "all five actuator leads" (:155); S3r "each 3-pin lead" (:161); S2r
pin list `13/14/18/19/23/34/35` (:160); guide §2 CRSF row: JST-XH **4-pin** carrying
`RP1_TX→GPIO16, GPIO17→RP1_RX, 5V(A), GND` (:48).

The S4b rationale — "a reversed 3-pin plug is the single most common crash-cause" — applies
with more force to the CRSF connector, where 5 V sits one crimp position from two UART pins,
yet the CRSF lead is outside S4b's scope entirely: no signal↔5V check, no signal↔GND check,
and GPIO16/17 (and link2's 25) appear in no isolation row anywhere. A strand whisker
bridging 5V(A)↔TX inside the JST-XH housing passes C1 (it still beeps) and everything else,
then puts rail A onto GPIO16 at first power — dead UART2 or dead RP1, i.e. the failsafe's
input path dies at the moment it's first trusted.

**Minimal fix:** add an S4 sub-block for the CRSF lead at the connector: `GPIO16 ↔ lead 5V
pin: no beep`, `GPIO17 ↔ lead 5V: no beep`, `each ↔ lead GND: no beep`; add 16/17/25 to the
S2r pin list (and to the F3 batt+ extension). link2's lead has no 5 V conductor, so
adding 25 to the matrices is sufficient for it.

### F7 — CONFIRMED. Old A2.5 — the GPIO13/14 boot-float pull-down/RC recommendation (R04) — was dropped by the restructure without a record-either-way row.

**Where:** `11_hardware_validation_plan.md` A2.5 (:43); absent from every S-gate.

The restructure absorbed A2.1–A2.4 into the S-gates (verified: A2.1→S3/S4, A2.2→S8,
A2.3→S7, A2.4→S5) but A2.5 simply vanished. It is "(Recommended)", so its absence is a
legitimate build outcome — but the checklist's own standard for optional items is H5:
*populated or explicitly recorded as not populated, never blank*. As written, if the
pull-downs are fitted no gate verifies them (value, node, not-a-short), and if they are
omitted nothing records that the R04 boot-float exposure on the **ESC and steering signal
lines** — the two safety-critical outputs — was accepted deliberately.

**Minimal fix:** one H5-style row in S4: `GPIO13/GPIO14 boot-float pull-down/RC: populated
(measure ≈value, to GND) or recorded as not populated — either is a PASS, blank is not.`

### F8 — CONFIRMED. A2's gate order contradicts the PDB soldering guide's build order, and the PDB frame the early gates depend on is never sequenced or verified (no S0).

**Where:** S1 banner "before the UBECs exist on the batt+ node" (:90) vs guide §5 order:
XT60+star (step 2) → **UBECs (step 3)** → C1 (4) → **divider (5)** → headers (6) (:101–111).
Also: H3 needs a "rail-A harness node" at S2 (:119), S4r needs "harness ground" at S4b
(:162) — nodes the guide builds in steps 2 and 6, which no S-gate sequences, so the
checklist consumes nodes whose construction it never orders or checks.

**Failure scenario:** the builder sits down with the soldering guide — the document that
tells them where to put the iron — and follows steps 1→8. They arrive at S1 with both UBECs
already on the batt+ node. D3 reads an unpredictable composite; §13 stop 3 says "divider
wrong at S1" — a **false FAIL on a correctly built board**, the precise defect class the
restructure was created to eliminate, manufactured by the restructure's own sibling
document. The charitable outcome is hours lost; the uncharitable one is the builder
rationalizing the reading and proceeding with no valid divider evidence ever taken.

**Minimal fix:** (a) amend guide §5 to the S-gate order (divider before UBECs; UBECs =
S6) — one-line change plus a pointer to A2; (b) add **S0 — PDB frame** to the checklist:
build XT60 input + star node + output headers + rail branch looms, verify `star ↔ XT60 −
pin: beep`, `batt+ ↔ GND: open at this stage`, `rail-A wiring ↔ rail-B wiring ↔ batt+ ↔
GND: all mutually open`, which also gives F2's pre-S6 rows a natural home and makes H3/S4r
reachable by construction. (Guide edit is workspace-repo scope — flagged here, not made.)

### F9 — CONFIRMED. The IP2326 charge path is built during the same build week and measured by nothing; its node placement is also ambiguous in a way that breaks the charge interlock.

**Where:** guide §5 step 8 (:115–117), §1 topology (:20–22); no A2 row touches USB-C,
IP2326 output polarity, or the balance-lead pinout.

Charging as an *activity* is Phase-B-gated, but the *joints* — IP2326 output onto the pack
path, JST-XH balance extension, hidden USB-C — get soldered per the guide during the A2
build and then never see a meter. A reversed IP2326 output or a mis-pinned balance lead is
a fire at first charge, and the two-part §12 gate will have honestly attested a record in
which these joints never appear. Separately, the topology is ambiguous about *where* the
charger lands: the interlock ("pull the XT90-S = safe to charge") only works if the charger
taps the **pack side** of the switch, but the pack side is off-board and the IP2326 lives
on the PDB — the guide never resolves this, and any batt+ expected value depends on the
answer (an on-switched-node charger is yet another parallel input stage the S1/S6 warnings
don't mention).

**Minimal fix:** either (a) an S-gate for the charge path — polarity rows (diode/resistance
asymmetry at the IP2326 output), balance-lead pin-for-pin continuity, isolation from rails —
plus a topology decision recorded for the tap point; or (b) an explicit exclusion: "the
charge path is NOT built during A2 build week and is gated behind its own checklist" —
either is defensible; silence is not. Add the charger to S6's "after this point" list
either way.

### F10 — CONFIRMED. The §10 photos cannot corroborate the two things §12 Part 1 most needs them for.

**Where:** §10 items 3 and 9 (:236, :242); §12 Part 1 "direct inspection" bullet (:303).

- **The ESC red-wire cut (item 3):** after §2-mandated insulation, a photo shows
  heat-shrink. A cut-and-insulated red wire and an **intact** red wire under a sleeve are
  photographically identical. Item 3 is also the only wiring photo in the list with **no
  gate tag** — precisely the photo whose timing decides whether it is evidence or
  decoration. Corroboration exists only if the photo is taken at the cut, before shrink
  (folded into F4's S8a).
- **The single star ground (item 9):** a photo shows wires converging at a junction. It
  cannot show that all 13 G-rows' conductors *reach* it, nor the absence of a second ground
  path elsewhere in the loom (the ground-loop case). The S7 beeps are the evidence; the
  photo corroborates only "a junction exists."

**Minimal fix:** retag item 3 as "at the cut, before insulation, severed conductor visible
+ after-shrink second shot"; annotate item 9 (and the §12 bullet) so Part 1 claims photo
corroboration only for what an image can actually show — bridges, orientation, stripe
polarity, band direction, pre-shrink cut — and names the S7/S8 electrical rows as the sole
evidence for topology. Otherwise Part 1's strongest-sounding check is decorative for the
two highest-stakes items.

### F11 — CONFIRMED. The Hall pull-up's documented location is unbuildable, and the contradiction steers the builder directly into §13 hard stop 8.

**Where:** guide §2 Hall row: "10k pull-up→3V3 **on sensor side**" (:58) vs the lead's own
conductors: `sig, +5V(A), GND` — a 3-pin lead with **no 3.3 V conductor**. Guide §3 hedges
("at the sensor / ESP32 #1"). Checklist §2 (:63) and H1 (:117) require the pull-up to 3V3
without saying where it lives.

A builder wiring from the connector table reaches the sensor end with a 10 kΩ resistor, an
instruction to pull up to 3.3 V, and exactly one supply conductor available: the +5 V. The
"obvious" improvisation — pull up to the wire that's there — is verbatim §13 stop 8 (5 V
pull-up on input-only GPIO35, no clamp headroom). The doc contradiction doesn't merely
permit the hard-stop condition; it manufactures the temptation. It also decides whether H1
is even measurable at S2 "isolated" (board-end pull-up ⇒ H1 needs the board-end harness
built; sensor-end ⇒ impossible as drawn).

**Minimal fix:** decide and write it once — the pull-up lives **at the ESP32 #1 end**
(GPIO35 header pin → 3V3 pin), where 3V3 exists; correct the guide's connector row; add
one explicit row `H1b: GPIO35 → rail-A/5V wiring: no beep / ≫10 kΩ` so stop 8 has a
generating measurement instead of a parenthetical.

### F12 — CONFIRMED. The checklist's board-geometry guidance is written for DevKit V1, but the build boards are MH-ET D1-Minis.

**Where:** §1 "DevKit pin headers are 2.54 mm" (:36); §2 "especially adjacent DevKit pins
(16/17, 25/26, 18/19)" (:55); §3 rule 2's "board sits in female headers" assumption
(:74–78). Owner decision 2026-07-24 (`CURRENT_STATUS.md`): cassette controllers are MH-ET
Live D1-Mini ESP32s; the DevKit V1 clones are TEST/SPARE. Inventory: MH-ET caliper still
owed.

The bridge-inspection call-outs are pin-*adjacency* facts, and adjacency is a property of
the board layout, not the GPIO number — on the D1-Mini's dual-row arrangement the risky
neighbor pairs are different, so the §2 list aims the magnifier at the wrong places.
Whether the MH-ETs will be socketed (making §3 rule 2's unseat-for-isolation runnable) or
hard-soldered (forcing every isolation row into the weaker resistance-mode variant) is
undecided and changes how half the isolation rows execute.

**Minimal fix:** at revision time, re-derive the adjacent-pair list from the actual MH-ET
silkscreen (a 2-minute job with the board in hand) and record the socketed-vs-soldered
mounting decision in §3 rule 2 before S2 runs.

### F13 — CONFIRMED, minor batch.

1. **S2r vs §3 rule 2 false-FAIL on pin 34:** rule 2's hard-wired variant says "resistance
   mode, expect ≫ 10 kΩ"; GPIO34→GND legitimately reads **≈10 kΩ** (the divider bottom
   leg). A careful builder with hard-soldered boards reads 10 k, concludes FAIL. Annotate
   34 (and, post-F3, its ≈27 kΩ to batt+). (:74–78, :160)
2. **Blower GND missing from S7:** it is a rail-B load (guide §2 :54) with a ground return;
   G1–G13 skip it. Add G14 `blower connector GND` (or recorded as not yet wired, H5-style).
3. **100 nF location contradiction:** guide §4 places C3 **at the ESP32 GPIO34 pin end**
   (:90), which per C10 isn't built until S4 — so S1's "wait for the cap to charge" preamble
   (:99) describes a cap that isn't in circuit at S1, and §10 photo 4 ("divider + 100 nF
   close-up *at S1*") cannot contain it. Decide the location once; fix whichever doc loses.
4. **Guide still offers the forbidden link2 RX wire:** connector row "(opt GPIO26←17)"
   (:49) contradicts the closed C4 decision ("verify NO wire present", §13 stop 9). A
   builder crimping from the table populates the conductor C4 exists to forbid. Strike it.
   (Workspace-repo edit — flagged, not made.)
5. **§12 Part 1 cross-references a stale document:** `11_hardware_validation_plan.md` §A2
   still describes the single-pass A2.1–A2.5 shape. A reviewer running Part 1 "against"
   it meets a structural mismatch (and A2.5 is the F7 hole). Update it to point at the
   S-gates, or scope the cross-reference.
6. **ESC 12 AWG power-input joints have no continuity/polarity row** and §2's polarity
   bullet doesn't name them; covered composite-side by F1's fix (pigtail-path row) but the
   ESC + input ↔ batt+ / − input ↔ star pair deserves explicit rows — a reversed ESC power
   input is destroyed at first connect, and the checklist's own standard is "never
   'probably right'" (§13 stop 7).

### F14 — PLAUSIBLE. S5's diode rows may lie on a post-S6 re-check, and only S1 carries a single-shot warning.

**Where:** W2/W3 (:185–187); the S1-only warning box (:90–97).

Post-S6, rail A ties to the UBEC-A output stage. W2 (diode-mode forward through the 1N5819)
then reads the diode **in parallel with** an unpowered buck output; W3's expected OL depends
on no alternate reverse path existing, and the UBEC output caps + strip load plausibly
present one (a slow charging ramp misread as leakage, or vice versa). Unlike D1–D3, nothing
marks W2/W3 as stage-bound. This is PLAUSIBLE, not CONFIRMED, because unpowered UBEC output
impedance isn't derivable from the documents — but the fix costs one sentence. Same class:
W2's forward reading must charge the 1000 µF through the diode before it settles, and §3
rule 1's settling language covers ohms mode only — extend it to diode mode at W2.

**Minimal fix:** generalize the S1 principle into §3 as rule 4: **"every pre-S6 row is
single-shot evidence, valid only at its own gate; after S6 only S7/S8 rows (and the F1/F2
composite rows) are meaningful"** — then the S1 box and a one-line W2/W3 note become
instances of a stated rule instead of the only two exceptions anyone thought of.

---

## Answers to the five standing questions

1. **Valid at its stage?** All S1–S5 rows check out at their own gates *given* the nodes
   they probe exist by then — which the gate order doesn't guarantee (F8: H3, S4r).
2. **Valid later?** D1–D3 correctly marked; W2/W3 unmarked (F14); everything else stable —
   but the *composite* itself is unmeasured after S6, which is the bigger hole (F1/F2/F3).
3. **Reachable?** S8 is the failure: E1–E3 are buried under mandated heat-shrink by the
   time their gate arrives (F4.1). S7's reference doesn't exist (F5).
4. **Falsifiable?** E1–E3 are the survivors of the "per your build" purge — they pass
   whether or not the cut was made, because the +5 pin removal leaves them nothing to beep
   against (F4.2). H5/G11's record-either-way pattern is good and should be extended (F7,
   F13.2).
5. **Measured nowhere?** batt+→GND post-S6; rail↔rail; rails↔batt+; rails↔GND post-S6;
   signals↔batt+; ESC red↔GND; the ESC header +5 position; the ESC 12 AWG power joints; the
   owner-made XT90H→XT60 joint; the CRSF lead's internal 5 V isolation; 16/17/25 in any
   matrix; the entire IP2326 charge path; the blower ground; the A2.5 pull-down decision.
   The FIRST_ACTIVE hole lived between checks; so do all fourteen of these.

## Verdict

**A2 as written is not safe to execute, and the unsafety is of the dangerous polarity:
most of what's wrong produces false PASSes, not false FAILs.** The restructure's core idea —
stage the gates so no reading is taken against an invalid expected value — is right and
should stand. But it was executed as a *subtraction*: the whole-harness screens the old §13
hard stops depended on were invalidated and never re-issued in staged form, so hard stops
1 and 4 are now aspirations with no measurement behind them, and the hard gate S8 cannot
fail on the fault it exists to catch. A first joint soldered under this document starts
accumulating states (the insulated red end, the guide-order UBEC attach) that make several
fixes impossible to apply retroactively. **Revision pass first. The paper is cheap; the
false PASS is not.**

Suggested revision-pass order (each independent, F1–F5 before any solder regardless):
F1+F2+F3 (the composite screens — one edit session), F4 (S8a/S8b restage), F5 (S7
reference), F8 (S0 + guide order), F6/F7 (matrix extensions), then the rest.

**Gate state after this review: unchanged. A2 NOT-EXECUTED. Phase B BLOCKED.**
