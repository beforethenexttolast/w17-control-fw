# Verification pass — deduped & adversarially re-checked findings

**Date:** 2026-07-03. **Method:** lead auditor re-read the cited code/docs for every High/Medium
finding across all 10 dimensions (no agent fan-out this round — the earlier 39-agent verify pass
hit the session limit). Cross-dimension duplicates collapsed. **Low findings** are carried
as-authored in `_raw_audit_findings.json` / `_safety_findings.json` / `_build_findings.json`
(not individually re-verified) and will be listed in the register at low severity.

Verdicts: **CONFIRMED** (evidence holds), **ADJUSTED** (real but mis-stated — severity/mechanism
corrected), **REFUTED** (claim does not hold), **PLAUSIBLE** (legitimate but only provable on
hardware). Severity is **post-verification**.

This table is the authoritative backbone for `10_risk_register.md`. `R##` IDs are stable.

## HIGH (verified)

| ID | Finding (deduped) | Verdict | Sev | Evidence / note | HW? |
|---|---|---|---|---|---|
| R01 | HUD reads `armed`/`failsafe` the car never transmits → "LINK LOST" is demo-only | CONFIRMED | High | Firmware telemetry emits only `buildBatteryFrame`/`buildGpsFrame`/`buildFlightModeFrame("G%u M%u E%u")` (main.cpp:266/277/287); `crsfTelemetry.js` never sets armed/failsafe; only `replaySource.js` (demo) does. On a real link loss the serial goes silent → HUD reverts to "Telemetry: sim", never to a warning. Viewer-only, so it's an **expectations/display** gap, not vehicle-safety. Merges spec#1, gs#6, gs#22. | no |
| R02 | control-fw `platform = espressif32` UNPINNED while its LEDC HAL uses the core-2.x channel API removed in core 3.x | CONFIRMED (lead) | High | `Esp32LedcPwm.cpp:10-17` uses `ledcSetup/ledcAttachPin/ledcWrite(channel,duty)`. **Latent, not live:** `pio pkg list` shows the unpinned platform currently resolves to `espressif32 @ 7.0.1 → core 2.0.17` (same as soundlight), so it builds today. Risk = a future platform release shipping core 3.x silently breaks the gift build. Downgraded from the agent's "critical". Merges build#11, arch#17, build#36. | no |
| R03 | Packaged `.exe` never rebuilds serialport for Electron's ABI → ships telemetry-disabled | CONFIRMED (lead) | High | `build` = `electron-builder --win`; no `app:rebuild` script exists; `@electron/rebuild` is a devDep but nothing invokes it; `electron-builder.yml:34` references a nonexistent "app:rebuild step"; no beforeBuild/afterPack hook. ABI-mismatched `serialport` throws at require → `CrsfSerialSource` catch logs "serialport unavailable; telemetry disabled". Merges build#12, build#37. | no |
| R04 | ESC signal pin (GPIO14) floats high-Z from reset until `escPwm.begin()`; no external pull-down | CONFIRMED (order) / PLAUSIBLE (effect) | High | `main.cpp:181` attaches GPIO14 LEDC only after crsf/link2/adc/hall begins (173-176); the "safe pulse" comment governs post-attach value, not the pre-attach float. Whether a powered QuicRun twitches on the floating line needs a scope. Same class: steering GPIO13. Safety#(ESC-float). | yes |

## MEDIUM (verified)

| ID | Finding (deduped) | Verdict | Sev | Evidence / note | HW? |
|---|---|---|---|---|---|
| R05 | Gear count inconsistent: link2 doc `1..6`, firmware `numGears=4`, HUD `FEEL.gears=8` | CONFIRMED | Med | Gearbox.hpp:26 `=4`; link2_protocol.md:39 `1..6`; hud.js `FEEL.gears=8`; FLIGHTMODE `G%u` unbounded. Live gear (1..4) renders against an 8-gear ring/caps table. Merges spec#13, arch#16, gs#24, proto#27. | no |
| R06 | `lib/link2` duplicated across firmware repos with no CI drift guard | CONFIRMED | Med | Codec/Frame/doc byte-identical today (lead diff), but copied not shared; no CI check keeps them in sync. A one-sided edit silently breaks the wire contract. Merges arch#2, proto#29, sim#30. | no |
| R07 | CRSF decoder reimplemented (C++↔JS) with no shared fixture; `crsf.js` comment overstates coupling | ADJUSTED (High→Med) | Med | `crsf.test.js` builds frames with a local JS `buildFrame`; only the CRC catalog value 0xBC is genuinely shared. No cross-repo golden-vector file → a firmware layout change wouldn't fail JS tests. Real drift risk but small/stable surfaces + both self-test. Merges arch#3, proto#7. | no |
| R08 | Pin-map GPIO numbers never reconciled against a pin-level source | CONFIRMED (strengthened) | Med | CLAUDE.md §1 calls the pins a "STARTING PROPOSAL … reconcile against the wiring atlas." The atlas (`w17_wiring_assembly_atlas.html`) is a topology diagram (CH1/CH2/CH3 PWM, "ADC pin") with **no GPIO numbers**, so it cannot resolve the obligation. Bench continuity check is the only proof. spec#15. | yes |
| R09 | ESC boot-arm hold (2000 ms) + forward/brake mode are unverified guesses vs the QuicRun 10BL120 | CONFIRMED (uncited) / PLAUSIBLE | Med | `EscOutput.hpp:14 bootArmHoldMs=2000` has no cited source; forward/brake assumed (Gearbox.hpp:57-61, Link2Sender reverse=false). D8 mandates forward/brake operationally but firmware can't enforce it. Merges ctrl#5, safety#(arm-hold), safety#(DRS/ESC-mode). | yes |
| R10 | Steering endpoints default to full servo range (500–2500 µs); no mechanical travel limit → bind/stall | CONFIRMED | Med | ServoConfig min=500/max=2500; ServoOutput.cpp:24-28 clamps only to those. Full stick can drive the DS3235SG against linkage stops (35 kg·cm → strip/snap). Endpoints must be narrowed at bench; steering min/max are NOT console-tunable (code edit + reflash). safety#(steering). | yes |
| R11 | HAL layer (`*_hal_esp32`) is untested/unsimulated; soundlight has no Wokwi sim | ADJUSTED (High→Med) | Med | HALs are thin seams by design (LEDC/ADC/Hall-ISR/NVS/I2S) — genuinely only provable on hardware. "soundlight zero validation" overstated: it has a native integration test + `SimLink2Feeder` (esp32dev_sim), just no Wokwi. Merges sim#8, sim#9. | yes |
| R12 | Control-board brownout/reset transient relies on ESC signal-loss behavior; no explicit brownout config | CONFIRMED (config) / PLAUSIBLE (effect) | Med | No brownout/WDT flags anywhere; on reset the board correctly boots Safe + re-runs 2 s arm, but the coast during the transient is ESC-dependent and unverified. Two-UBEC split means one rail can sag independently. safety#(brownout). | yes |
| R13 | CRSF telemetry backchannel (battery/GPS/FLIGHTMODE via sync 0xC8) — ELRS actually relaying these is unverified | CONFIRMED (unverified) / PLAUSIBLE | Med | Frames built + written to Serial2 TX (main.cpp:266-293); whether RP1/ELRS schedules arbitrary sensor + GPS + flight-mode-string frames onto the downlink is a hardware truth. Lead flagged this independently earlier. proto#28. | yes |
| R14 | Telemetry return path blocked on the exclusive FT232 COM port | CONFIRMED | Med | docs/TELEMETRY.md:52-62 — elrs-joystick-control holds the port exclusively; three unresolved options (forward flag / com0com / not-recommended). No option validated. spec#14. | yes |
| R15 | Video path (mediamtx WHEP; H.265→H.264 WebRTC gate) is an unproven placeholder | CONFIRMED | Med | The #1 documented bench risk (SETUP.md): WebRTC needs H.264; camera codec unverified. gs#25. | yes |
| R16 | `main.cpp` orchestration + Wokwi sim not asserted in CI; named coverage gaps | CONFIRMED | Med | No unit test exercises main.cpp wiring; the Wokwi sim is a manual build, not a CI assertion. Gaps: NVS corruption on real flash, ADC saturation extremes, Hall glitch/bounce, board-#2 boot-staleness. Merges sim#31, sim#32. | partial |
| R17 | CI gaps: `esp32dev_tuning` never built; ground CI never packages the app | CONFIRMED | Med | control CI builds native/esp32dev/esp32dev_sim only — but D8 flashes esp32dev_tuning for the whole bring-up. Ground CI = npm ci + vitest only. Merges build#38, build#37. | no |
| R18 | Hall GPIO35 EMI double-count risk (2 ms software lockout only) | PLAUSIBLE | Med→Low | GPIO35 no Schmitt trigger; 2 ms lockout mitigates mild bounce; heavy motor EMI may need the D8-noted 1–10 nF RC. **Telemetry/ERS only — not in the control/safety path.** ctrl#20, safety#(hall). | yes |

## LOW (verified/adjusted; plus ~35 low findings carried as-authored)

| ID | Finding | Verdict | Sev | Note |
|---|---|---|---|---|
| R19 | `driveMode` label mismatch: firmware/link2 "Gearbox/Gearbox+ERS" vs HUD/TELEMETRY.md "RACE/ERS" | CONFIRMED | Low | Numbers agree (0/1/2) — purely a naming/doc-clarity issue, **no functional impact**. Downgraded from Med. Merges gs#23, proto#26, spec/arch label items. |
| R20 | WS2812 3.3 V data into 5 V strip | ADJUSTED (Med→Low) | Low | build sheet:35 documents "1N5819 diode → ~3.0 V threshold **or** 74AHCT125 shifter" — documented, not an undocumented marginal hack. Prefer the 74AHCT125 option. hw#18. |
| R21 | WheelSpeed "integer-division collapse" | REFUTED (mechanism) → Low | Low | `impliedCeilingRpm=60000/elapsedMs` ≈ 40 rpm one tick before the 1500 ms timeout, not near-zero. Real behavior = a benign resolution floor (<~40 rpm reads 0), **telemetry-only**. ctrl#19. |
| R22 | Board #2 stays calm "breathe" forever if board #1 never connects | CONFIRMED | Low | Intentional; benign (car can't move, engine silent). A cut wire mid-run correctly reads Lost→hazard. safety#(never-connected)/sl#21. |

## Net effect of verification
- **1 finding refuted/downgraded materially** (R21 mechanism; R04-effect is HW-gated).
- **1 downgraded from Critical→High** (R02 — latent not live).
- **3 downgraded High→Med / Med→Low** (R07, R11; R18, R20 to low).
- **Everything else confirmed.** No new findings surfaced during verification.
- **Post-verification High count = 4** (R01–R04): two are pure-software fixable now (R02, R03),
  one is software/expectations (R01), one is hardware-gated (R04).
