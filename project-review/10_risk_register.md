# 10 — Risk Register (W17 RC project)

**Scope:** independent skeptical audit of `w17-control-fw`, `w17-soundlight-fw`,
`w17-ground-station`, pre-hardware. **Method:** 10 dimension investigations → cross-dimension
dedupe → adversarial re-check of every High/Medium finding by re-reading the cited code/docs
(see `_verification_results.md`). Low findings are carried from the dimension files and marked
as not-individually-re-verified.

**Baseline (confirmed):** control-fw 147/147 native tests + all 3 ESP32 envs build;
soundlight-fw 40/40 + builds; ground-station 21/21 vitest. Flash/RAM ~22%/~7%.

**Headline:** **no Critical risks survived verification** — the failsafe / arm-gate / ESC-boot-arm
architecture verified as genuinely sound, so nothing here damages hardware or causes uncontrolled
motion on its own. **4 High** risks remain: two are pure-software fixes doable now (R02, R03), one
is a software/expectations gap (R01), one is hardware-gated (R04).

**Legend** — Verdict: CONFIRMED / ADJUSTED (real, corrected) / REFUTED (didn't hold) / PLAUSIBLE
(legit, only provable on hardware). **HW?** = requires physical hardware to fully verify/close.
**Fix when:** `now` (pre-hardware) or `wait` (needs bench/parts).

## Summary table

| ID | Sev | Conf | Verdict | HW? | Fix | Title |
|---|---|---|---|---|---|---|
| R01 | High | High | CONFIRMED | no | now | HUD reads `armed`/`failsafe` the car never sends → "LINK LOST" is demo-only |
| R02 | High | High | CONFIRMED | no | now | control-fw platform UNPINNED + LEDC channel API removed in core 3.x (latent build break) |
| R03 | High | High | CONFIRMED | no | now | Packaged `.exe` never rebuilds serialport for Electron ABI → ships telemetry-disabled |
| R04 | High | Med | CONFIRMED/PLAUSIBLE | yes | wait | ESC signal pin (GPIO14) floats from reset until `escPwm.begin()`; no pull-down |
| R05 | Med | High | CONFIRMED | no | now | Gear count inconsistent: link2 doc 6 / firmware 4 / HUD 8 |
| R06 | Med | High | CONFIRMED | no | now | `lib/link2` duplicated across repos, no CI drift guard |
| R07 | Med | Med | ADJUSTED | no | now | CRSF C++↔JS decoders share no fixture; comment overstates coupling |
| R08 | Med | Med | ADJUSTED | yes | wait | Pin GPIO numbers unverified against physical build (atlas is illustrative-only) |
| R09 | Med | Med | CONFIRMED/PLAUSIBLE | yes | wait | ESC 2000 ms arm-hold + forward/brake mode unverified vs QuicRun 10BL120 |
| R10 | Med | High | CONFIRMED | yes | wait | Steering endpoints = full servo range; no mechanical travel limit → bind/stall |
| R11 | Med | High | ADJUSTED | yes | wait | HAL layer untested on hardware; soundlight has no Wokwi sim |
| R12 | Med | Med | CONFIRMED/PLAUSIBLE | yes | wait | Brownout/reset transient relies on ESC signal-loss behavior; no brownout config |
| R13 | Med | Med | CONFIRMED/PLAUSIBLE | yes | wait | Uplink telemetry uses sync `0xC8`; ELRS relaying it (and address correctness) unverified |
| R14 | Med | High | CONFIRMED | yes | wait | Telemetry return path blocked on the exclusive FT232 COM port |
| R15 | Med | High | CONFIRMED | yes | wait | Video path (mediamtx WHEP; H.265→H.264 gate) unproven |
| R16 | Med | High | CONFIRMED | partial | now | `main.cpp` orchestration + Wokwi sim not asserted in CI; named coverage gaps |
| R17 | Med | High | CONFIRMED | no | now | CI gaps: `esp32dev_tuning` never built; ground CI never packages the app |
| R18 | Low | Med | PLAUSIBLE | yes | wait | Hall GPIO35 EMI double-count risk (telemetry-only) |
| R19 | Low | High | CONFIRMED | no | now | `driveMode` label mismatch (Gearbox vs RACE) — cosmetic, numbers agree |
| R20 | Low | High | ADJUSTED | yes | wait | WS2812 3.3 V→5 V data marginal (build sheet documents diode/74AHCT125) |
| R21 | Low | High | REFUTED(mech) | no | — | WheelSpeed "integer collapse" — benign resolution floor, telemetry-only |
| R22 | Low | High | CONFIRMED | no | opt | Board #2 stays calm "breathe" forever if board #1 never connects |

Plus an appendix of ~30 additional Low findings carried as-authored (not individually re-verified).

---

# HIGH

### R01 — HUD reads `armed`/`failsafe` telemetry the car never transmits → "LINK LOST" is demo-only
- **Severity/Confidence:** High / High. **Verdict:** CONFIRMED. **HW?** No. **Fix:** now.
- **Affected:** `w17-ground-station` renderer/hud.js (LINK-LOST branch), shared/crsfTelemetry.js, docs/TELEMETRY.md; `w17-control-fw` src/main.cpp telemetry send.
- **Evidence:** Firmware telemetry emits only `buildBatteryFrame` / `buildGpsFrame` / `buildFlightModeFrame("G%u M%u E%u")` (main.cpp:266/277/287). `crsfTelemetry.js` maps only battery/LQ/speed/gear/driveMode/ersPct — never `armed`/`failsafe` (grep: those fields set **only** in `replaySource.js`, the demo). hud.js keys "LINK LOST" off `telem.failsafe`.
- **Why it matters:** The owner's on-screen link-loss/armed indicator can never fire from the real car. On a genuine link loss the serial goes silent → the HUD staleness fallback shows "Telemetry: sim" and keeps animating simulated values — the opposite of an alarm. `npm run demo` hides this because the replay source hand-fills the fields.
- **Expected vs actual:** Expected the HUD to reflect the car's real armed/failsafe. Actual: those fields exist only in the demo path; live path never sets them.
- **How to verify:** (done) grep firmware for any frame carrying armed/failsafe (none); trace crsfTelemetry.js (no such branch). No hardware needed.
- **Note:** This is a **viewer-only HUD**, so it is an *expectations/display* gap, **not** a vehicle-safety defect — the car's own failsafe is independent and verified sound (see R-safety positives).
- **Fix direction:** Either encode armed/failsafe into the FLIGHTMODE status string (extend `"G3 M2 E55"`) and parse them ground-side, or drop them from the TELEMETRY.md contract + hud.js and drive "LINK LOST" from `linkQualityPct==0` + staleness. At minimum document that real link loss reverts the HUD to sim.

### R02 — control-fw `platform = espressif32` UNPINNED while its LEDC HAL uses the core-2.x channel API (removed in core 3.x)
- **Severity/Confidence:** High / High. **Verdict:** CONFIRMED (lead-verified). **HW?** No. **Fix:** now.
- **Affected:** `w17-control-fw` platformio.ini:8; lib/outputs_hal_esp32/src/Esp32LedcPwm.cpp:10-17.
- **Evidence:** `Esp32LedcPwm.cpp` uses `ledcSetup(channel,freq,res)` + `ledcAttachPin(pin,channel)` + `ledcWrite(channel,duty)` — the channel API replaced in Arduino-ESP32 **core 3.x** by `ledcAttach(pin,freq,res)`/`ledcWrite(pin,duty)`. platformio.ini pins no version. **Lead check:** `pio pkg list` shows the unpinned platform currently resolves to `espressif32 @ 7.0.1 → framework-arduinoespressif32 3.20017 (core 2.0.17)`, so it builds today.
- **Why it matters:** **Latent, not live.** It compiles now only because the resolved platform is 7.0.1/core 2.0.17. A future espressif32 release shipping core 3.x — or a fresh checkout after that release — would fail to compile all three servo/ESC outputs, potentially days before the deadline. soundlight-fw already pins `@ ~7.0.1`; control-fw's silence is the asymmetry.
- **Expected vs actual:** Expected a pinned, reproducible toolchain. Actual: unpinned; builds today by luck of resolution.
- **How to verify:** temporarily set the platform to a core-3.x version and observe `ledcSetup`/`ledcAttachPin` compile errors; or `pio pkg show` the resolved core. (No hardware.)
- **Fix direction:** Pin `platform = espressif32 @ 7.0.1` in control-fw (matches soundlight and provides the channel LEDC API). If a future core-3.x move is wanted, migrate the HAL to `ledcAttach`/`ledcWrite(pin,…)` first, then pin — never leave unpinned.

### R03 — Packaged `.exe` never rebuilds `serialport` for Electron's ABI → ships telemetry-disabled
- **Severity/Confidence:** High / High. **Verdict:** CONFIRMED (lead-verified). **HW?** No. **Fix:** now.
- **Affected:** `w17-ground-station` package.json (scripts), electron-builder.yml:33-38, main/CrsfSerialSource.js.
- **Evidence:** `build` script = `electron-builder --win` with no rebuild; there is **no `app:rebuild` script**; `@electron/rebuild` is a devDependency but nothing invokes it; electron-builder.yml:34 references a nonexistent "app:rebuild step"; no beforeBuild/afterPack hook. An ABI-mismatched `serialport` throws at `require()`, which `CrsfSerialSource._open()` catches → logs "serialport unavailable … telemetry disabled".
- **Why it matters:** The shippable `.exe` — the whole point of the ground-station deploy path — would silently run **without** real battery/LQ/speed/gear telemetry, falling back to simulation. Green CI proves nothing here (CI never packages).
- **Expected vs actual:** Expected the packaged app to load a matched native serialport. Actual: no rebuild step; likely ABI mismatch → telemetry silently off.
- **How to verify:** On Windows, `npm run build`, install, connect FT232, `W17_TELEMETRY_SOURCE=crsf-serial`, check log for "CRSF serial open" vs "serialport unavailable"; or inspect the unpacked `.node` NODE_MODULE_VERSION vs Electron 31.
- **Fix direction:** Add an `app:rebuild` npm script (`electron-rebuild -f -w serialport`) and chain it before electron-builder (or a beforeBuild hook); verify the unpacked `.node` ABI matches Electron 31.

### R04 — ESC signal pin (GPIO14) floats high-Z from reset until `escPwm.begin()`; no external pull-down
- **Severity/Confidence:** High / Medium. **Verdict:** CONFIRMED (ordering) / PLAUSIBLE (effect). **HW?** Yes. **Fix:** wait.
- **Affected:** `w17-control-fw` src/main.cpp setup() (crsf/link2/adc/hall begins at 173-176, `escPwm.begin()` at 181); Esp32LedcPwm.cpp:9-13; wiring/BOM (no signal pull-down specified). Same class: steering GPIO13.
- **Evidence:** GPIO14 is not configured as an LEDC output until main.cpp:181, after four other `begin()` calls. Before `ledcAttachPin` it is a default input (high-Z). The "safe initial pulse" comment governs the value *after* attach, not the pre-attach float window. No signal-line pull-down in the wiring docs.
- **Why it matters:** From power-on until setup() reaches `escPwm.begin` (tens of ms), the ESC signal floats. A powered, armed ESC could misread noise as pulses. On the finished car both boards power together, so the ESC IS powered during this window. GPIO14 is not a strapping pin, so the bootloader itself is benign — the risk is the undriven window + a signal-latching ESC.
- **Expected vs actual:** Expected a defined safe level throughout boot. Actual: floating window; safe behavior relies on the ESC's own power-on-neutral-arming and the D8 rule to not power the ESC until failsafe is proven.
- **How to verify (HARDWARE REQUIRED):** scope GPIO14 (and GPIO13) from rail-up through end of setup(); confirm no ESC twitch. Confirm the QuicRun requires clean neutral before it arms.
- **Fix direction:** Add a hardware pull-down / small RC on the ESC (and steering) signal lines so the float reads as a safe/no-pulse level; optionally move `escPwm.begin` to the first line of setup(). Document in the atlas/BOM.

---

# MEDIUM

### R05 — Gear count inconsistent across the system (link2 doc 6 / firmware 4 / HUD 8)
- **Sev/Conf:** Medium / High. **Verdict:** CONFIRMED. **HW?** No. **Fix:** now.
- **Affected:** `w17-control-fw` lib/gearbox/Gearbox.hpp:26 (`numGears=4`), docs/link2_protocol.md:39 (`1..6`); `w17-ground-station` renderer/hud.js / shared/feelConstants.js (`FEEL.gears=8`); FLIGHTMODE `G%u` field is unbounded.
- **Why it matters:** The car sends its real gear (1..4) via FLIGHTMODE; the HUD renders it against an 8-gear ring + `computeCaps` table + redline logic, so the car's top gear shows mid-ring and the per-gear speed caps are meaningless. Board #2 also reads gear expecting 1..6 per the copied doc.
- **Expected vs actual:** Expected one shared gear count. Actual: three (4/6/8).
- **How to verify:** compare the three cited constants; `npm run demo` and watch the gear ring vs a live G4. (No hardware.)
- **Fix direction:** Make `FEEL.gears` and the link2 doc match the gearbox `numGears` (4), ideally via the shared feel-constants the ground station already imports; or make the HUD ring adapt to the received range.

### R06 — `lib/link2` duplicated across firmware repos with no CI drift guard
- **Sev/Conf:** Medium / High. **Verdict:** CONFIRMED. **HW?** No. **Fix:** now.
- **Affected:** `w17-control-fw` + `w17-soundlight-fw` lib/link2/{Link2Frame.hpp,Link2Codec.hpp,Link2Codec.cpp}, docs/link2_protocol.md.
- **Why it matters:** The two copies are byte-identical today (lead diff), but they are copied not shared, and nothing (no submodule, no CI diff check) keeps them in sync. A one-sided edit silently breaks the control↔sound wire contract — the hardest class of bug to diagnose because both build and test green independently.
- **Expected vs actual:** Expected a single source of truth. Actual: two independent copies + a third (JS) reimplementation of the CRSF sibling (see R07).
- **How to verify:** `diff` the files across repos (identical now); note there is no CI job doing so.
- **Fix direction:** A CI check (in both repos, or a shared workflow) that fails if the link2 files/doc diverge; or promote link2 to a git submodule / shared package.

### R07 — CRSF decoder reimplemented (C++ ↔ JS) with no shared fixture; a comment overstates the coupling
- **Sev/Conf:** Medium / Medium. **Verdict:** ADJUSTED (High→Med). **HW?** No. **Fix:** now.
- **Affected:** `w17-control-fw` lib/crsf; `w17-ground-station` shared/crsf.js + test/crsf.test.js + test/crsfTelemetry.test.js.
- **Evidence:** `crsf.test.js` builds frames with a *local* JS `buildFrame` helper and hand-written payloads; the only genuinely shared vector is the CRC catalog check `0xBC`. No cross-repo golden-vector file is imported. The `crsf.js` header comment says "The firmware's golden vectors are reused as tests" — an overstatement.
- **Why it matters:** If the firmware changes a CRSF byte layout (e.g. a telemetry frame), the JS tests — which reconstruct frames from the *assumed* layout — would not fail, so the ground decode could silently disagree. Adjusted down from High because the surfaces are small/stable and both sides self-test.
- **How to verify:** read the JS tests (done) — no firmware fixture import.
- **Fix direction:** Emit a small shared golden-vector fixture (hex frames) from the firmware and load it in the JS tests; or at least correct the comment to reflect that layouts are hand-mirrored.

### R08 — Pin GPIO numbers not verified against the physical build (the atlas is illustrative-only)
- **Sev/Conf:** Medium / Medium. **Verdict:** ADJUSTED. **HW?** Yes. **Fix:** wait.
- **Affected:** `w17-control-fw` lib/config/PinMap.hpp; docs/w17_wiring_assembly_atlas.html; CLAUDE.md §1; docs/bill_of_materials_v2.md.
- **Evidence:** CLAUDE.md §1 calls the pins a "STARTING PROPOSAL … reconcile against the wiring atlas." **Correction from verification:** the atlas is a topology/channel diagram (`CH1/CH2/CH3 PWM`, "ADC pin") with **no GPIO numbers**, and its footer states pin numbers are "illustrative — your firmware defines the real ones." The hardware reviewer found PinMap.hpp **does** agree with CLAUDE.md + BOM. So there is **no doc contradiction**; the only open item is that the physical GPIO assignments have never been continuity-checked against the actual soldered board.
- **Why it matters:** A single mis-assigned actuator pin is a bring-up hazard (servo/ESC/DRS on the wrong pin, or an input-only pin driven). Internally the docs are consistent; the risk is purely physical-vs-config.
- **How to verify (HARDWARE):** continuity-check every signal from the ESP32 pin to its connector against PinMap.hpp during assembly (D8).
- **Fix direction:** Treat PinMap.hpp + BOM as the authority (they agree); add a one-line note that the atlas is illustrative; verify continuity at the bench.

### R09 — ESC 2000 ms arm-hold and forward/brake mode are unverified guesses vs the QuicRun 10BL120
- **Sev/Conf:** Medium / Medium. **Verdict:** CONFIRMED (uncited) / PLAUSIBLE (behavior). **HW?** Yes. **Fix:** wait.
- **Affected:** `w17-control-fw` lib/outputs/EscOutput.hpp:14 (`bootArmHoldMs=2000`); gearbox brake pass-through (Gearbox.cpp), Link2Sender (reverse=false).
- **Evidence:** `bootArmHoldMs=2000` cites CLAUDE.md §6.3 but no QuicRun spec; D8 says "~2 s" as an assumption. The firmware assumes forward/brake ESC mode (the "brake" stick region) — but cannot enforce the ESC's configured mode; in forward/reverse mode that region would command ungoverned reverse.
- **Why it matters:** If the ESC needs longer to arm (or range-calibration), the first throttle after 2 s could be ignored or misread; wrong ESC mode turns "brake" into full reverse. D8 Phase 7 mandates forward/brake operationally, so it's covered by procedure, not code.
- **How to verify (HARDWARE):** bench the QuicRun arm time; confirm forward/brake mode set in the ESC; verify first post-hold throttle spins the motor (wheels off ground).
- **Fix direction:** Confirm arm time from the manual/bench and set `bootArmHoldMs` (tunable in the tuning build); document the required ESC mode as a flash-day checklist gate.

### R10 — Steering endpoints default to the full servo range (500–2500 µs); no mechanical travel limit
- **Sev/Conf:** Medium / High. **Verdict:** CONFIRMED. **HW?** Yes. **Fix:** wait.
- **Affected:** `w17-control-fw` lib/outputs/ServoOutput.hpp (min=500/max=2500), ServoOutput.cpp:24-28 (clamp only to those).
- **Evidence:** setPosition maps full stick onto center±(endpoint) and clamps only to 500/2500 — the DS3235SG's full ~180°. No per-installation travel limit; steering min/max are **not** console-tunable (only center/trim are → a code edit + reflash to change).
- **Why it matters:** Full stick can drive a 35 kg·cm servo hard against the linkage stops — stall heat, current draw, stripped gears / snapped tie-rod. A real mechanical-damage path on a gift car.
- **How to verify (HARDWARE):** D8 Phase 6 — with linkage fitted, sweep full left/right; confirm no bind/stall; narrow endpoints.
- **Fix direction:** Set conservative steering endpoints for the actual linkage; consider exposing steering min/max to the tuning console.

### R11 — HAL layer (`*_hal_esp32`) untested on hardware; soundlight has no Wokwi sim
- **Sev/Conf:** Medium / High. **Verdict:** ADJUSTED (High→Med). **HW?** Yes. **Fix:** wait (add sim = now, optional).
- **Affected:** both firmwares' `*_hal_esp32` libs (LEDC, ADC, Hall ISR, NVS, I2S, NeoPixel); `w17-soundlight-fw` (no wokwi.toml/diagram.json).
- **Evidence:** By design the HALs are thin seams excluded from native tests; none is exercised by any automated build/sim. Control-fw has a Wokwi sim; soundlight does not. **Adjustment:** the agent's "soundlight zero pre-hardware validation" is overstated — soundlight has a native integration test + `SimLink2Feeder` (esp32dev_sim); it just lacks a Wokwi visual sim.
- **Why it matters:** ISR concurrency, LEDC timing, ADC calibration, NVS wear, I2S DMA, NeoPixel timing are exactly where hardware surprises live, and are the least-covered code.
- **How to verify (HARDWARE):** the D8 runbook + a soundlight bench bring-up.
- **Fix direction:** (optional, now) add a soundlight Wokwi sim mirroring control-fw's; otherwise this closes only on the bench.

### R12 — Brownout/reset transient relies on ESC signal-loss behavior; no explicit brownout config
- **Sev/Conf:** Medium / Medium. **Verdict:** CONFIRMED (config) / PLAUSIBLE (effect). **HW?** Yes. **Fix:** wait.
- **Affected:** `w17-control-fw` platformio.ini (no brownout/WDT flags), src/main.cpp boot sequence.
- **Evidence:** No brownout/WDT build flags anywhere. On reset the board correctly boots Safe (everReceivedFrame_→false) and re-runs the 2 s arm — good — but the coast during the transient is ESC-dependent and unverified; the two-UBEC split lets the ESP32 rail sag independently of motor power.
- **Why it matters:** If the ESP32 resets mid-drive, the ESC holds its last throttle until it detects signal loss, then the firmware re-arms over 2 s. The car may coast at the last commanded throttle during that window.
- **How to verify (HARDWARE):** sag the ESP32 rail / reset mid-throttle (wheels off ground); measure motor coast time and ESC signal-loss behavior.
- **Fix direction:** Verify the ESC's signal-loss timeout; ensure bulk capacitance on the clean rail; optionally document/assert the brownout detector level.

### R13 — Uplink telemetry uses sync/address `0xC8`; ELRS relaying it (and address correctness) is unverified
- **Sev/Conf:** Medium / Medium. **Verdict:** CONFIRMED (unverified) / PLAUSIBLE. **HW?** Yes. **Fix:** wait.
- **Affected:** `w17-control-fw` lib/crsf/CrsfFrameBuilder.hpp (`buildFrame` uses `kSyncByte=0xC8`), src/main.cpp:266-293 (battery/GPS/flightmode writes to Serial2 TX).
- **Evidence:** All uplink telemetry frames are built with first byte `0xC8` (the FC/broadcast address) and pushed straight out UART2 TX. **Open protocol question:** for device→RX→TX telemetry, standard CRSF often uses an *extended* frame header with a device/destination address (e.g. `0xEC`/`0xEE`) rather than `0xC8`. Whether RP1/ELRS relays these `0xC8` sensor + GPS + flight-mode-string frames onto the downlink at all is a hardware truth.
- **Why it matters:** If ELRS doesn't relay `0xC8`-addressed locally-originated sensor frames (or needs an extended header), the entire real-telemetry feature (battery/speed/gear/ERS on the HUD) silently produces nothing — while everything builds and unit-tests green.
- **How to verify (HARDWARE):** the FT232 + ELRS TX + RP1 bench chain; observe whether the frames arrive on the host serial; try `0xC8` vs an extended/device address if they don't.
- **Fix direction:** Verify on the bench; if not relayed, switch to the correct CRSF extended-telemetry addressing.

### R14 — Telemetry return path blocked on the exclusive FT232 COM port
- **Sev/Conf:** Medium / High. **Verdict:** CONFIRMED. **HW?** Yes. **Fix:** wait.
- **Affected:** `w17-ground-station` docs/TELEMETRY.md:52-62, main/CrsfSerialSource.js.
- **Evidence:** elrs-joystick-control holds the FT232 port exclusively; TELEMETRY.md lists three *unresolved* options (elrs-jc forward flag / com0com splitter / not-recommended own-the-port). None validated.
- **Why it matters:** Even if R13 is fine, the ground station can't read the return stream until this is solved; the "real telemetry on the HUD" feature is gated on an external tool's capability.
- **How to verify (HARDWARE/tooling):** determine whether elrs-joystick-control can forward telemetry; else set up com0com/hub4com.
- **Fix direction:** Confirm the forward capability or the splitter path; document the chosen one in SETUP.md.

### R15 — Video path (mediamtx WHEP; H.265→H.264 gate) is an unproven placeholder
- **Sev/Conf:** Medium / High. **Verdict:** CONFIRMED. **HW?** Yes. **Fix:** wait.
- **Affected:** `w17-ground-station` mediamtx/mediamtx.yml, renderer/whep.js, docs/SETUP.md.
- **Evidence:** SETUP.md flags the camera codec as the #1 bench risk: WebRTC needs H.264; the OpenIPC camera's actual codec (H.264 vs H.265) is unverified.
- **Why it matters:** If the camera emits H.265, the WebRTC video underlay won't play without a transcode; the FPV experience (the point of the build) falls back to the VLC path.
- **How to verify (HARDWARE):** point the real camera at mediamtx; confirm H.264 negotiation in the browser/WHEP client.
- **Fix direction:** Confirm/force H.264 on the camera, or add an ffmpeg transcode step; documented already as a bench gate.

### R16 — `main.cpp` orchestration + Wokwi sim not asserted in CI; named coverage gaps
- **Sev/Conf:** Medium / High. **Verdict:** CONFIRMED. **HW?** Partial. **Fix:** now (CI) / wait (HIL).
- **Affected:** `w17-control-fw` src/main.cpp, wokwi.toml; test suites across repos.
- **Evidence:** No unit test exercises the main.cpp control-loop wiring; the Wokwi sim is a manual build, not a CI assertion. Documented gaps: NVS corruption on real flash, ADC saturation extremes, Hall glitch/bounce, board-#2 boot-staleness.
- **Why it matters:** The green suites cover the pure modules (which is where most bugs are), but the integration glue (cadences, ordering, the actual sequencing in loop()) is only "covered" by an un-asserted sim.
- **How to verify:** inspect CI (no sim assertion); enumerate untested scenarios.
- **Fix direction:** Add a headless/automated sim assertion where feasible; add targeted tests for the named gaps (NVS-corruption path already has a guard chain — assert it; ADC extremes; staleness-at-boot for board #2).

### R17 — CI gaps: `esp32dev_tuning` never built; ground CI never packages the app
- **Sev/Conf:** Medium / High. **Verdict:** CONFIRMED. **HW?** No. **Fix:** now.
- **Affected:** `w17-control-fw` .github/workflows/ci.yml (builds native/esp32dev/esp32dev_sim only); `w17-ground-station` .github/workflows/ci.yml (npm ci + vitest only).
- **Evidence:** control CI omits `esp32dev_tuning` — yet D8 flashes exactly that env for the whole bench bring-up. Ground CI never runs `electron-builder` nor rebuilds serialport (compounds R03).
- **Why it matters:** The bench firmware can rot undetected between now and hardware arrival; the shippable `.exe` is entirely untested by CI.
- **How to verify:** read the CI files (done).
- **Fix direction:** Add `pio run -e esp32dev_tuning` to control CI; add a packaging/smoke job (windows-latest, `electron-builder --dir` + serialport rebuild) to ground CI.

---

# LOW

### R18 — Hall GPIO35 EMI double-count risk (2 ms software lockout only)
- **Sev/Conf:** Low / Medium. **Verdict:** PLAUSIBLE. **HW?** Yes. **Fix:** wait.
- **Affected:** `w17-control-fw` lib/telemetry_hal_esp32/Esp32HallPulseCounter (2 ms lockout, RISING).
- **Why it matters:** GPIO35 has no Schmitt trigger; heavy motor EMI on a slow edge could double-count despite the 2 ms lockout. **Telemetry/ERS only — not in the control/safety path**, so worst case is a wrong speed readout / spurious ERS harvest.
- **How to verify (HARDWARE):** scope the Hall line at full throttle near the motor; add the D8-noted 1–10 nF RC if the edge is ugly.
- **Fix direction:** hardware RC filter if bench scoping shows double-counts.

### R19 — `driveMode` label mismatch (firmware/link2 "Gearbox/Gearbox+ERS" vs HUD/TELEMETRY.md "RACE/ERS")
- **Sev/Conf:** Low / High. **Verdict:** CONFIRMED (downgraded from Med). **HW?** No. **Fix:** now.
- **Affected:** `w17-control-fw` link2_protocol.md:43 / Link2Frame.hpp; `w17-ground-station` renderer/hud.js (`DRIVE_MODES`), docs/TELEMETRY.md.
- **Why it matters:** Numbers agree (0/1/2) so there is **no functional impact** — purely a naming/doc-clarity inconsistency the owner reads differently in different places.
- **Fix direction:** Pick one label set (the HUD's is user-facing) and align the protocol doc + Link2Frame comment to it.

### R20 — WS2812 3.3 V data into 5 V strip is marginal (documented mitigation exists)
- **Sev/Conf:** Low / High. **Verdict:** ADJUSTED (Med→Low). **HW?** Yes. **Fix:** wait.
- **Affected:** `w17-soundlight-fw` lib/config/PinMap.hpp; `w17-control-fw` docs/00_BUILD_SHEET.md:35.
- **Evidence:** build sheet documents "1N5819 diode → ~3.0 V threshold **or** 74AHCT125 shifter" — so it is a documented, mitigated choice, not an undocumented marginal hack (agent's framing corrected).
- **Why it matters:** the diode-drop trick is marginal at higher strip currents; the 74AHCT125 is the robust option.
- **How to verify (HARDWARE):** confirm the strip latches data reliably at operating current; prefer the 74AHCT125.
- **Fix direction:** use the 74AHCT125 level shifter (already documented as the alternative).

### R21 — WheelSpeed "integer-division collapse" — mechanism refuted; benign resolution floor
- **Sev/Conf:** Low / High. **Verdict:** REFUTED (mechanism). **HW?** No. **Fix:** none needed.
- **Affected:** `w17-control-fw` lib/telemetry/WheelSpeed.cpp:48-54.
- **Evidence:** `impliedCeilingRpm = 60000/elapsedMs` ≈ 40 rpm one tick before the 1500 ms timeout — **not** near-zero; the division doesn't collapse. Real behavior is a benign resolution floor: wheels below ~40 rpm read 0. **Telemetry/ERS only.**
- **Why it matters:** minimal — a low-speed telemetry readout floor, no control impact. Listed for completeness / to close the original claim.
- **Fix direction:** none required; if sub-40 rpm readout matters, raise `zeroSpeedTimeoutMs`.

### R22 — Board #2 stays in a calm "breathe" forever if board #1 never connects
- **Sev/Conf:** Low / High. **Verdict:** CONFIRMED (intentional). **HW?** No. **Fix:** optional.
- **Affected:** `w17-soundlight-fw` lib/lights/LightRenderer.cpp (NeverConnected branch).
- **Why it matters:** benign — a never-connected board #1 means a non-moving car with a silent engine; a *cut* wire mid-run correctly reads Lost→hazard. Diagnostics-only gap.
- **Fix direction:** optional — after N seconds of NeverConnected, escalate to a distinct slow amber to aid wiring diagnostics.

---

# Appendix — Low findings carried as-authored (NOT individually re-verified)

These come from the dimension investigations and are preserved in `_raw_audit_findings.json`
(and the safety/build JSONs). They were not part of the adversarial re-check; treat their
severity/confidence as the investigating agent's, pending a second pass.

- **spec:** camera gimbal listed "decoded, unwired" in D8 but fully wired in main.cpp (stale doc); GPS-frame km/h×10 truncation caps low-speed resolution; CLAUDE.md "first deliverable, stop and show me" gate long overtaken by scope.
- **arch:** soundlight `lib/link2/library.json` description is a stale verbatim copy; `feelConstants.js`/`crsf.js` reference a firmware source path/claim that is stale.
- **hw:** ESC/steering pins float pre-setup (same root as R04); Hall plausibility clamp (5000 rpm) vs 2 ms lockout assume different top speeds; WS2812 power budget only checks amber (2-channel), white halo (3-channel) not budgeted; ADC has no headroom margin if divider tolerance / slightly-over-8.4 V.
- **ctrl_fw:** FLIGHTMODE string reads controlSnapshot fields updated at a different cadence (benign staleness); WheelPulseSnapshot count/period can tear briefly (telemetry-only); no explicit loop/task WDT policy; gimbal servos driven even in Training/plain mode.
- **sl_fw:** production `volumeFor` mapping untested (tests exercise a different formula); overrun crackle burst (noiseAmpMax×3) not in the compile-time headroom budget; decoded wheel-rpm field unused on board #2; NeoPixel show() core/interrupt interaction (see open Q — likely RMT, moot); `i2s.begin()` return codes ignored.
- **gs:** default launch shows a fully-simulated HUD mistakable for real data; stale merged telemetry fields never cleared after a frame type stops; non-Windows telemetry-port default + reader CRSF baud unproven; no raw-frame/decode-error diagnostics for bring-up.
- **build:** soundlight NeoPixel `^1.12.0` caret not tightly pinned; control-fw has no README / flash-port guidance (env-confusion hazard); no Node `engines`/.nvmrc; inert `allowScripts` key (no lavamoat consumer); CI PlatformIO cache key hashes only platformio.ini (compounds R02).

*(Several of these are sub-facets of the verified R## items above; a future pass can merge or
promote any that warrant it.)*
