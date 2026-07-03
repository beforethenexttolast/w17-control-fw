# W17 Control Firmware — Review Verdict & Remaining-Work Roadmap

Status date: 2026-07-02 · Gift deadline: **2026-07-21 (19 days)**
Deliverable #1 (skeleton, crsf, failsafe, outputs, 27 native tests) is DONE and was
adversarially reviewed. This file records the confirmed defects and the plan for everything left.

---

## A. Review verdict on deliverable #1

### CONFIRMED — must fix before any bench flash with servos/ESC connected

**A1. [CRITICAL] Failsafe goes Active at boot with zero CRSF frames received —
steering slams to full lock for ~350 ms on every power-up.**
`lib/failsafe/src/FailsafeStateMachine.cpp:9` + `src/main.cpp:25`
The FSM has no "a frame has ever arrived" concept: link-valid is purely
`nowMs - lastFrameMs < 500`, and `lastFrameMs` boots as 0, so for the first 500 ms the link
*reads* valid with no frames. Trace: rearm window opens on the first loop tick → Active at
~t=155 ms → main.cpp reads the **zero-initialized** channel array → `rawChannelToNormalized(0)`
= −1211 → clamped −1000 → steering commanded to 500 µs (hard full-left, stalling the 35 kg·cm
DS3235SG against the printed rack) until t=500 ms. The ESC gets full-brake/reverse in the same
window and is saved *only* because `bootArmHoldMs` (2000) happens to exceed the window — an
unrelated tunable, not a designed guard. The header's own comment "never claim Active before a
frame has ever arrived" is an invariant the code does not implement, and
`test_climbs_to_active_after_full_rearm_window_from_boot` currently codifies the buggy behavior.
**Fix:** make frame-arrival an explicit FSM input (`everReceivedFrame_` latch set only on real
FrameReady events); regression tests: no frame ever fed → Safe forever, at any `nowMs`.

**A2. [MAJOR] No arm gate on the live throttle pass-through.**
`src/main.cpp:92` — CLAUDE.md §6.2 (non-negotiable #2) requires throttle neutral until arm
switch ON **and** throttle observed at neutral once. The placeholder drives the ESC from raw
ch3 with neither check: TX throttle held high at power-on goes straight to the ESC when the 2 s
boot hold expires. The arm *switch* legitimately waits for the channels module, but the
neutral-latch half needs nothing from it.
**Fix now:** minimal neutral-latch in main.cpp (throttle forced 0 until the decoded channel has
been seen inside a deadband around 992 once). Full arm-switch gate lands in the channels module.

### Triaged from the unverified pool (panel was cut off; manually assessed)

| # | Finding | Verdict | Action |
|---|---|---|---|
| A3 | Failsafe re-arm after link recovery snaps throttle to current stick position mid-drive | Real design gap | Channels-module arm gate must require throttle-neutral again after any failsafe (D2) |
| A4 | `Esp32LedcPwm::begin()` leaves duty 0; safe idle depends on caller ordering | Real, cheap | Write neutral/center pulse inside `begin()` (D1.5) |
| A5 | ESC boot-arm hold timed from static init, not from when pulses actually start | Real but small (static init → setup gap is ms) | Re-anchor hold to first `setThrottle()`/`begin()` (D1.5) |
| A6 | `main.cpp` reads `lastFrame()` every tick, violating its documented "valid only immediately after FrameReady" contract | Real contract violation, currently harmless | Copy channels out on FrameReady (D1.5) |
| A7 | Interleaved telemetry frames (LinkStatistics 0x14) reported as FrameInvalid; RX failsafe flag/LQ never parsed | Real limitation, by design for D1 | Parse LinkStatistics in D4; feeds real `rxFailsafeFlag` |
| A8 | RP1 configured with "Set Position" failsafe would defeat the timeout (RX keeps sending frames) | Real operational hazard | Bench checklist: RP1 failsafe mode MUST be **No Pulses** (D8) |
| A9 | Assembler discards buffered bytes on CRC fail (a 0xC8 inside a corrupt frame isn't rescanned) | True; costs ≤1 extra frame per corruption at 50–250 Hz frame rates | Accept; note as known limitation |
| A10 | `millis()` wraparound at 49.7 days | True; irrelevant for RC session lengths | Accept; comment only |
| A11 | Trim large enough to push center past an endpoint inverts direction silently | True; needs absurd config values | Config validation later, low priority |
| A12 | Loop free-runs instead of a fixed ≥50 Hz cadence | Spec-visible; harmless now | Fixed-tick scheduler when link2 lands (D6) |
| A13 | crsf module doesn't expose `linkUp`/`lastFrameMicros` per CLAUDE.md §2.1 | Deviation, undocumented | `CrsfReceiver` facade in D4 |

Review coverage note: the build-config reviewer and part of the test-gaps reviewer did not
complete (session limits), so platformio.ini/library.json semantics and test-gap analysis have
had one pass, not an adversarial one.

---

## B. Roadmap — everything remaining, in build order

### D1.5 — Safety fixes — ✅ DONE 2026-07-02
Scope: findings A1, A2(minimal), A4, A5, A6. All landed:
- `FailsafeStateMachine`: `everReceivedFrame_` latch; `update(nowMs, frameArrivedThisTick,
  rxFailsafeFlag)` — frame arrival is now an explicit event, timestamps alone can never make
  the link look healthy. Boot-climb test fixed (it had codified the bug); new regression:
  no frame ever → Safe at every timestamp.
- `main.cpp`: throttle neutral-latch (reset on every failsafe episode — also closes A3 at
  this level until the channels module lands); channels copied out on FrameReady (A6).
- `Esp32LedcPwm::begin(initialPulseMicros)`: commands the safe position immediately on attach.
- `EscOutput`: arm-hold anchored to the first `setThrottle()` call; new regression test.
Verified: 29/29 native tests pass, esp32dev builds clean.

### D2 — `channels` module + full arm gate (safety §6.2) — ✅ DONE 2026-07-02
- `lib/channels/ChannelDecoder`: config-table raw→named mapping (defaults: steering ch1,
  throttle ch3, arm ch5, DRS ch6, gearUp ch7, gearDown ch8 — placeholders, verify at bench),
  piecewise-exact ±1000 normalization, invert flags, switch hysteresis (+250/−250),
  first-decode level seeding (no phantom edges at boot), OFF→ON edge detection.
- `lib/channels/ArmGate`: armed ⇔ switch ON ∧ throttle-seen-neutral since last disarm;
  instant disarm on switch-off or failsafe; recovery requires fresh neutral (closes A3).
- main.cpp: decode on every frame (phantom-edge-proof), steering live while disarmed,
  temporary neutral-latch and rawChannelToNormalized placeholders removed.
- Verified: 45/45 native tests (16 new), esp32dev clean, lib/channels Arduino-free.

### D3 — `gearbox` — ✅ DONE 2026-07-02
- `lib/gearbox`: pure `shapeThrottle(throttle, gear)` — expo blend (endpoint-exact integer
  math) then scale-by-maxOutput (not clip: full stick travel maps to each gear's range);
  brake/reverse passes through unshaped. `Gearbox` state: shiftUp/shiftDown (saturating),
  `setGear()` for a future 3-pos selector, gear survives failsafe/disarm (ArmGate's fresh-
  neutral interlock covers the re-arm surprise). Default table: 400/50, 600/35, 800/20, 1000/0.
- main.cpp: shift edges consumed inside the frameArrived block (a bare loop-body check would
  re-fire one press every tick — caught in design review); post-gearbox throttle kept in a
  named local for link2 (D6).
- Verified: 59/59 native tests (14 new), esp32dev clean, lib/gearbox Arduino-free.

### D4 — CRSF LinkStatistics + receiver facade — ✅ DONE 2026-07-02
- `CrsfLinkStatistics` + pure `decodeLinkStatistics` (0x14, 10-byte payload, layout confirmed
  vs Betaflight/TBS). `CrsfFrameAssembler` generalized to framing+CRC only: FrameReady = any
  CRC-valid type (closes A7 — telemetry no longer misreported as corruption); per-type payload
  length validation moved to the facade.
- `CrsfReceiver` facade (CLAUDE.md §2.1, closes A13): owned `channels()` copy, `linkStats()`,
  `lastRcFrameMs()`, `linkUp()` (reporting only — the FSM stays the actuation authority), and
  `rxSignalsFailsafe()`: **latched** uplink-LQ==0, cleared ONLY by a LQ>0 stats frame — never
  by RC frames or staleness, so hold-position RC frames during an outage can't re-arm the car
  (mitigates A8). Real `rxFailsafeFlag` now feeds the failsafe FSM in main.cpp.
- Verified: 72/72 native tests (13 new), esp32dev clean.
- **Bench-verify (added to D8):** ELRS LQ=0 forced burst on disconnect (count/cadence),
  ~100ms connected stats cadence, RX disconnect-declaration latency at the chosen packet
  rate, whether serial-CRSF ELRS even has a Set-Position mode, no-RC-output-before-first-bind.

### D5 — `telemetry` — ✅ DONE 2026-07-02
- Seams: `IVoltageSensor` (calibrated pin-mV — deliberate upgrade over the planned raw-counts
  `IAdc`: esp_adc_cal/analogReadMilliVolts keeps chip nonlinearity below the seam, the board's
  divider above it) and `IWheelPulseSensor` (count + ISR-timestamped pulse period — count-only
  would quantize speed to the tick rate and flap 2:1 at top speed under D6's fixed 50Hz tick).
- `BatteryMonitor`: rounded combined divider+trim conversion, stall-free scaled-accumulator
  EMA seeded from the first sample (kills a boot false-warning), low-voltage warn latched only
  after 3s sustained below 7.0V, cleared above 7.4V — sag-proof, monitoring only per §6.4.
- `WheelSpeed`: rpm from measured pulse period, EMI plausibility clamp (5000rpm), graceful
  silence decay (report capped by 60000/elapsedMs) with a 1.5s hard-zero tail.
- esp32 impls: burst-averaged `analogReadMilliVolts` (11dB), Hall ISR with IRAM_ATTR
  trampoline via `attachInterruptArg`, `std::atomic` counters, 2ms edge lockout (9× margin
  at ~55Hz max pulse rate).
- Verified: 88/88 native tests (16 new), esp32dev clean, lib/telemetry + lib/hal Arduino-free.
- **Bench-verify (added to D8):** two-point ADC check (~6.5V and 8.4V) vs multimeter + log
  which eFuse cal type characterization reports; scope the Hall line at full throttle near
  the motor (add 1-10nF across the sensor output if ugly); add 100nF GPIO34→GND (divider
  source impedance ~7.3kΩ makes single reads noisy).

### D6 — `link2` UART frame to ESP32 #2 — ✅ DONE 2026-07-02
- Wire format v1 (12 bytes, `docs/link2_protocol.md` with worked hex example pinned by a
  golden-frame test): start 0xA5, length, payload (version, throttle% as-commanded,
  steering% for indicators — replaced the redundant speed field, flags incl. hysteresis-
  filtered braking, 1-based gear, wheel rpm, battery mV), CRC8 0xD5 over length+payload.
  CRC deliberately duplicated (lib liftable wholesale into board #2; test cross-checks vs
  crsf). Validation order start→length→CRC→version pinned; assembler hard-rejects bad
  length bytes immediately. Doc mandates receiver 500ms staleness → local failsafe.
- `Link2Sender` over new `hal::IByteSink`; esp32 UART1 TX-only (GPIO25, RX deliberately
  unopened; remap mandatory — UART1 defaults are flash pins).
- main.cpp restructured to fixed cadences (closes A12): always-drain UART, event-driven
  decode/shifts, 50Hz control tick (failsafe/armgate/outputs, `rcFrameSinceTick`
  accumulator), 20Hz link2 — **no Safe-branch early return: link2 keeps transmitting
  during failsafe** (caught in design review), boot-safe initial snapshot.
- Verified: 101/101 native tests (13 new), esp32dev clean, lib/link2 Arduino-free.

### D7 — Wokwi simulation (Stage 2) — ✅ DONE 2026-07-02
- `wokwi.toml` + `diagram.json`: devkit-v1, servos on D13/D14/D18, pot on D34 (battery,
  preset ≈8.4V), button+10k pull-up on D35 (Hall; one click = one pulse on release),
  UART2 TX→RX loopback wire for CRSF self-feeding.
- `[env:esp32dev_sim]` = esp32dev + `-DW17_SIM_CRSF_FEEDER`; `src/SimCrsfFeeder.cpp` scripts
  a ~25s loop showing all three failsafe behaviors DISTINCTLY (boot never-received, pure
  timeout, LQ=0-while-RC-flowing hold-position instant drop — the A8 demo), arm-gate block,
  fresh-neutral recovery, gear shifts, DRS; stats-before-RC recovery ordering and gearDown
  cooldown for loop idempotency (both design-review catches). Serial monitor narrates phases
  + 2Hz state line.
- `crsf/CrsfFrameBuilder.hpp`: frame construction lifted out of the tests, shared with the
  feeder (zero duplication, still pure).
- `docs/SIMULATION.md`: run instructions, phase table, cosmetic quirks, first-run VERIFY
  checklist for low-confidence Wokwi platform facts (pin label names, 420k baud, pot attr,
  merged-bin fallback, bounce timing).
- Verified: 101/101 native tests, esp32dev AND esp32dev_sim build clean. First interactive
  Wokwi run = user's checklist (needs the free Wokwi license).

### D8 — Hardware bring-up (Stage 3) — bench days, gated on parts
**Consolidated into an ordered runbook: `docs/D8_BENCH_BRINGUP.md`** (11 phases, safety-gated,
with pass/fail and the tuning-console commands). All the accumulated bench-verify items from the
Phase-1/2 design reviews (RP1 failsafe = No Pulses, channel-map + switch thresholds, servo-center
-before-linkage, ESC forward/brake, wheels-off until failsafe+arm proven, ADC two-point cal,
Hall EMI scope, link2 staleness, camera H.265→WebRTC, com0com telemetry) live there.

## B2. Phase 2 — approved extensions (order agreed 2026-07-02)

1. **CI** — GitHub Actions: native tests + both esp32 builds on every push. ✅ DONE 2026-07-02
2. **ERS mode + drive-mode selector + link2 amendment** — ✅ DONE 2026-07-02
   `lib/ers`: micro-permille energy store (drain/harvest exact at any tick rate), boost +18%
   / overtake +25% post-gearbox multipliers (HUD-matched rates: deploy 26%/s, brake harvest
   11%/s), deploy needs positive commanded throttle, ALL harvest rpm-gated (parked car never
   creeps), freeze-with-clock-reseed outside ERS mode (no dt gaps; stale boost in failsafe
   inert), applyBoost(0)==0 invariant test-pinned (can't bypass the arm gate).
   Modes on ch13 3-pos: 0=Training (fixed 400-cap+expo, shifts inert), 1=Gearbox
   (mid/default), 2=Gearbox+ERS. **Design change from the original wording: no raw
   "Direct" mode** — gearbox top gear already IS full power (scale-not-clip), so Direct
   only removed the shifts between a bumped switch and full throttle; Training replaces it.
   Boost ch11 / overtake ch12 (held switches); all three absent-tolerant.
   link2 v1 amended (board #2 unwritten, no version bump): payload 11 bytes (+ersPercent,
   +driveMode), flags bit6=ersDeploying, frame 14 bytes; golden test + protocol doc updated.
   Wokwi demo: ERS showcase at t=13-14.5s. 119/119 native tests (18 new).
3. **Board #2 sound/light firmware** — ✅ DONE 2026-07-03 in the sibling repo
   `w17-soundlight-fw` (committed locally; push to GitHub pending user action). Hybrid sound:
   testable procedural V10-flavored synthesis now (harmonic partial stack shifted up so the
   fundamental sits in a small speaker's band, ERS whine, rev limiter, overrun crackle,
   starter sequence) behind an `ISampleSource` seam for a future PCM player. Pure libs:
   link2 (copied verbatim), link2monitor (500ms staleness + per-field failsafe + LinkStatus),
   enginesim, soundsynth, lights (F1 rain light flashes on ERS *harvest*, hazard override,
   power budget). Dual-core: control on core 1, audio pump on core 0, one atomic param word +
   heartbeat dead-man. 40 native tests, esp32dev + esp32dev_sim build clean, own CI.
4. **Ground station (w17-ground-station repo)** — ✅ DONE 2026-07-03 (committed locally; push
   to GitHub pending user). Viewer-only Electron app: WebRTC/WHEP video (bundled mediamtx)
   under an F1 HUD that mirrors the DualShock and overlays car telemetry. Does NOT command the
   car — elrs-joystick-control keeps the control link (gift-day safety); zero-code fallback is
   elrs-joystick-control + VLC. Pure core unit-tested (14 vitest): CRSF decoder ported from the
   firmware (golden-vector tested), transport-agnostic TelemetrySource + replay/demo source,
   shared ERS feel-constants (numbers only — the models differ, so no logic port). Two design-
   review course-corrections: **item-5's CRSF uplink lands on the same FT232 port
   elrs-joystick-control holds** (so telemetry should return over WiFi, not that serial), and
   the H.265→WebRTC codec check is the #1 bench risk (docs/SETUP.md). Live video + the .exe are
   bench steps on the Windows machine.
5. **Telemetry uplink to the HUD (battery + LQ over ELRS backchannel)** — ✅ DONE 2026-07-03
   Scoped with the user to **battery voltage + link quality** over the existing two-way ELRS
   link (WiFi/ESP-NOW paths declined: ESP32-WROOM is 2.4GHz-only, the video AP is 5.8GHz, and
   this needs no new radio; ELRS relays standard sensor frames + MSP, not arbitrary data, so
   speed/gear/ERS stay HUD-simulated). Control board emits a standard CRSF **battery frame
   (0x08)** out GPIO17 → RP1 → ELRS downlink → ground TX → FT232 (new `CrsfFrameBuilder::
   buildBatteryFrame` + `Esp32CrsfUart::write`; ~5Hz, outside the control tick; voltage real,
   percent a coarse 2S estimate). **LQ needs no firmware** — the ground TX module reports
   LINK_STATISTICS to the host natively. Ground station: `CrsfSerialSource` +
   `shared/crsfTelemetry.js` map battery+LQ into a partial Telemetry the existing HUD overlays.
   No link2 change, no board #2, no WiFi. Verified: 143 control native tests (+2), esp32dev +
   esp32dev_tuning clean; ground 18 vitest (+4). **Bench-gated:** the FT232 COM port is held
   exclusively by elrs-joystick-control — read telemetry via its forward flag (verify) or a
   com0com splitter (docs/TELEMETRY.md + SETUP.md in the ground repo).
6. **Serial tuning console + NVS persistence** — ✅ DONE 2026-07-03
   `lib/settings` (pure): `Settings` aggregates the tunable subset (steering center/trim,
   battery calibrationPpt, gear feel table), `constexpr kDefaults` + composed
   `static_assert(kDefaults.valid())` (keeps the "bad config can't compile" net), versioned
   CRC blob (de)serialize with the never-brick guard chain (length→CRC→version→valid()→apply;
   any failure ⇒ defaults). `lib/console` (pure): dotted-key handler (`get/set/save/load/
   reset/status/help`), mutations gated on DISARMED, every `set` runs the sub-config's valid()
   (incl. gear monotonicity + the A11 trim-past-endpoint rule now in `ServoConfig::valid()`);
   `ConsoleRunner` glues the char-IO + store seams (`hal::ICharIO`, `hal::ISettingsStore`).
   `set` is RAM-only, only `save` writes NVS. Modules gained `setConfig()` (pure config-copy;
   ESC arm anchor + gearbox current-gear preserved). esp32 impls: `Esp32NvsStore` (Preferences)
   + `Esp32SerialConsole` (UART0). **Behind `-DW17_TUNING_CONSOLE` (`[env:esp32dev_tuning]`) —
   the delivered gift firmware builds plain `esp32dev` with no UART0/console surface.**
   Verified: 141 native tests (22 new), esp32dev + esp32dev_tuning build clean, libs pure.
   D8 checklist: tune `steer.trim`/`batt.ppt`/gears over the console, then `save`.

## B3. Phase 3 — post-Phase-2 extras

7. **Camera gimbal pan/tilt** — ✅ DONE 2026-07-03. Two MG90S servos (GPIO19 pan / GPIO23 tilt,
   LEDC ch3/ch4) driven each control tick from the already-decoded `controls.pan`/`.tilt`
   (ch9/ch10), with `invertPan`/`invertTilt` bench flags. Not safety-gated (aiming a camera is
   harmless armed or disarmed); holds last position on failsafe (`controls` frozen).
   **Input:** map the right DualShock stick X/Y → ch9/ch10 in elrs-joystick-control (the right
   stick is otherwise unused). 144 native tests (+1), both esp32 builds clean.
8. **Real speed + gear + drive-mode + ERS to the HUD (standard CRSF frames, no MSP)** —
   ✅ DONE 2026-07-03. Extends item 5's ELRS backchannel to carry the remaining car-side truths
   over *standard, ELRS-relayed* frames instead of MSP: real wheel speed as a **GPS frame (0x02)
   groundspeed** field (`buildGpsFrame`; km/h·10 = mm/s·36/1000 from `WheelSpeed`), and
   car-authoritative **gear / drive-mode / ERS%** as a **FLIGHTMODE frame (0x21)** status string
   `"G3 M2 E55"` (`buildFlightModeFrame`; read from `controlSnapshot`). Both emitted in the same
   ~5 Hz telemetry cadence as the battery frame. **Why send gear/ERS rather than let the HUD
   mirror them:** the HUD's independent gamepad-driven gear/ERS drifts from the car's (a dropped
   shift edge desyncs; the display model even used a different gear count) — sending the actual
   values shows ground truth; speed isn't inferable on the ground at all. Ground station:
   `decodeGps`/`decodeFlightMode`/`parseFlightMode` in `shared/crsf.js`, mapper extended, and
   `CrsfSerialSource` now *merges* frame types into one running snapshot (a battery frame must
   not blank speed/gear). HUD needed no change — it already prefers telemetry over its mirror
   when live. Verified: 147 control native tests (+3 frame-builder golden vectors), esp32dev +
   esp32dev_tuning clean; ground 20 vitest (+2). Same bench gate as item 5 (FT232 COM access).

### Still deferable / optional
Code-signing the ground-station .exe (plumbing + docs ready — `electron-builder.yml` +
docs/CODESIGNING.md; actual signing opt-in, removes the one-time SmartScreen prompt), BX100
low-voltage buzzer polish, gearbox curve tuning (ship conservative defaults).

### Calendar sketch (19 days)
- Jul 02–03: D1.5 + start D2 · Jul 04–06: D2 + D3 · Jul 07–08: D4 + D7 (Wokwi early — parts
  may still be shipping) · Jul 09–11: D5 + D6 · Jul 12–20: Stage-3 bench + car integration,
  paint/assembly margin (firmware must NOT be the long pole; everything above except D8 needs
  zero hardware).

### Risks
1. **Parts arrival gates D8 only** — everything else is native/Wokwi; keep it that way.
2. **ESC behavior assumptions** (arming, brake-vs-reverse mode) unverified until bench —
   budget one bench session purely for ESC characterization.
3. **RP1 failsafe misconfiguration silently defeats the firmware timeout** (A8) — checklist item.
4. Single-person schedule: D5/D6 are parallelizable in scope but serial in practice; the
   deferable list is the pressure valve.
