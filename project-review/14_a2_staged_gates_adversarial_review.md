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

---

## Owner decisions taken 2026-08-03 (answering F9, F11, F12)

Four decisions the revision pass needs and the checklist cannot make for itself. Recorded here
rather than in `CURRENT_STATUS.md` because the RESIDUAL B+D session is live on that file; they
move to the workspace record when this branch merges.

### F9a — the IP2326 is NOT fitted during A2 build week

**Decision: defer the whole charger.** Drop guide step 8 from the A2 build; add the IP2326 to
S6's "after this point" list; charging is gated behind its own checklist. Rationale: A2 stays a
pure no-power continuity exercise, and charging is powered work — Phase B class — regardless.
This is the reviewer's second option, taken cleanly: **nothing about the charge path is left
silent, and nothing unmeasured is built.**

Revision pass must: strike step 8 from the A2 build sequence, add the S6 entry, and state
explicitly in §14 that the charge path is out of A2's scope and owns its own gate.

### F9b — the charge tap is PACK-SIDE of the XT90-S master switch

**Decision taken now even though the build is deferred**, because it constrains PDB layout and
the reviewer was right that a recorded tap-point decision is required either way.

Pack-side keeps the **pull-the-XT90 charge interlock real**: pulling the master genuinely
isolates the car from the charge path. The PDB-side alternative was rejected specifically
because the interlock would have become a false safety story that then had to be struck from
the docs — a worse outcome than the extra run.

Consequence for the revision pass and the guide: the IP2326 does **not** sit on the PDB as
currently drawn. `w17-pdb-build-and-connector-guide.md` needs the charger moved off the PDB
block diagram and onto the pack side of the switch.

### F11 — the Hall pull-up goes at the ESP32 end

**Decision: 10 kΩ at the board end, where 3V3 actually exists.** The sensor lead stays a
standard 3-wire run (sig/+5V/GND). Electrically equivalent to the sensor-side placement, and it
makes the dangerous improvisation *structurally impossible*: with no pull-up expected at the
sensor, there is no reason to reach for the +5 that is there, which is verbatim §13 hard stop 8
on input-only GPIO35. The 4-conductor alternative was rejected — non-standard lead, extra loom
conductor, and a 3V3 output position the PDB does not have.

Revision pass must: correct the guide's connector row, and add **H1b `GPIO35 → 5V wiring: no
beep`** so hard stop 8 finally has a generating measurement (it currently has none).

### F12 — MH-ET boards are SOCKETED, with a verification owed

**Decision: female headers on the PDB; boards lift out.** §3 rule 2's unseat-for-isolation stays
runnable as written, a dead board is swappable without desoldering ~30 pins inside the cassette,
and the pre-soldered male pins the boards arrived with are already the correct half.

⚠ **This decision carries an owed verification and is NOT final until it clears.** Socket height
eats into the **S0 ≥ 9.82 mm** cassette clearance from the ZK study. Nobody has calipered a
female header against that headroom. **If the measurement breaks S0, this decision reopens** and
direct-soldering returns — which would in turn require the revision pass to write a different
board-isolation method, since rule 2 would no longer be runnable. Do not treat socketing as
settled in any document that depends on S0.

The measurement is a no-power job and the boards are on hand: it belongs in the batch-2
measurement session, ahead of the S2 adjacency re-derivation F12 also calls for.

### Still open, not decided here

F12's other half — re-deriving the §2 adjacency call-outs from the **MH-ET silkscreen** instead
of the DevKit V1 layout — is measurement work, not a decision, and is blocked on nothing but
someone with the boards and a magnifier.

---

## Revision pass 2026-08-04 — closure table

Executed on branch `docs/a2-revision-pass` (this repo: checklist §13, plan §11, this
register) and `docs/a2-revision-pass` (workspace repo: the PDB guide, `CURRENT_STATUS.md`).
**Revision only: A2 stays NOT-EXECUTED and Phase B stays BLOCKED — closing findings makes
A2 executable, not executed.** Line refs in this document remain pre-revision checklist
refs, as before.

| Finding | What changed | Where |
|---|---|---|
| F1 | batt+→GND composite screen added post-S6 (M1, ohms, §3-rule-1 language, no numeric expectation, persistent ≈0 Ω = stop 1); the mated pigtail chain incl. the owner-made XT60-female tail joint gets pin-for-pin rows (CP1–CP3) before it ever carries pack current, with a NOT-ASSEMBLED record option that becomes a hard precondition to first pack connection | checklist §S7, §14 |
| F2 | Rail isolation matrix now exists: rail↔rail + rails↔batt+ at SF (P3–P5) re-screened at S4b (S6r/S7r, marked single-shot); rails↔GND post-S6 at S7 (M2/M3, charging language for C1); S6 states rails↔batt+ is not re-runnable after the UBECs attach | checklist §SF, §S4b, §S6, §S7 |
| F3 | Signals→batt+ generating row added (S5r) covering 13/14/16/17/18/19/23/25/35, with GPIO34 annotated ≈27 kΩ (top leg) as the expected non-beep reading; §13 stop 4 now reachable | checklist §S4b, §13 map |
| F4 | S8 split: **S8a at the cut** (executes during S4's ESC-lead step, before insulation — E0 falsifies the cut itself, E1–E3 probe the named **ESC-side end**, E6 covers red→GND, photos before/after shrink) + **S8b whole-harness** (E4, E5, E7 — the header +5 position probed from the accessible side, with an explicit "no metal in position" PASS so the row can't silently pass on empty air) | checklist §S8a/§S8b, §S4, §2, §10 |
| F5 | S7 reference redefined harness-side: PDB input XT60 male − pin = star node; §1 reference-points paragraph rewritten; §14's battery paragraph rewritten against inventory reality (no in-envelope pack exists; the ZEEE 1500 never arrived; XT90-S set arrived 2026-07-30) | checklist §1, §S7, §14; `CURRENT_STATUS.md` Hardware gates (same stale text lived there) |
| F6 | CRSF-lead internal isolation block added (K1–K4 at the JST-XH connector); 16/17/25 added to S2r and S5r; link2 noted as covered by 25's matrix membership (no 5 V conductor) | checklist §S4, §S4b, §S3 note |
| F7 | PD1 row added, H5-pattern: populated (measure + record value) or explicitly not populated; blank is not a pass — plus the F15 annotations so the row can't false-FAIL S2r/rule 2 | checklist §S4; plan §A2.5 mapping |
| F8 | **SF** (PDB frame) added — XT60 + star + ESP32 sockets + output headers + rail looms built and verified before any gate consumes them (H3/S4r now reachable by construction); guide §5 build order rewritten to BE the S-gate order (divider before UBECs; each step names its gate). *Proposed here as "S0"; renamed **SF** in the closure pass — see the naming record below* | checklist §SF; guide §5 |
| F9a | Owner decision implemented: IP2326 NOT fitted during A2 build week — guide step 8 struck, S6 "after this point" carries the exclusion, §14 states the charge path owns its own (not-yet-written) gate | guide §5; checklist §S6, §14 |
| F9b | Owner decision implemented: charge tap is PACK-side of the XT90-S — guide §1 topology redrawn (charger off the PDB), §3 "what lives where" moves the IP2326 off-PDB, interlock bullet states the placement is what makes it real | guide §1, §2, §3 |
| F10 | Photo item 3 retagged (two shots at S8a: pre-shrink severed conductor + insulated result); item 9 + the §12 photo bullet scoped to what an image can show — topology evidence is the S7/S8b rows alone | checklist §10, §12 |
| F11 | Owner decision implemented: pull-up at the ESP32 #1 end — S2 preamble states it and why; H1b added as stop 8's generating row; guide §2 Hall row + §3 table corrected | checklist §S2, §2; guide §2, §3 |
| F12 | Socketing decision recorded in §3 rule 2 **with its owed caliper verification and the reopening condition stated** (socket stack vs the ZK clearance `S0` ≥ 9.82 mm, due before **SF**'s first socket joint — the name clash that annotation used to carry is gone, see the naming record below); §2 adjacency call-outs replaced by an explicit OWED placeholder (inspect every joint until the MH-ET list exists). **Neither measurement is closed — see below** | checklist §3, §2, §SF; guide §5 |
| F13.1 | GPIO34's ≈10 kΩ (bottom leg) and ≈27 kΩ (top leg) written into §3 rule 2's exceptions list and S2r/S5r annotations | checklist §3, §S4b |
| F13.2 | G14 blower connector GND added, record-or-N/A pattern | checklist §S7 |
| F13.3 | Decided: guide §4 wins — C3 lives at the GPIO34 pin end. S1 preamble rewritten (divider is bare, readings settle immediately), §2 divider row corrected, photo 4 retagged capless, C10b + photo 12 verify the pin-end fit at S4 | checklist §S1, §2, §S4, §10 |
| F13.4 | The `(opt GPIO26←17)` conductor struck from the guide's link2 row — the row now states GPIO26 stays unwired by design (C4, §13 stop 9) | guide §2 |
| F13.5 | Plan §A2 rewritten: the A2.1–A2.5 rows are now explicitly the risk-register mapping onto the S-gates, not a runnable list; A2.5 maps to PD1 | plan §A2 |
| F13.6 | ESC 12 AWG power-feed rows added (PW1/PW2 at S7) and the §2 polarity bullet now names the pair; the feed's attach moment sequenced at S6 | checklist §S7, §2, §S6 |
| F14 | §3 rule 4 (every pre-S6 row is single-shot, valid only at its own gate) — S1's box, W2/W3's new note, and S6r/S7r are instances of the stated rule; rule 1 extended to diode mode at W2 | checklist §3, §S5 |

**§13 walk performed:** every hard stop now has at least one generating row; the
stop→row map is written into §13 itself so the property stays checkable, not asserted.

### Closure pass 2026-08-04 (later) — F15/F16, same table

Same branches, same rule: **A2 stays NOT-EXECUTED and Phase B stays BLOCKED.** These two are
the revision pass's own findings, closed on the same terms as F1–F14, plus the `S0` naming
record and the two findings closing *them* generated (F17, F18 below).

| Finding | What changed | Where |
|---|---|---|
| F15 | §3 rule 2's blanket "≫ 10 kΩ" replaced by a **closed exceptions list** — 34→GND (≈10 kΩ), 34→batt+ (≈27 kΩ), 35→3V3 (≈10 kΩ), 13/14→GND (= the fitted pull-down when PD1 is populated) — with a standing instruction that any future revision adding a deliberate resistance to the signal set must extend the list in the same edit, since that omission *is* the F13.1/F15/F17 defect. PD1 states the populated case is the expected reading, not a fault; S2r annotated | checklist §3 rule 2, §S4 (PD1), §S4b (S2r) |
| F16 | C1's fit moment sequenced: **fitted at S6 with the UBECs**, and said so at every row whose expected value depends on it — SF (absent by construction, P7's clean OPEN), S6 (the attach moment, in one sitting), S7/M3 (a rising reading is the expected signature), guide §5 step 6. Closing it exposed the corollary the fit moment makes visible: C1 is the only PDB electrolytic with **no photo**, while §12 Part 1 claims cap-stripe polarity is photo-checkable and **no no-power reading tells a reversed 1000 µF from a correct one** — so **photo item 14** (C1 at S6, stripe visible) is added and §13's stop-6 row now names §2 + photo 14 as the whole of C1's evidence | checklist §SF, §S6, §S7, §10 item 14, §12, §13 map; guide §5 |
| — | **`S0` naming settled** (was "Register note (not a finding)"): the frame gate is renamed **SF**; `S0` now names only the ZK cassette clearance. Swept across both repos — see the naming record below | checklist (all), plan §A2, this register's pointers, guide §5, `CURRENT_STATUS.md` |

### Deliberately NOT closed

- **F12, both measurement halves.** The MH-ET adjacency re-derivation and the socket-height
  caliper check are bench work with the boards in hand. The revision marks both as OWED
  placeholders/preconditions (due before SF's first joint) rather than absorbing them —
  writing pairs from memory or treating socketing as settled would be the exact
  asserted-over-unchecked-set defect this file exists to catch.
- **The charge-path gate itself.** F9 is closed via the owner's option (b) — the exclusion
  is explicit everywhere it matters — but the no-power checklist the charge path will need
  does not exist yet. Tracked in checklist §14 as a prerequisite of building the path.

### F15 — CONFIRMED (found during the revision pass). F7's minimal fix, implemented as specified, manufactures a §3-rule-2 false FAIL when the pull-downs are fitted.

**Where:** this document's F7 fix (:197–199); S2r (:160); §3 rule 2 (:74–78).

The H5-style row F7 asks for makes "pull-downs populated" a legitimate PASS — but S2r
expects every signal → GND open, and rule 2's hard-wired variant expects **≫ 10 kΩ**. A
fitted 10 kΩ pull-down on GPIO13/14 reads ≈ 10 kΩ to GND: beeper-mode S2r passes (10 kΩ
doesn't beep), but the resistance-mode variant — exactly the fallback that becomes the norm
if F12's socket-height verification fails — reads a correctly built board as a rule-2 FAIL.
Same shape as F13.1's pin-34 case, and another instance of the recurring class (a property,
"all signals ≫ 10 kΩ to GND," asserted over a set with an unchecked member) — this time
introduced by a fix inside the review itself. **Fix applied:** PD1 records the fitted value;
§3 rule 2 carries an explicit exceptions list (34→GND, 34→batt+, 35→3V3, 13/14→GND when
PD1 says populated); S2r annotated.

### F16 — CONFIRMED (found during the revision pass). C1's fit moment was sequenced by neither document, and the revision's own new rows made it load-bearing.

**Where:** guide §5 old step 4 (C1 between the UBECs and the divider); the F2/F8 fixes'
new pre-S6 rail rows.

Neither document ever said when C1 (the rail-B 1000 µF) goes on relative to the gates. In
the pre-revision checklist this was latent — no pre-S6 rail↔GND row existed (that absence
was F2). The moment F2/F8's rows exist, C1's fit moment decides their validity: fitted
early, P7/S2r's clean-OPEN expectation reads a charging transient it has no language for;
deferred past S7, M3's expected charging signature is wrong instead. The fix F2 prescribed
would have introduced the hole it was fixing — the same pattern this review's preamble
notes about the restructure it reviewed. **Fix applied:** C1 is explicitly fitted at S6
with the UBECs, stated at SF (absent by construction), S6 (attach moment), S7/M3 (expected
signature), and guide §5.

### F17 — CONFIRMED (found closing F15). F15's exceptions list omits the 13↔14 pair, and S1r false-FAILs on it in resistance mode.

**Where:** F15's fix — §3 rule 2's exceptions list; checklist S1r (S4b); PD1 (S4).

F15's fix legitimises a finite 13/14 → **GND** reading when PD1 records the pull-downs
populated. It stops there. **S1r is the signal↔signal matrix**, and with a pull-down on
GPIO13 *and* one on GPIO14 both returning to the star node, the pair 13↔14 reads through
them in series: **≈2× the fitted value.** Beeper mode is safe at any plausible value. The
**resistance-mode variant is not** — and that variant is not hypothetical: §3 rule 2 makes it
the norm for hard-wired boards, which is exactly what F12's socket-height caliper may force.
No pull-down value is mandated anywhere (R04 says "pull-down/RC" and leaves it to the bench),
so two ordinary 4.7 kΩ parts put 13↔14 at **≈ 9.4 kΩ** — a plain rule-2 FAIL, on a correctly
built board, reported to the builder as *"the ESC signal line and the steering signal line
are bridged."* The two most safety-critical outputs, on the false-FAIL polarity.

This is the third instance of the same class in this register (F13.1 pin 34, F15 pin 13/14 to
GND, F17 the 13↔14 pair) and the second generated by a fix inside it. The pattern is not
carelessness — it is that each fix legitimises **one** new reading and the property "all
other pairs are open" is re-asserted over the enlarged set without re-walking it.

**Fix applied:** 13↔14 added to §3 rule 2's exceptions list with the ≈2× arithmetic and a
worked value; S1r annotated with the exception **and its fault signature restated** (the
failure is a beep / ≈0 Ω, not a finite resistance); PD1 points at S1r as well as S2r; §11's
S1r template row reads `OPEN; 13-14 per PD1`. And the list is now declared **closed by
construction**: any future revision that adds a deliberate resistance to the signal set must
extend it in the same edit. That instruction is the actual fix — the enumeration is only as
good as the next person who adds a part.

### F18 — CONFIRMED (found closing F15). PD1's pull-downs have no stated location and no fit moment — F16's defect, one row over.

**Where:** checklist PD1 (S4); guide §5 step 4 and §3 "what lives where"; §3 rule 2's
unseat-for-isolation instruction.

PD1 says *whether* the pull-downs are populated and *what value*. It never says **where they
sit** or **when they go on**, and the guide never mentions them at all — they appear in
neither §5's build steps nor §3's "what lives where" table. F16 is the same sentence about
C1.

It bites through §3 rule 2: **isolation rows are measured with the ESP32 unseated.** A
harness-side pull-down (at the socket position) is still in circuit with the board out, so
F15's exception reads true. A **board-side** pull-down — soldered to the module's own pin,
which is a perfectly natural reading of "GPIO13 → GND" — comes out with the board, so S2r
reads OPEN while PD1 says populated: two rows of one document contradicting each other on a
correctly built car, with no text to arbitrate. The same ambiguity decides whether F17's
13↔14 exception applies at all.

**Fix applied — decided, on the document's own precedent rather than escalated:** *if*
fitted, each pull-down lives **harness side at the ESP32 #1 socket position**, returning to
the star ground, fitted **at S4 with the board-end signal wiring**, before S4b's matrices
read it. This follows the placement rule F11 already set for the Hall pull-up and F13.3 for
C3, it is the only placement under which F15's exception is measurable, and it is the only
one that holds the ESC/steering line low with **no board seated** — which is the R04
boot-float window the part exists for. Written into checklist §2 (a visual bullet), PD1
(placement recorded, not just value), §11 (`value+where, or N/A`), plan §A2.5, and guide §5
step 4.

**Also recorded, because it changes what the bench should expect:** the honest A2-time state
is **NOT POPULATED**. R04's evidence is the B1.4 boot-float scope, which is Phase B and has
not run. Fitting nothing and recording that is the expected outcome, not a lapse — and adding
the pull-downs later invalidates no A2 row, it only moves 13/14 into the exceptions list for
any re-measure. PD1 and §A2.5 now say so.

### Naming record — the `S0` collision, settled (supersedes the 2026-08-04 "Register note")

The revision pass created gate **S0** (PDB frame, F8) while `S0 ≥ 9.82 mm` already existed as
the ZK cassette clearance. The first note here documented the clash and left both names
standing. That was the wrong call, and it was the second such collision in two days — the
R16 one produced a near-miss where a commit subject read as closing a Phase-B safety gate.

**Settled by renaming the gate: SF (PDB frame).** Reference sweep before deciding, since
"rename the newer one" is a rule of thumb, not evidence:

- **ZK clearance `S0`** — 7 workspace files, incl. a rendered diagram that uses it as a datum
  (`S0=0 → lower bound`), three Codex-handoff documents already sent, and a derivation formula
  (`S0 = board-top 32 + 5 − roof-Z`). Renaming it means editing documents already handed over
  and a published artifact. (The Codex repos themselves carry **no** `S0` — checked read-only;
  the exposure is the handoff docs on our side.)
- **Gate `S0`** — the checklist, this register, plan §A2, guide §5, `CURRENT_STATUS.md`. All
  on the two unmerged `docs/a2-revision-pass` branches. **Nothing published, nothing sent.**

So the gate is the cheap rename, as expected — but now on counted references. Row IDs stay
**P1–P7**. Every pointer into the checklist says **SF**; the **finding bodies of F8/F2/F12
keep their original "S0" wording as written history**, because falsifying what the reviewer
proposed on 2026-08-03 to tidy a name would cost more than the ambiguity it removes. The
checklist states flatly that **there is no gate S0** and that `S0` names the clearance only,
so the next grep is unambiguous in both directions.

Unlike R16 — annotated because the name was already load-bearing in two published series —
nothing here was published, so it was fixed while it was cheap. That is the rule the two
cases together establish: **rename before publication, annotate after.**

**Gate state after the revision pass and this closure pass: unchanged. A2 NOT-EXECUTED.
Phase B BLOCKED.** Closing findings makes A2 executable, not executed. The two F12 bench
measurements above remain **OWED and open** — this pass re-checked that neither has been
promoted anywhere in either repo.
