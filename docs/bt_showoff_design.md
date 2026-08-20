# BT show-off mode — design (canonical; decisions ratified, merge pending)

**Status: canonical design document; all eleven §8 decisions RATIFIED by the owner
2026-08-17; prototype implemented on this branch; not yet merged.** This is the canonical
location for the BT show-off design (it originated as a scratchpad draft dated 2026-08-16,
committed here the same day; content unchanged apart from this header, the §8 decision
outcomes, and the dated amendment notes marked "Owner 2026-08-17"). Commissioned by the
owner 2026-08-16 as the design half of the approved "design + default-off compile-flagged
branch prototype in one pass" (`../W17_PRODUCT_VISION.md`, backlog item *BT show-off
mode*, decision 12).

The prototype lives on this branch (`proto/bt-showoff-flagged`, originally based on
`main` = `94b3615`, rebased 2026-08-17 onto `main` = `2d146dc` — the unified settings
blob v2 and link2 protocol v2): `W17_BT_SHOWOFF` is absent from every existing
environment, nothing is flashed, nothing has run on hardware, nothing is pushed. The
owner ratified every §8 recommendation on 2026-08-17 (one scope simplification on BT-9;
BT-7 resolved onto the link2 v2 `modeFlags` byte) — each decision point in code and docs
now carries a grep-able `OWNER-DECIDED(BT-n)` tag (n = the §8 row number), so the full
consequence set of any decision remains a grep away. Bench execution stays gated on BT1
(§9) regardless of ratification.

Repo state studied for the draft: `w17-control-fw` at `3f4f9b7` (read via an isolated
audit worktree — the repo's own tree was not touched). Every firmware claim below cites
the file it comes from; every external number cites a URL in *Sources*. Where a claim is
an assumption or a to-be-verified item, it says so inline.

---

## 1. Purpose & scope

**What it is.** A DualShock 4 pairs directly to ESP32 #1 over Bluetooth Classic and the car
drives like a basic RC car at close range with **no PC, no handset, no phone** — the "look
what I have here" scenario. Sound and light keep working (link2 to board #2 is downstream
of arbitration and is unchanged), so the demo is: key in, pad on, gentle driving with
engine noise and lights.

**What it is not (explicit non-goals):**

- **Not the primary drive mode.** The laptop/ELRS chain remains the product's drive path
  (vision decision 12); this mode dilutes "Windows is the control authority" exactly the
  way the approved ELRS backup handset does — accepted by the owner, gated per process.
- **No iPhone anything.** No iPhone input, no iPhone output, no head tracking. Firmware
  stays iPhone-unaware (workspace boundaries 1–4). A BT gamepad is a new *raw* control
  input arbitrated in firmware like CRSF — it is not, and must never become, a relay for
  any phone-derived signal.
- **No WiFi.** Board #1 initializes no WiFi stack in any mode, including this one.
- **No gimbal from the pad.** Workspace boundary 4's parenthetical is explicit: "Gimbal
  pan/tilt is stick-driven **CRSF ch9/10 only**, source-agnostic" (`../CLAUDE.md`). A pad
  stick is not CRSF ch9/10, so in BT mode the gimbal is held at center. Changing this
  requires the owner to amend the boundary wording first — it is listed as an open
  decision, not assumed. **[Owner 2026-08-17, OWNER-DECIDED(BT-9), scope superseded:
  BT mode is car-control only — the camera SYSTEM is OFF in this mode ("a simple mode
  for quick showing off"). Gimbal-fixed-center remains the implementation, but the
  rationale is now camera-off scope, not merely boundary-4 wording; the boundary text
  itself stays untouched.]**
- **No telemetry uplink.** In BT mode the CRSF UART is never opened, so no battery/GPS/
  flightmode frames are emitted (there is no ground station in this scenario to read them).
- **No new hardware outputs.** Firmware remains the only producer of final hardware
  outputs, from already-arbitrated inputs (boundary 6) — this design adds an input, not an
  output path.

---

## 2. Mode model — CRSF mode vs BT mode, selected at boot only

### 2.1 Two exclusion layers

**Layer 1 — compile flag (`W17_BT_SHOWOFF`).** Default OFF in every existing environment
(`esp32dev`, `esp32dev_sim`, `esp32dev_tuning`, `native` — `platformio.ini`). With the flag
absent, no BT code, no Bluepad32, and no boot-mode selector exist in the binary at all; the
delivery firmware is bit-for-bit unaffected by this feature's existence. Verification is
the same class as the existing console invariant ("ELF-verified", repo `CLAUDE.md`
*Delivery vs tuning builds*): the delivery ELF must contain no `btpad`/Bluepad32 symbols —
add a `strings`/`nm` spot-check next to the existing one in the delivery runbook
(`docs/D8_BENCH_BRINGUP.md` Phase 11a analog).

**Layer 2 — boot-time selection (flag ON, prototype env only).** `setup()` resolves a
`BootMode` (CRSF | BTPAD) **once**, before any input stack is initialized, and the value is
`const` for the life of the boot:

- **CRSF mode:** the boot path is exactly today's `setup()`/`loop()` — `crsfUart.begin()`,
  CRSF drain, telemetry emission. The BT stack is **never initialized**: no
  `BP32.setup()`, no BTstack run loop, no radio-on. Not "BT ignored" — BT *absent* at
  runtime.
- **BT mode:** `crsfUart.begin()` is never called, the CRSF drain loop and the three
  telemetry emitters are compiled behind the mode check and never run — CRSF is ignored
  end-to-end because the UART is never opened and no byte is ever read. (The RP1 receiver
  remains physically powered on the UBEC rail and may bind to a live handset; that is
  harmless by construction — no code reads its output in this mode.)

There is **no runtime switch** in either direction; changing mode = reboot. This is a
feature, not a limitation: a mode transition mid-drive is exactly the ambiguity this design
exists to exclude.

### 2.2 Boot-selection mechanisms (proposals)

The eventual operator is a non-hobbyist giftee ("user friendly af",
`../W17_PRODUCT_VISION.md` operator model), so the selector must be a physical, labeled,
un-screw-uppable thing — and it must have exactly **one** source of truth.

| # | Mechanism | How it works | Giftee UX | Risk / cost |
|---|---|---|---|---|
| **A** | **Boot-strap GPIO switch** (recommended) | Spare GPIO, internal pull-up, read once in `setup()` (≥10 ms settle + majority-of-N sample). Switch to GND = BT mode; open/floating = CRSF mode — **fails toward the normal, most-tested mode** on any wiring fault. | A labeled 2-position slide switch on the cassette ("LAPTOP ◂▸ SOLO"), reachable under the engine cover — the shell itself stays unmodified (owner constraint). Flip + key-cycle. | One GPIO consumed; one switch in the cassette BOM; placement is a Codex-fit question. Mid-session flips do nothing until reboot (by design). |
| B | NVS mode flag via tuning console | New console command writes a mode field; boot reads it. | **Fails the scenario's own premise**: changing mode would require the laptop + tuning build — the mode exists precisely for "no PC available". | Rejected as the selector. (The console may still *display* the strap state read-only.) |
| C | Double-key-cycle NVS toggle | Boot writes a "booted" marker to NVS; a second power-cycle within ~5 s toggles the mode ("pull the key, reinsert within 5 s = solo mode"). RTC memory can't do this (lost on power-off — `src/main.cpp` R5-b comment), so it needs an NVS **write on every boot in delivery**, relaxing the standing "delivery is NVS read-only, mutation only via console" invariant (repo `CLAUDE.md`) and adding flash-wear + write-timing considerations in the boot path. | Car-like and lovely — the strongest giftee story. | Real invariant change + its own failure analysis (interrupted write, wear). **Deferred: candidate for v2, not the prototype.** |

**Recommendation: A for the prototype and, most likely, the product.** It is
deterministic, testable as a pure function (pin level → mode), fail-safe toward CRSF, and
honest to a non-hobbyist ("the switch says what the car will listen to"). C is recorded as
the future UX upgrade if the owner wants zero visible switches; it needs its own small
design pass because of the invariant it touches.

**Pin proposal:** GPIO27 (alternatives: 32, 33). Free in `lib/config/include/config/PinMap.hpp`
(used today: 16/17 CRSF, 25/26 link2, 13/14/18/19/23 PWM, 34/35 input-only sensors), not a
strapping pin (0/2/12/15 avoided per repo `CLAUDE.md` pin rules), supports internal
pull-up. Final say per repo rule: reconcile against `PinMap.hpp` + the wiring atlas at
implementation, and note the A2 interaction — a new wired pin joins the continuity-matrix
scope question already tracked as F20 (`../CURRENT_STATUS.md` 2026-08-04 entry).

**[2026-08-17 three-mode reconciliation — ONE selector seam.]** The showcase-mode merge
brought `lib/bootmode` (Drive/Showcase) to `main`; this branch folded its own
`btpad::BootModeResolver` INTO that seam rather than shipping a second boot-mode concept:
`bootmode::BootMode` now carries all three modes (**Drive** = LAPTOP, **Showcase** =
SHOW, **BtSolo** = SOLO, this design's mode), `bootmode::classifyStrapLevels()` is this
section's majority-vote sampling recast as levels → position, and the three mode policies
(arm input, D4 failsafe flag, showcase bit) are total over all three modes with showcase
behavior untouched (`test_bootmode`). Composition rule in `src/main.cpp`: the constexpr
`StrapReading` injection is a bench override; `Floating` (default) means the
`W17_BT_SHOWOFF` envs read the physical GPIO27 strap once at boot, and every other build
constant-folds to Drive. **The one open selector corner — how SHOWCASE gets a physical
position when the decided GPIO27 strap is a two-position LAPTOP/SOLO part — is
`OWNER-PENDING(D3-SHOW-SELECT)`:** proposal in `lib/bootmode/include/bootmode/BootMode.hpp`
(SP3T slide switch, common to GND, throws on GPIO27 = SOLO and GPIO32 = SHOW, center =
LAPTOP; any ambiguous read classifies Floating → Drive). Until decided, SHOWCASE remains
selectable via the compile-time injection only, exactly as the showcase branch shipped it.
The NVS-override idea (mechanism B/C territory) stays deferred per this table.

---

## 3. Safety invariants — mapped 1:1 from the CRSF path

The four non-negotiable priorities (repo `CLAUDE.md` *Safety priorities*) map as follows.
The core claim of this design: **the BT path reuses the same arbitration objects, not
parallel re-implementations** — the mapping is 1:1 because it is the same code.

| # | CRSF-path invariant (today) | BT-mode equivalent (this design) |
|---|---|---|
| 1 | **Failsafe first.** `FailsafeStateMachine` over (nowMs, frame-arrived, rx-failsafe-flag); no frame for 500 ms ⇒ Safe; Safe ⇒ throttle neutral (ESC 1500 µs), steering center, DRS closed; latched until the link PROVES itself: continuously good 150 ms **and** ≥5 frame-bearing ticks with no >60 ms intra-proof gap (2026-08-20 hardening — one CRC-valid frame can never reach Active; loss timing unchanged) (`lib/failsafe/include/failsafe/FailsafeStateMachine.hpp`, `src/main.cpp` Safe branch). | **Same machine, same instance, same Config.** Inputs become: frame-arrived = "a new report from the bonded pad was consumed this tick"; failsafe-flag = "stack reports the pad disconnected" (latched by `onDisconnectedController`, cleared only by reconnect + first report — mirror of the LQ==0 latch semantics in `lib/crsf/include/crsf/CrsfReceiver.hpp`). Staleness timeout: **same 500 ms**, same ~540 ms worst-case detection incl. tick quantization (`src/main.cpp` control-tick comment). Same Safe branch writes the same outputs. Failsafe timing stays **non-tunable** in both modes (deliberately outside `settings::Settings`, `lib/settings/include/settings/Settings.hpp`). |
| 2 | **Arm gate.** Throttle passes only when arm control ON *and* throttle seen at neutral since last disarm (`lib/channels/include/channels/ArmGate.hpp`). | **Same `ArmGate` instance**, same `update(armSwitchOn, throttle, forceDisarm)` call in the same tick position. `armSwitchOn` comes from a pad **arm ritual** (below) instead of ch5. Neutral latch, forceDisarm-on-Safe, no-arm-into-full-throttle: unchanged code. |
| 3 | **ESC boot arm sequence.** Neutral held `bootArmHoldMs` = 2000 ms from the first `setThrottle()` (`lib/outputs/include/outputs/EscOutput.hpp`). | **Untouched.** `EscOutput` neither knows nor cares which mode feeds it. |
| 4 | **Battery telemetry warn-only.** `BatteryMonitor` monitors, warns via link2 `lowBattery`; never auto-cuts (repo `CLAUDE.md` 6.4). | **Untouched.** Same sampling cadence, same link2 flag; board #2's pulsing lights remain the giftee-visible warning (there is no HUD in this scenario). Optional pad-side mirror (lightbar/rumble) is output-only garnish — §6. |

### 3.1 Failsafe timing analysis (why staleness is primary)

DS4 over BT streams input reports continuously while connected (it includes IMU data), so
"no report for 500 ms" is a reliable loss signal. The stack's own disconnect event is the
*secondary* trigger: BT Classic link-supervision timeouts are typically seconds, so the
callback can lag a real RF loss — the 500 ms staleness clock, fed per-tick exactly like the
CRSF frame timeout, is what bounds the car's reaction. Both signals feed the same state
machine, mirroring CRSF's two independent loss signals (frame timeout + latched LQ==0).
**Assumption to bench-verify:** the DS4's idle report rate really is continuous (a
change-only-reporting controller would nuisance-trip staleness at constant input — one more
reason this design specifies DS4 only).

### 3.2 Pad arm ritual (proposal)

- **Arm:** hold **L1 + R1 simultaneously for `btpad.armHoldMs`** (tunable, default
  1000 ms) → ritual latch ON. `ArmGate` still requires throttle-at-neutral (triggers
  released; released triggers read 0, inside the ±60 neutral window,
  `lib/channels/include/channels/ArmGate.hpp`) before the motor may run.
- **Disarm:** single press of **OPTIONS** → latch OFF instantly. Disconnect → OFF.
  Failsafe episode → OFF (see below). PS button is deliberately unmapped (it is the pad's
  own power/pairing button; long-press powers the pad off, which lands in the disconnect →
  failsafe path).
- **Stricter-than-CRSF choice, flagged for the owner:** on the CRSF path a failsafe episode
  does *not* clear the physical arm switch — recovery re-arms once neutral is re-seen. For
  the giftee demo this design proposes the ritual latch **is** cleared by any failsafe
  episode, so recovery requires the full deliberate hold again. This is strictly more
  conservative than the CRSF semantics (never weaker); if the owner prefers exact-mirror
  semantics instead, it is a one-line change. Open decision §8-3.

### 3.3 Reduced demo envelope

BT mode never gets full authority. Proposal (all **tunables** in `settings::Settings`, not
constants, per the same bench-tuning philosophy as the gear table):

- `driveMode` is **pinned to 0 (TRAINING)** in the decoded controls: one fixed gentle
  shape, gear paddles inert — the exact semantics the training mode already has
  (`src/main.cpp` drive-modes comment). ERS can never activate (it requires driveMode 2).
- The shaping parameters come from a new `btpad` sub-config instead of the compiled
  `kTrainingGearParams{400, 50}`: `btpad.maxOutput` (default **400**/1000, same as
  training's cap) and `btpad.expoPercent` (default **50**) — proposed values, owner-tunable
  at the bench like every feel value.
- Steering: full range, with `btpad.steerDeadzone` (default **40**/1000 ≈ 4%, covers DS4
  stick drift; proposed value).
- Optional upgrade (owner decision §8-4): let the gear paddles shift the virtual gearbox
  for the sound show, with output clamped to `btpad.maxOutput` — deferred from v0 to keep
  the prototype's arbitration delta near zero.

Settings impact: `settings::Settings` grows the sub-config **unconditionally** (in every
build, flag or no flag — a flag-conditional struct would fork the blob layout between
envs and break tuning-build ↔ delivery blob compatibility); only its *consumption* is
flag-gated. `kBlobVersion` bumps 1 → 2; a stored v1 blob then falls back to complete
defaults by the documented guard chain (`lib/settings/include/settings/Settings.hpp`) —
one-time re-save of bench tuning after the first flash of a v2 build, acceptable
pre-delivery.

### 3.4 Control mapping (proposal)

| Pad control | Bluepad32 raw (verify exact ranges against the pinned version at implementation) | Decoded `channels::Controls` field |
|---|---|---|
| Left stick X | axis ≈ −512…+511 | `steering` −1000…+1000 (deadzone, invert tunable) |
| R2 trigger | 0…1023 | `throttle` 0…+1000 (forward) |
| L2 trigger | 0…1023 | `throttle` negative (brake; net = R2 − L2, clamped — ESC runs forward/brake, so this is brake, never reverse, matching the gearbox note in `lib/gearbox/include/gearbox/Gearbox.hpp`) |
| L1 + R1 held `armHoldMs` | buttons | arm ritual → `armSwitch` |
| OPTIONS | buttons | instant disarm |
| Square | buttons | `drsSwitch` toggle (cosmetic; owner decision §8-5) |
| Everything else | — | ignored in v0; `pan`/`tilt` fixed 0; `driveMode` pinned 0; gear edges never fire |

---

## 4. Architecture

### 4.1 Data flow — one arbitration path, two heads

```
CRSF mode (unchanged):                         BT mode (new head, same spine):

 RP1 → UART2 → CrsfReceiver                     DS4 → BT Classic → Bluepad32 (HAL)
          → ChannelDecoder ─┐                        → btpad::PadDecoder ─┐
                            ▼                                             ▼
                    channels::Controls  ← ← ← same struct → → →  channels::Controls
                            ▼
        FailsafeStateMachine → ArmGate → shaping (gearbox/training/btpad envelope)
                            ▼
        ServoOutput / EscOutput / DrsOutput / gimbal servos   (untouched)
                            ▼
                    Link2Sender → board #2 sound + light      (untouched)
```

`channels` (ArmGate), `gearbox`, `failsafe`, `outputs`, `link2`, `telemetry`, `ers`,
`settings` — **no changes to their logic**. The only shared-code edits are: `settings`
grows the `btpad` sub-config (§3.3), `main.cpp` grows the mode branch at the input stage,
and the 50 Hz tick body is factored so both heads feed the identical tick (a small
extraction, not a rewrite — the tick already consumes `controls` + flags).

### 4.2 New modules (repo conventions: pure logic split from hardware, repo `CLAUDE.md`)

- **`lib/btpad`** — pure logic, no Arduino/ESP32 headers, fully native-testable:
  - `PadFrame` — plain struct of raw pad state (axes, triggers, button bits, sequence/
    connected info) as delivered by the HAL seam.
  - `IPadSource` — the seam interface: `bool poll(PadFrame&)` (new report since last
    poll), plus connected/disconnected latches. Mirrors the role `hal::ICharIO`/UART seams
    play elsewhere.
  - `PadDecoder` — `PadFrame` → `channels::Controls` (deadzone, trigger math, arm-ritual
    state machine with injected time, DRS toggle). Analog of `ChannelDecoder`.
  - `BootModeResolver` — pure function: sampled strap-pin level(s) → `BootMode`.
- **`lib/btpad_hal_esp32`** — the real Bluepad32 wrapper implementing `IPadSource`;
  **referenced only from `src/main.cpp`** under `W17_BT_SHOWOFF`, exactly the pattern of
  the five existing `*_hal_esp32` libs; added to the native env's `lib_ignore` list
  (`platformio.ini`). Also owns pairing policy calls (`enableNewBluetoothConnections`,
  `forgetBluetoothKeys`) and optional lightbar/rumble output.
- **Native mock** — `FakePadSource` in `test/mocks`, scripted like the existing test
  doubles; a new `test/test_btpad` suite (§7).

### 4.3 Build integration

- **Flag:** `-DW17_BT_SHOWOFF`. Absent from every existing env; delivery/tuning/sim
  binaries are unchanged.
- **New env:** `[env:esp32dev_btshowoff]` extends `env:esp32dev` (with the mandatory
  `${env:esp32dev.build_flags}` interpolation — `platformio.ini` warns a child
  `build_flags` *replaces* the parent's) plus the flag, plus the Bluepad32 core override
  and a larger app partition (`board_build.partitions = huge_app.csv`; §5). NVS partition
  offset is unchanged between the stock and huge_app tables, so stored tuning survives —
  verify once at the bench.
- **The honest wrinkle — Bluepad32 replaces the Arduino core.** Bluepad32 works by
  swapping the precompiled arduino-esp32 core for one built with **BTstack instead of
  Bluedroid** (custom board package / template project; Bluepad32 docs). Two consequences,
  stated plainly:
  1. The BT env's core is **not** the pinned stock `espressif32 @ ~7.0.1` core, even for
     non-BT code. That is exactly why the flag is OFF everywhere else: the prototype env is
     quarantined, and the delivery build keeps the audited stock core.
  2. **Version pin problem:** current Bluepad32 (v4.2.0, 2025-01-03) requires ESP-IDF
     5.3/5.4 and Arduino Core 3.1 — incompatible with this repo's deliberately pinned core
     2.0.17 / IDF 4.4, whose channel-based LEDC API the outputs HAL depends on
     (`platformio.ini` audit-R02 comment). The last Bluepad32 line built against IDF 4.4 is
     **3.10.3 (2023-11-26)**. The prototype therefore pins the 3.10.x-era artifact; the
     exact `platform_packages` URL is a verify-at-implementation item. Fallback if that
     artifact proves unusable: a custom esp32-arduino-lib-builder Bluedroid core with
     `CONFIG_BT_HID_HOST_ENABLED` (it is **disabled in the stock precompiled core**, so
     stock-core esp_hidh is not an option either) — same custom-core cost, more raw HID
     work, kept only as the fallback. Migrating the whole repo to core 3.x for Bluepad32
     4.x is rejected for now (LEDC HAL rewrite + re-audit of a reviewed codebase for a
     demo feature). Owner decision §8-8.
- **License note (for the post-gift public repos, vision decision 18):** Bluepad32 is
  Apache-2.0; the bundled BTstack is BlueKitchen's license — free for open-source
  projects, commercial license required for closed-source. A public hobby repo qualifies;
  record the notice when publishing.

### 4.4 Exclusivity, enforced structurally

- Flag OFF ⇒ no BT symbols in the binary (link-level absence, ELF-checkable).
- Flag ON + CRSF mode ⇒ `BP32.setup()` never called; radio never initialized.
- Flag ON + BT mode ⇒ `crsfUart.begin()` never called; the drain loop and telemetry
  emitters sit behind the mode check; no CRSF byte is ever read.
- `BootMode` is resolved once, `const`, before either stack starts. No code path exists
  that re-evaluates it.

---

## 5. Resource & timing budget

Numbers below are **planning envelopes from cited sources**, not measurements of this
firmware. The bench gate (§9) owns the real numbers.

**Flash.** BT Classic host + controller code is heavy: a trivial Arduino sketch with
`BluetoothSerial` (Bluedroid) compiles to **849,303 B — 64% of the default 1,310,720 B app
slot** (arduino-esp32 #1712), i.e. the stack costs roughly +550–600 KB over a minimal
sketch. Current firmware size + BTstack + Bluepad32 may or may not clear the default
`default.csv` slot; the BT env therefore plans `huge_app.csv` (≈3 MB app, sacrifices OTA —
this project uses no OTA) from the start. BTstack is generally leaner than Bluedroid;
treat the Bluedroid figure as worst case. **Measure:** `pio run -e esp32dev_btshowoff`
size report — a CI-able check, no hardware needed.

**RAM.** Bluedroid Classic BT costs ≈**140 KB of heap** total (~70 KB base BT reservation
+ ~70 KB on stack start; esp32.com t=3139, arduino-esp32 #6451). WROOM-32 has 520 KB SRAM;
a typical Arduino sketch boots with ~290–300 KB free heap, so worst case leaves
≈150 KB free. This firmware's own steady-state heap use is near zero (all module objects
are namespace-scope statics, `src/main.cpp`), so the envelope passes on paper with margin.
Bluepad32/BTstack claims a smaller footprint than Bluedroid but publishes no number —
budget with the Bluedroid figure, **measure the free-heap watermark at the bench**.

**CPU / timing.**

- Core placement: the BT controller (and BTstack host) are expected on core 0; the entire
  control loop is the Arduino loopTask on core 1 (`CONFIG_ARDUINO_RUNNING_CORE=1`, already
  documented in `src/main.cpp`'s TWDT comment). `BP32.update()` is a poll from loop
  context — the new per-pass cost on core 1 is a queue drain + decode, same order as the
  CRSF drain. Verify the pinned Bluepad32 build's sdkconfig core-pinning at
  implementation.
- **TWDT interaction:** `esp_task_wdt_init(2, true)` moves the already-subscribed core-0
  idle task to a 2 s deadline too (`src/main.cpp` R5-a comment). BT controller load on
  core 0 must never starve core-0 idle for 2 s. Expected fine (BT tasks block on events),
  but this is precisely the class of claim that is **bench-only**.
- 50 Hz tick semantics, tick-guard math, and the ~540 ms worst-case failsafe detection
  bound are unchanged — BT mode changes what feeds the tick, not the tick.

**Power / RF.** BT TX bursts add tens of mA average on the 3.3 V rail (datasheet-level
estimate to be attached at implementation; not cited here). RF environment: BT at 2.4 GHz
next to the still-powered ELRS RX antenna (RX stays on the UBEC rail even in BT mode) —
close-range demo use is expected to tolerate it, but it is unproven.

**Bench-only list (these become gate items, §9):** actual flash fit; free-heap watermark
(boot, pairing, driving, disconnect storm); control-tick jitter with BT active; TWDT
margin on both cores; pairing/reconnect reliability; `enableNewBluetoothConnections(false)`
lockout actually holding (there is a filed report of it not functioning in at least one
version — bluepad32 #130); real disconnect → outputs-safe latency (walk-away and
pad-power-off cases); DS4 idle report-rate assumption (§3.1); DS4 auto-sleep behavior;
**reconnect-without-input probe (review F1, 2026-08-17):** power the pad back on / walk
back into range and touch NOTHING — the car must stay in failsafe until genuine
post-connect reports flow (the wrapper seeds its freshness baseline at connect time, so a
bare stack reconnect claim must yield zero frames; this probe is the hardware-side proof,
since no native test can execute the custom-core wrapper path);
3.3 V rail draw with BT active; ELRS-RX-powered coexistence; genuine-vs-clone DS4 behavior
(clones are known-flaky — bluepad32 #127; spec genuine Sony).

---

## 6. Pairing & giftee UX

### 6.1 Pairing procedure (DS4 facts, cited)

- **First pair:** hold **SHARE + PS** until the lightbar flashes rapidly (pairing mode);
  Bluepad32 discovers and pairs automatically — no MAC configuration, no tools. Lightbar
  goes solid when connected. The bond persists across reboots; thereafter a single **PS**
  press reconnects.
- **Re-pair:** a DS4 stores one host. If the pad is later paired to a PS4/PC, the ESP32
  bond on the pad side is replaced — the giftee repeats the SHARE+PS ritual. One line in
  the glovebox booklet covers it.
- **Lockdown policy (proposal):** accept new pairings only while no pad is bonded, or
  during the first `btpad.pairWindowMs` (default 30 s, tunable) after a BT-mode boot;
  afterwards call `enableNewBluetoothConnections(false)` so a stranger's pad cannot join
  mid-demo. Single-controller policy: exactly one pad; additional connection attempts are
  refused. `forgetBluetoothKeys()` is exposed as a bench/console action only ("factory
  reset" path), not a giftee control. The lockout's effectiveness is a bench gate item
  (bluepad32 #130).

### 6.2 Giftee demo script (target UX)

1. Engine cover off → mode switch to **SOLO** → key in (XT90 loop key; ESC does its
   normal boot-arm neutral hold — 2 s, unchanged).
2. Press **PS**. Lightbar solid; car lights leave "awaiting controller".
3. Hold **L1+R1** one second → armed (lights: the planned ignition animation is the
   natural cue, board-2 territory).
4. Drive gently — triggers gas/brake, stick steers, Square toggles DRS, engine sound
   tracks the commanded throttle (link2 `throttlePercent` semantics unchanged,
   `docs/link2_protocol.md`).
5. Walk out of range / pad dies → car stops and latches safe within the same ~½ s bound as
   an ELRS loss; **OPTIONS** or key-out ends the session.

### 6.3 Surfacing BT state through link2 (proposal — protocol impact, doc-first)

Board #1 has no LEDs, so board #2's lights are the only "is it listening?" display.
Today's frame already conveys most of it for free: no pad ⇒ failsafe=1 ⇒ board #2's
existing hazard behavior. What it cannot express is "BT mode, pairing window open /
awaiting controller" as distinct from "link lost".

- **Proposal (as drafted 2026-08-16):** assign payload `flags` **bit 7** — then "reserved
  (sender writes 0, receivers must mask, never reject)" — as `awaitingController` (BT
  mode, no pad bonded-or-connected). Old board-#2 firmware masks it (documented receiver
  rule), so the change is backward-compatible and needs no version bump; new board-#2
  firmware may render a distinct light pattern (pattern choice is board-2/owner
  territory).
- **[Owner 2026-08-17, OWNER-DECIDED(BT-7) — RESOLVED onto link2 v2 `modeFlags` bit 1.]**
  Showcase mode (a separate accepted feature) and this proposal both wanted `flags`
  bit 7; the owner resolved the collision with a dedicated **`modeFlags` payload byte**
  in link2 protocol v2, already landed on `main` (`lib/link2/include/link2/Link2Frame.hpp`
  at `2d146dc`): **bit 0 = `showcase`** (belongs to showcase mode — BT mode must NEVER
  set it), **bit 1 = `awaitingController`** — the pairing-state surface this section
  designed, now an already-reserved wire bit that current firmware always transmits
  as 0. The doc-first adoption this section demanded is therefore DONE on the protocol
  side (protocol doc + codec + golden-frame tests + soundlight re-sync all landed with
  link2 v2); **emitting** bit 1 from the BT head (wiring "BT mode, pairing window open /
  awaiting controller" into `VehicleState::awaitingController`) is a deliberately
  separate future slice, not part of this prototype. `flags` bit 7 stays reserved.
- **Process cost, honored (original wording, satisfied by the link2-v2 landing):** this
  repo owns the protocol — the change lands in `docs/link2_protocol.md` + `lib/link2`
  first, then the soundlight copy re-syncs, and `tools/link2_copy_check.sh` + the
  golden-frame native tests are updated in the same commit.
- Optional output-only garnish (owner decision): DS4 lightbar mirrors car state (e.g.
  solid = connected-disarmed, distinct color = armed, blink = low battery), rumble blip on
  failsafe. Pure output to the pad; touches no safety path.

---

## 7. Test matrix

**Native (`pio test -e native`, new `test/test_btpad` + extensions, no hardware):**

| Area | Cases |
|---|---|
| Pad mapping | axis→steering anchors + deadzone edges + inversion; trigger→throttle scaling anchors (0, mid, full); R2−L2 net-throttle clamp; out-of-range defensive clamps |
| Arm ritual | hold-duration semantics with fake clock (999 ms ≠ armed, 1000 ms = armed); OPTIONS instant disarm; disconnect clears latch; failsafe clears latch (§3.2 strict variant); ritual + real `ArmGate`: arm-with-trigger-held stays motor-off until neutral seen |
| Staleness → failsafe | the pad-source adapter produces the exact (frameArrived, failsafeFlag) sequences; `FailsafeStateMachine` with those inputs: 500 ms timeout fires, Safe latches, 150 ms link-proof re-arm (continuous-good + frame count + gap bound), disconnect-flag path — asserting the **same outcomes as the existing `test_failsafe` suite** |
| Exclusivity logic | `BootModeResolver` pure function: pulled-up/floating ⇒ CRSF, grounded ⇒ BT, sample-majority debounce; (stack never-init is main.cpp wiring — covered by sim + ELF checks, below) |
| Envelope | driveMode pinned 0 in decoder output; shaping with `btpad` GearParams honors maxOutput/expo endpoints; boost cannot engage (driveMode ≠ 2) |
| Settings | `btpad` sub-config `valid()` bounds; blob v2 round-trip; v1 blob rejected ⇒ complete defaults (guard-chain behavior) |
| link2 | bit-7 encode; golden-frame variant with `awaitingController` set; receivers-mask rule untouched for bits 1/7 |

**Sim stage (LANDED, review F2 2026-08-17):** Wokwi has no Bluetooth, so the sim's job
here is wiring, not radio. `[env:esp32dev_simbt]` builds the SAME `W17_BT_SHOWOFF` main
wiring — booted straight into the BT head (no strap hardware in the sim) — driven by the
scripted `src/SimPadFeeder.cpp` session (`W17_SIM_PAD_FEEDER`, the `SimCrsfFeeder`
pattern) in place of Bluepad32, **on the pinned stock core with `btpad_hal_esp32` still
`lib_ignore`d**: the env building at all is the proof that the BT head carries no
BT-stack dependency. The 13 s looping session, narrated on Serial0 (`[simpad]` phase
lines + a 500 ms `[state]` readout): awaiting-pad → connect + neutral stream (link
recovers, disarmed) → L1+R1 ritual arms → drive shapes (R2 ramp capped by the btpad
envelope, steering sweep, Square DRS taps) → dropout (failsafe within ~540 ms, ritual
cleared) → reconnect CLAIM only (zero reports — stays Safe; the wiring-level twin of the
BT1 reconnect-without-input probe) → neutral stream without ritual (Active again, STAYS
DISARMED) → fresh ritual re-arms → OPTIONS instant disarm. The same beat sequence is
pinned natively in `test/test_btpad`'s
`test_full_session_mirrors_sim_pad_feeder_beats`, which drives the identical pure-module
chain in loopBtPad's per-pass order. Plus the ELF checks: delivery ELF has no btpad
symbols; BT-env ELF has no live CRSF-init path in BT mode (spot-check).

**Bench-only gates:** the list at the end of §5, executed as this mode's own gate (§9),
car on stand, wheels off the ground, motor-power rules per the existing gate regime.

---

## 8. Decisions — RATIFIED by the owner 2026-08-17

All eleven were decided by the owner on **2026-08-17**: every row's **Recommendation**
was accepted as-is, with two clarifications — **BT-7** resolved onto the link2 v2
`modeFlags` byte (bit 1, see §6.3), and **BT-9** superseded by a scope simplification
(BT mode is car-control only; the camera system is OFF). Every implementation point is
tagged `OWNER-DECIDED(BT-n)` in the code/docs (grep for the tag to find every
consequence of a decision). Bench execution of anything here remains gated on BT1 (§9).

| # (tag) | Decision | Options considered | Outcome (owner 2026-08-17) |
|---|---|---|---|
| 1 `OWNER-DECIDED(BT-1)` | Boot-mode selector | A strap switch / B console NVS / C double-key-cycle NVS toggle | **A accepted** as recommended; C stays recorded as a v2 candidate (needs its own pass — relaxes delivery NVS read-only invariant); B rejected as selector |
| 2 `OWNER-DECIDED(BT-2)` | Strap pin | GPIO27 / 32 / 33 | **GPIO27 accepted** as recommended; atlas reconciliation and the A2/F20 continuity-matrix note remain wiring-time tasks (decided pin, still bench-verified) |
| 3 `OWNER-DECIDED(BT-3)` | Arm ritual + failsafe interaction | (a) L1+R1 1 s hold + OPTIONS disarm, ritual cleared by failsafe (stricter than CRSF); (b) exact CRSF mirror; (c) dead-man variant | **(a) accepted** as recommended — deliberate re-arm suits an untrained operator; implementation additionally requires release-before-rearm after every latch-clear (strictly more conservative, never weaker) |
| 4 `OWNER-DECIDED(BT-4)` | Demo envelope | v0: driveMode pinned TRAINING with `btpad.maxOutput`=400 / expo 50 tunables; v1 option: paddles drive the gearbox for the sound show | **v0 accepted** as recommended; paddles-for-sound stays a possible follow-up once the mode is bench-proven |
| 5 `OWNER-DECIDED(BT-5)` | DRS on the pad | include (Square toggle) / omit | **Include accepted** as recommended — pure showpiece value, cosmetic channel, no safety surface |
| 6 `OWNER-DECIDED(BT-6)` | Pad feedback outputs | lightbar state colors / + rumble cues / none | **Lightbar only accepted** as recommended for the prototype |
| 7 `OWNER-DECIDED(BT-7)` | link2 pairing-state surface + board-2 light pattern | adopt flags bit 7 (drafted) / defer | **Adopted, RESOLVED onto link2 v2 `modeFlags` bit 1 `awaitingController`** (§6.3) — the dedicated byte the owner added when showcase mode and this proposal both wanted flags bit 7. The reserved bit is on the wire today (always 0); EMITTING it from the BT head is a future slice. BT mode must never set `modeFlags` bit 0 (`showcase` — that bit belongs to showcase mode, built on a parallel branch) |
| 8 `OWNER-DECIDED(BT-8)` | BT stack integration | pin Bluepad32 3.10.x (IDF 4.4-era core) / custom Bluedroid lib-build + esp_hidh (fallback) / migrate repo to core 3.x (rejected) | **3.10.x pin accepted**, concretely **3.10.2** — the last 3.10.x custom-core artifact that exists (3.10.3 was source-only) — and PROVEN by the branch build: `esp32dev_btshowoff` links the real BTstack on this pin (verification commit evidence). Fallback stays documented |
| 9 `OWNER-DECIDED(BT-9)` | Gimbal in BT mode | fixed center (boundary-4 wording compliant) / pad right stick (would need a boundary-4 amendment) | **Superseded by scope simplification: BT mode is car-control only — the camera system is OFF** ("a simple mode for quick showing off"). Gimbal-fixed-center stays as the implementation; the rationale is now camera-off scope, not just boundary-4 wording. Boundary text untouched |
| 10 `OWNER-DECIDED(BT-10)` | Pairing window policy | 30 s-after-boot window, locked after / always-open until first bond | **First option accepted** as recommended (tunable window `btpad.pairwin`); lockout effectiveness stays a BT1 bench item |
| 11 `OWNER-DECIDED(BT-11)` | BTstack license notice at publication | add notice when repos go public / n/a | **Add accepted** as recommended — free for open source; note it in the repo license file at publication time |

---

## 9. Relationship to gates

- **A2 / Phase B: untouched.** A2 remains NOT-EXECUTED and Phase B BLOCKED
  (`../CURRENT_STATUS.md`); nothing in this design changes their scope, order, or wording.
  The only contact point is additive: if the strap switch is adopted, its pin joins the
  continuity-matrix scope discussion already open as F20.
- **This mode gets its own bench gate** — proposed name **BT1** — consisting of the
  bench-only list in §5/§7. BT1 can begin only when powered work is legal at all (A2
  closed, Phase B rules in force) and runs car-on-stand first; ESC motor-power rules are
  the existing ones, unchanged. **No BT code runs on powered hardware before BT1 is
  opened by the owner.**
- **Branch discipline:** design doc first (this document, owner-reviewed), then the
  prototype on a feature branch with `W17_BT_SHOWOFF` default-OFF in every existing env,
  **native tests only** — no flash, no bench, nothing merged and nothing pushed before the
  owner reads and approves. This mirrors the branch-only precedent the owner set for the
  U4 arbiter (`../W17_PRODUCT_VISION.md`, head-tracking reality check).
- **Authority note, recorded:** the mode dilutes "Windows is the control/integration
  authority" (boundary 7) the same way the approved ELRS backup handset does — accepted by
  the owner at vision lock (decision 12), gated per process. Boundaries 1–6 are untouched
  by construction (§1, §3).

## What this design does NOT change

Failsafe timing and semantics; ArmGate logic; ESC boot-arm; battery warn-only; the CRSF
path in CRSF mode (bit-for-bit when the flag is off); link2 framing (one masked reserved
bit gains meaning, doc-first); the gimbal boundary; the iPhone boundaries; A2/Phase B;
the delivery build's console-free, NVS-read-only posture.

---

## Sources

Firmware (all at `w17-control-fw` @ `3f4f9b7`): `CLAUDE.md`, `platformio.ini`,
`src/main.cpp`, `lib/config/include/config/PinMap.hpp`,
`lib/failsafe/include/failsafe/FailsafeStateMachine.hpp`,
`lib/channels/include/channels/ArmGate.hpp`,
`lib/channels/include/channels/ChannelDecoder.hpp`,
`lib/gearbox/include/gearbox/Gearbox.hpp`, `lib/outputs/include/outputs/EscOutput.hpp`,
`lib/outputs/include/outputs/ServoOutput.hpp`, `lib/settings/include/settings/Settings.hpp`,
`lib/console/include/console/Console.hpp`, `lib/crsf/include/crsf/CrsfReceiver.hpp`,
`docs/link2_protocol.md`. Workspace: `CLAUDE.md` (boundaries), `W17_PRODUCT_VISION.md`,
`CURRENT_STATUS.md`.

External:

- Bluepad32 README (supported controllers incl. DualShock 4; Apache-2.0; BTstack
  free-for-open-source / commercial-for-closed-source; ESP32 supported):
  <https://github.com/ricardoquesada/bluepad32>
- Bluepad32 release notes (v4.2.0 2025-01-03 requires ESP-IDF 5.3/5.4, Arduino Core 3.1;
  v3.10.3 2023-11-26 last 3.x line, IDF 4.4-era):
  <https://bluepad32.readthedocs.io/en/stable/release_notes/>
- Bluepad32 Arduino integration = custom board package / precompiled core:
  <https://bluepad32.readthedocs.io/en/stable/plat_arduino/>; current template (IDF 5.4.2,
  BTstack component in place of Bluedroid, PlatformIO supported):
  <https://github.com/ricardoquesada/esp-idf-arduino-bluepad32-template>
- DS4 pairing with Bluepad32 (SHARE+PS rapid-flash ritual, auto-discovery, bond persists,
  solid lightbar): <https://racheldebarros.com/esp32-projects/connect-your-game-controller-to-an-esp32/>
- Classic BT (Bluedroid) RAM ≈ 70 KB + 70 KB and `esp_bt_controller_mem_release`:
  <https://esp32.com/viewtopic.php?t=3139>,
  <https://github.com/espressif/arduino-esp32/issues/6451>
- BluetoothSerial sketch = 849,303 B / 64% of the 1,310,720 B default app partition;
  huge_app remedy: <https://github.com/espressif/arduino-esp32/issues/1712>,
  <https://arduinoyard.com/esp32-and-bluetoothserial-issue/>
- Stock precompiled arduino-esp32 core ships `CONFIG_BT_HID_HOST_ENABLED` disabled:
  <https://github.com/espressif/arduino-esp32/issues/5582>,
  <https://github.com/espressif/esp32-arduino-lib-builder/issues/40>
- `enableNewBluetoothConnections(false)` reported non-functional in at least one version
  (⇒ bench-verify the lockout): <https://github.com/ricardoquesada/bluepad32/issues/130>
- DS4 clone flakiness: <https://github.com/ricardoquesada/bluepad32/issues/127>
