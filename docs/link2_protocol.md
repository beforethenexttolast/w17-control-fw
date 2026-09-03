# link2 protocol v2 — control board (ESP32 #1) → sound/light board (ESP32 #2)

One-way UART. **115200 baud, 8N1, 3.3 V logic, common ground.** Sender TX = ESP32 #1
GPIO25 → receiver RX on ESP32 #2. (GPIO26 is reserved for a future ack channel and is
not driven — do not connect anything to it yet.) Both boards power from the same UBEC
rail, so they come up together; avoid driving the line into an unpowered board #2 for
long periods.

Reference implementation: `lib/link2/` in this repo — **liftable wholesale** into the
board-#2 project (no dependencies beyond a byte-sink interface; the decoder and
`Link2FrameAssembler` are what board #2 needs).

## Ownership and the copy in soundlight-fw

**This repo owns the protocol.** Every change lands here first — this document and
`lib/link2/` together are the definition. `w17-soundlight-fw/lib/link2/` is a **verbatim
copy** of the shared subset (`include/link2/Link2Frame.hpp`, `include/link2/Link2Codec.hpp`,
`src/Link2Codec.cpp`, `library.json`); it deliberately omits `src/Link2Sender.cpp`, which is
control-side only. The copy is **permanent by decision (2026-07-25)**, not a bootstrap toward
a submodule — a submodule would drag the sender into board #2 as dead code that PlatformIO's
LDF would compile, for a four-file library that has never drifted.

**This document is also copied** into `w17-soundlight-fw/docs/link2_protocol.md`.

Because it is all copies, it is guarded rather than shared:

> **Reading this in `w17-soundlight-fw`?** Everything from here to the end of this section
> is **control-fw-local** and describes tooling that lives only in the owner repo. The
> checker script, the `pio test -e native` test names below, and `lib/crsf` are all
> control-fw paths — board #2 has none of them. Do not copy this section's claims forward as
> though they were local facts.
>
> **The normative content — frame layout, payload table, state matrix, timing rule, worked
> example — matches across both repos** and starts at *Frame layout* below. Note the precise
> claim: it is the normative *content* that matches, **not** the whole region byte-for-byte.
> (Corrected 2026-07-30 — this note previously claimed everything from *Frame layout* onward
> "**is** byte-identical across both repos", which is false and was verified false: the
> drive-mode naming commentary under *Drive mode* carries the same repo-local point-of-view
> split as this section — control-fw says "the CRSF FLIGHTMODE string this firmware emits",
> soundlight says "this board emits no CRSF whatsoever". That is legitimate local prose, not
> drift, but it sits inside the region the old sentence promised was identical.)
>
> **Why the wrong claim mattered:** the guard's doc tier reports this file as differing on
> every run. Someone trusting the old sentence would conclude the report *must* mean normative
> drift and go hunting — or, worse, learn to dismiss the report as noise and miss a real one.
> A guard that cries wolf teaches `--no-verify`; a doc that over-claims teaches the same habit.

- `tools/link2_copy_check.sh` (**control-fw only** — it does not exist in soundlight)
  compares against a checked-out `../w17-soundlight-fw`. Run it after **any** change to
  `lib/link2/` or to this document. Two tiers, deliberately:
  - **Fatal (exit 1)** — the four shared **code** files. They are compiled on both boards, so
    byte-identity is the right invariant and any difference is a bug.
  - **Reported (exit unchanged)** — *this document*. Its normative content (field table,
    lengths, CRC, the 500 ms staleness rule) must not drift, but byte-identity is the wrong
    bar: this copy legitimately carries repo-local prose, such as the section you are reading,
    which names tooling that does not exist in soundlight. A diff cannot separate normative
    drift from local commentary, so the script surfaces the difference and a human judges it.
    **If you change anything normative above, re-sync soundlight's copy.**
- `pio test -e native` pins the wire format hermetically from this side. **Control-fw only:**
  `test_golden_frame_bytes` fixes the exact golden-frame bytes, and
  `test_crc_matches_crsf_implementation` pins the CRC against `lib/crsf` — a library board #2
  does not have. Soundlight pins the same wire format from the receiver side with its own tests.

The copy-check is **enforced, not advisory**: `w17-soundlight-fw` runs it in strict mode in
its own CI as of **2026-07-25** (`2d22f85`, the `link2-drift` job in
`.github/workflows/ci.yml`). That job anonymously shallow-clones this repo into
`$RUNNER_TEMP` — outside `GITHUB_WORKSPACE`, so the sibling never enters soundlight's source
tree — and runs *this* repo's `tools/link2_copy_check.sh --strict` against its own checkout.
`--strict` means an absent sibling or a missing checker fails **loudly** (exit 2,
COULD-NOT-CHECK) rather than passing quietly. This repo owns the protocol and the script;
soundlight owns the enforcement, and it now exists. **This repo's own `link2-drift` job**
(`.github/workflows/ci.yml`) clones the soundlight checkout the other direction and now
calls the same `tools/link2_copy_check.sh --strict` (fixed 2026-09-03 — it previously
re-implemented a plain `diff -u` on all four shared files including this document, which
hard-failed CI on the this-repo-local prose the paragraph above calls out as legitimate;
that was never the tiered design, just a job nobody had reconciled with the script after
the script grew tiers). Both repos' CI now exercise the same two-tier logic, from opposite
directions.

## Frame layout (17 bytes)

```
offset  size  field
0       1     start byte, always 0xA5
1       1     length = payload byte count (14 in v2)
2       14    payload (below)
16      1     crc8 over bytes [1..15]  (length + payload; start byte excluded)
```

CRC8: polynomial 0xD5, initial value 0, MSB-first, no reflection (CRC-8/DVB-S2 — the
same algorithm CRSF uses; catalog check value for ASCII `"123456789"` is `0xBC`).

**Validation order:** start → length → CRC → version. `BadVersion` therefore means a
*well-formed frame from a newer sender*, not corruption. Receivers must hard-reject an
unsupported length byte the moment it arrives (do not buffer `length` unknown bytes —
a corrupted 0xFF length would otherwise swallow ~1 s of following frames). This same
obligation is what makes protocol bumps a coordinated flash — see *v1 → v2* below.

## Payload v2 — all multi-byte fields little-endian (low byte first)

| offset | size | field | semantics |
|---|---|---|---|
| 0 | 1 | version | `2`. Reject anything else. |
| 1 | 1 | throttlePercent | int8, −100…+100. **What the ESC is actually commanded** (0 while disarmed or failsafe) — engine sound should track this, not stick position. Negative = braking, **never reverse motion** (the ESC runs forward/brake). |
| 2 | 1 | steeringPercent | int8, −100…+100. Left/right for turn indicators. Live even while disarmed. |
| 3 | 1 | flags | bit0 braking (already hysteresis-filtered by the sender — drive the brake light from it directly), bit1 reverse (**reserved since v1, always 0 — do not key anything off it**), bit2 drsOpen, bit3 armed, bit4 failsafe, bit5 lowBattery, bit6 ersDeploying (boost/overtake actively draining — e.g. an ERS whine sound layer), bit7 reserved (sender writes 0, **receivers must mask, never reject**). |
| 4 | 1 | gear | 1-based display gear, 1…4 (matches the firmware gearbox numGears). |
| 5–6 | 2 | rpm | uint16. **Wheel/axle rpm** (one magnet), plausible max ~5000 — *not* engine rpm; derive engine revs from throttlePercent or scale this. |
| 7–8 | 2 | batteryMv | uint16, 2S pack millivolts. Display garnish — the `lowBattery` flag is the authoritative judgment (calibrated, 3 s-qualified, hysteresis-latched on board #1). |
| 9 | 1 | ersPercent | 0…100, ERS energy store. Frozen (not zero) outside ERS mode. |
| 10 | 1 | driveMode | 0 = TRAINING, 1 = RACE (gearbox), 2 = ERS (gearbox + ERS deploy). Receivers may vary engine character per mode; treat unknown values as 1 (RACE). |
| 11 | 1 | soundProfile | **v2.** Engine voice: 0 = V10 (the default), 1 = V6 turbo-hybrid. Values ≥ 2 are **reserved**: receivers MUST fall back to 0 (V10) — a voice fallback, **never a frame rejection** — so a receiver that predates a future voice keeps making sound instead of going mute. Mirrors `driveMode`'s unknown-value rule. |
| 12 | 1 | volume | **v2.** 0…100 engine-sound level. **0 = true silence** (bit-exact zero synth output), 100 = full output, applied in integer math at the synth's final gain stage; receivers clamp values >100 to 100. Scales **sound only, never lights**. Default 80 — loud-but-not-max: showpiece-loud out of the box with headroom left below full scale (`sound.volume` in the board-#1 tuning console). Failsafe silencing ALWAYS wins over this field (see the state-matrix note). |
| 13 | 1 | modeFlags | **v2.** bit0 `showcase` — **LIVE (showcase-mode wave, 2026-08-17)**: board #1 booted in its stationary-demo SHOWCASE state. Boot-selected and constant for the whole session — set in **every** frame of such a boot (failsafe frames included), never in a DRIVE boot. Every other field stays truthful (`armed` 0, `throttlePercent` 0, real battery/steering); the `failsafe` flag follows the showcase-scoped rule under the state matrix below. Receivers key engine ignition on `armed \|\| showcase` and MUST treat the bit as **command-class on staleness** (zeroed in the local-failsafe projection, like `armed` — a stale showcase indication must not outlive the link). bit1 `awaitingController` — still reserved for the BT show-off design's §6.3 pairing-state surface (**always 0 today**; no receiver behavior may key off it until that mode ships). bits 2–7 spare: sender writes 0, **receivers mask/ignore, never reject** (same discipline as `flags` bit7). |

### v1 → v2 (2026-08-17) — a COORDINATED FLASH of both boards

v2 appends three payload bytes: `soundProfile` and `volume` — carrying the engine-voice
selection and volume/quiet level from board #1's persisted settings to board #2's synth
(vision decision 15; mechanism decided at the 2026-08-16 owner review) — and `modeFlags`
(owner decision 2026-08-17). Framing, the CRC algorithm, field order and every v1 field's
offset are unchanged; the length byte goes 11 → 14 and the version byte 1 → 2.

**Why `modeFlags` was introduced empty:** the accepted showcase-mode design (D2) and the
BT show-off design (BT-7) had each earmarked `flags` bit7 — the byte's last free bit — for
their own future state, a collision the owner resolved on 2026-08-17 with a dedicated
`modeFlags` byte. Reserving both bits inside this bump means **one coordinated flash
covers both future modes**: switching either mode on later is a sender-behavior change on
an already-flashed wire format, not another protocol bump. That is exactly how bit0 then
went live in the showcase-mode wave — a sender-behavior change, **no bump, no third
coordinated flash**; bit1 still awaits the BT mode. `flags` bit7 stays reserved.
(A 13-byte-payload v2 draft existed briefly during development and was **never flashed**;
the shipped v2 is this 14-byte-payload form only — a length-13 frame is rejected exactly
like a v1 frame.)

**There is no mixed-version operation.** The receiver obligation above — hard-reject an
unsupported length byte the moment it arrives — cuts both ways:

- a board #2 still on v1 firmware rejects every v2 frame **at the length byte** and sits
  in its 500 ms staleness failsafe indefinitely — engine silent, hazard blink;
- a board #2 on v2 firmware rejects every v1 frame the same way.

Rejected frames never reach version parsing, so neither board can tell "the other side
is one protocol version behind" from a cut wire — the hazard state is the only symptom.
A mismatch is therefore **safe but totally non-functional** (this link carries no
control authority; the failure mode is silence + hazards, nothing worse). **Whenever
this protocol's length changes, flash both boards in the same bench session** and treat
the pair as one unit of deployment.

### driveMode: wire values vs display labels (audit R19, decided 2026-07-25)

`driveMode` is a **number on the wire**; the names above are the shipping *display* labels.
**TRAINING / RACE / ERS** is what a person reads — the ground-station HUD shows exactly
these. Two nearby spellings are deliberate, not drift:

- The iPhone canonical contract maps the same three values to the enum strings
  `TRAINING` / `GEARBOX` / `GEARBOX_ERS` (`w17-ground-station/shared/telemetrySnapshot.js`).
  Those are **wire identifiers for a machine consumer**, not labels; the HUD maps them back
  to RACE / ERS before anything is displayed.
- The CRSF FLIGHTMODE string this firmware emits is `"G%u M%u E%u"` — `driveMode` goes out as
  a bare integer (`M2`), so a handset displaying the raw string shows **no mode label at all**
  and cannot diverge.

So there is no user-visible contradiction to fix, and none of these three surfaces needs to
change. Verified 2026-07-25.

## State matrix

| condition | throttlePercent | braking | armed | failsafe | telemetry (rpm/battery) |
|---|---|---|---|---|---|
| failsafe (link lost) | 0 | 0 | 0 | 1 | still live |
| disarmed idle | 0 | 0 | 0 | 0 | live |
| driving | as commanded | as filtered | 1 | 0 | live |

**SHOWCASE boots** (`modeFlags` bit0 = 1 in every row below) add three rows and carry the
protocol's **single showcase-scoped exception** — the `failsafe` flag rule:

| condition (showcase boot) | throttlePercent | armed | failsafe | modeFlags bit0 |
|---|---|---|---|---|
| shelf demo — no CRSF link EVER this boot | 0 | 0 | **0** | 1 |
| table demo — CRSF link up | 0 | 0 | 0 | 1 |
| CRSF link existed, then died mid-session | 0 | 0 | **1** | 1 |

In a SHOWCASE boot the drive-authority fields keep their exact semantics from the main
matrix — `armed` can never assert (the arm-switch input is structurally pinned false into
the unchanged arm gate) and `throttlePercent` remains "what the ESC is actually
commanded," which is therefore always 0 — but `failsafe` follows a deliberately narrowed
rule (owner decision D4, 2026-08-17): **assert only when the CRSF failsafe FSM is Safe
AND a CRSF link existed at least once this boot** (`Safe && everLinkedThisBoot`). A shelf
demo with no radio ever powered transmits `failsafe = 0` — nothing that matters is lost:
there is no drive authority to lose, and the battery stays honestly monitored — while a
table demo whose radio dies mid-session still transmits `failsafe = 1` and the receiver
hazard-blinks. This is the NeverConnected-vs-Lost distinction the receiver already
applies to *this* link's staleness, applied one level up to the CRSF link. **DRIVE boots
are byte-unchanged**: `failsafe` is the FSM state, exactly the main matrix, test-pinned
(a never-linked DRIVE boot still transmits `failsafe = 1`, today's behavior).

**BT_SOLO boots have no rows of their own — they emit exactly the main matrix above,
byte-unchanged from DRIVE.** All three per-frame policies pass BT_SOLO straight through
to the DRIVE behavior: `armSwitchInput()` only forces the switch false in Showcase, so in
BT_SOLO `controls.armSwitch` is live and is the BT pad's arm ritual, not a CRSF stick
(`lib/bootmode/include/bootmode/BootMode.hpp:163-165`); `link2FailsafeFlag()` only
narrows the rule in Showcase, so BT_SOLO reports the raw FSM `Safe` state exactly like
DRIVE (`BootMode.hpp:194-196`); and `link2ShowcaseFlag()` — modeFlags bit0 — is false for
every mode except Showcase, so a BT_SOLO frame never sets it
(`BootMode.hpp:199-206`; "BtSolo NEVER sets it" is the comment's own words). A receiver
therefore needs no BT_SOLO-specific handling: if `modeFlags` bit0 is 0, the three rows at
the top of this section already describe the frame, regardless of which non-showcase boot
mode produced it.

`soundProfile` and `volume` are deliberately absent from this matrix: they are
**configuration, not state**. The sender transmits its current persisted values in every
frame under every condition above — they never change with failsafe/arm state. Receiver
silencing on link loss comes from the `failsafe` flag and the 500 ms staleness rule via
the receiver's own engine state machine, and **always wins over `volume`**: a car with
`volume` 100 still goes silent in failsafe, and a car with `volume` 0 is silent even
while driving.

## Timing — and the one rule the receiver MUST implement

Frames are sent at a nominal **20 Hz** (every 50 ms). Receivers must tolerate jitter and
must not phase-lock to the rate.

**Mandatory staleness timeout:** if no CRC-valid frame arrives for **500 ms** (10 missed
frames), the receiver must enter its own local failsafe — engine sound to idle/off,
hazard blink. On a one-way link, a cut wire is otherwise indistinguishable from "the
last state persists forever".

## Worked example

The byte-identical frame is pinned by `test/test_link2/test_main.cpp`
(`test_golden_frame_bytes`):

```
A5 0E 02 2A E7 4C 03 DC 05 DC 1E 3C 02 01 50 00 5A
│  │  │  │  │  │  │  └─┴─ rpm 1500    └─┴─ battery 7900  │  │  │  │  │  └ crc8
│  │  │  │  │  │  └ gear 3                               │  │  │  │  └ modeFlags 0 (both reserved bits off)
│  │  │  │  │  └ flags 0x4C = drsOpen | armed | ersDeploying │  │  └ volume 80
│  │  │  │  └ steeringPercent = 0xE7 = −25                │  │  └ soundProfile 1 (V6)
│  │  │  └ throttlePercent = 0x2A = +42                   │  └ driveMode 2 (ERS)
│  │  └ version 2                                         └ ersPercent 60
│  └ length 14
└ start
```
Decoded: throttle +42 %, steering −25 %, DRS open, armed, ERS deploying at 60 %
store in ERS mode, no failsafe, gear 3, wheel 1500 rpm, battery 7.900 V, engine
voice V6 turbo-hybrid at volume 80/100, no mode flags set.

A second golden pin covers the showcase bit: `test_showcase_golden_frame_bytes`
re-encodes this same state with `showcase = true` — byte-identical except `modeFlags`
`0x00` → `0x01` and crc `0x5A` → `0x8F` (the last three bytes read `50 01 8F`). That is
a **codec pin, not a plausible frame**: a real SHOWCASE boot transmits `armed = 0` and
`throttlePercent = 0` (state matrix above).
