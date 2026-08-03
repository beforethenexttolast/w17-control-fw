# Open Questions

Questions raised across the audit that the code/docs alone can't answer. Tagged:
**[HW]** = needs the physical bench to resolve · **[OWNER]** = a decision only you can make ·
**[RESOLVED]** = answered during the verification pass (kept for the record).

Each links to the risk-register entry (`R##`) it feeds where applicable.

## Owner decisions — ALL FOUR ANSWERED 2026-07-25

> **Three of these four were not open questions.** They were stale readings of the code, which is
> itself the useful finding: an audit-era question can go stale in the *resolved* direction, and a
> later session will then chase a defect that no longer exists. Corrected below rather than ticked.
> See the staleness note at the top of `10_risk_register.md`.

- **[ANSWERED — genuine decision] Should the HUD's `armed`/`failsafe` indicators be driven from
  something real, or are you content that a real link loss silently reverts the HUD to simulated
  values?** (→ R01) **Owner: keep the simulated values, but label them.** No firmware change; the
  ground station must visibly mark armed/failsafe as **SIMULATED** whenever they are not live, so a
  real link loss can never read as armed-and-fine. Extending FLIGHTMODE was rejected for now: it
  fits only just (15 usable chars, `"G4 M2 E100"` already uses 10, so exactly 5 chars spare) and
  the whole path still depends on the unproven [HW] question of whether ELRS relays a
  locally-originated `0x21` frame at all (→ R13). **This was the only one of the four that was
  actually open.** Follow-up is ground-station-side and not done.

- **[CORRECTED — was never a three-way disagreement] Which gear count is authoritative — gearbox 4,
  link2 doc 6, or HUD 8?** (→ R05) **Owner: 4, and it already was.** The premise was a
  **documentation artefact** in two parts: the "6" is `GearboxConfig::kMaxGears`, the per-gear table's
  *array capacity*, misread as a count — `docs/link2_protocol.md` has always read `1…4`; and the "8"
  was in `docs/f1_hud.html`, a standalone themed **design mock**, not the shipping HUD (which already
  uses `feel.GEARS = 4`). Fixed: mock corrected to 4 and labelled non-authoritative, `kMaxGears`
  commented as capacity-not-count, and `numGears == 4` pinned by a native test.

- **[CORRECTED — no user-visible surface exists] Which drive-mode labels ship — "Gearbox /
  Gearbox+ERS" (firmware/doc) or "RACE / ERS" (HUD)?** (→ R19) **Owner: TRAINING / RACE / ERS.**
  The FLIGHTMODE string emits `driveMode` as a **bare integer** (`M2`), so a handset showing the raw
  string displays no mode label and cannot diverge from the HUD — the divergence had nowhere to
  appear. The protocol doc and `ChannelDecoder.hpp` had already adopted RACE/ERS; only four prose
  comments lagged (`GearboxErs` was never a symbol). The iPhone contract's `GEARBOX`/`GEARBOX_ERS`
  stay as **wire identifiers**, now documented as such in `link2_protocol.md`.

- **[ANSWERED] Is the `lib/link2` copy into soundlight-fw meant to be permanent (two evolving copies)
  or a bootstrap toward a shared submodule?** (→ R06) **Owner: permanent, and guarded — not a
  submodule.** A submodule would drag control-only `Link2Sender.cpp` into board #2 as LDF-compiled
  dead code, plus a shared repo/subtree and sibling commits, for a four-file library that has never
  drifted (all four shared files byte-identical as of today). Landed here:
  `tools/link2_copy_check.sh` (distinct exit codes; `--strict` so CI can't pass by the sibling being
  absent; verified to bite on injected drift) and `test_shared_feel_constants_pinned`. **The finding
  conflated the wire format with the feel constants** — separate guards now. Enforcement (a
  soundlight CI step in strict mode) is deliberately **not built** and is soundlight's to own.

**A third stale entry, same pattern** (found while checking the above, listed here so the count is
honest): "[Q] Is the Arduino `loopTask` subscribed to the task WDT…?" under *Firmware / toolchain*
was answered by the R5-a remediation — see the correction there.

## Protocol / relay (mostly hardware-gated)

- **[HW] Does RP1/ELRS actually relay battery (0x08), GPS (0x02) and FLIGHTMODE (0x21) frames
  emitted with a `0xC8` first byte at 420000 baud — or is an extended (dest/origin) frame header
  / device address (e.g. `0xEC`) required for locally-originated sensor telemetry?** (→ R13) The
  entire "send everything over standard relayed frames" claim rests on this.
- **[HW] Will a real ELRS/handset relay a GPS 0x02 frame with only groundspeed set (lat/lon/sats=0)
  and a custom FLIGHTMODE status string "G3 M2 E55" without rejecting/reformatting them?** (→ R13)
- **[HW/tooling] Does elrs-joystick-control expose a telemetry-forward (UDP/stdout/log), or must a
  com0com/hub4com splitter be used?** (→ R14) The whole real-telemetry path hinges on this.
- **[Q] If a handset also displays the raw FLIGHTMODE string, the Gearbox-vs-RACE label divergence
  surfaces there too — is that acceptable?** (→ R19)
- **[Q] link2 flag bit7 is reserved/masked — is a v2 field planned for it? If v2 adds a field, the
  version-before-CRC ordering makes old soundlight report BadVersion and hold last-known state; is
  that the intended degrade for a mixed-firmware bring-up?**
- **[Q] The HUD prefers live `telem.gear/driveMode/ersPct`; if the car ever emits a FLIGHTMODE
  string truncated at `kFlightModeMaxLen`, does `parseFlightMode` fall back per-field gracefully?
  The truncation test only covers the clean-ASCII path, not a mid-token cut.**

## Hardware / electrical (bench-gated)

- **[HW] Does the QuicRun 10BL120 treat a floating/absent PWM signal at ESP32 boot as
  disarmed-neutral, or can boot noise on GPIO14 be misread?** (→ R04)
- **[HW] Does the QuicRun arm with a plain 2000 ms neutral hold, or does it need throttle-endpoint
  calibration / a specific arming gesture?** (→ R09)
- **[HW] Does the QuicRun return output to neutral (motor stop) on signal loss, and how fast?**
  (→ R12) Determines coast behavior during a control-board reset/brownout.
- **[HW] Actual logic level on GPIO14 (ESC) and GPIO13 (steering) during the reset→ledcAttachPin
  window on the real board — does the powered ESC twitch?** (→ R04)
- **[HW] Real minimum Hall edge spacing at full throttle — ~12 ms (5000 rpm clamp) or ~18 ms (ISR
  comment)? Determines whether the 2 ms lockout should be tightened.** (→ R18)
- **[HW] Is the 2 ms Hall lockout adequate against real ESC EMI on the Schmitt-less GPIO35 under
  motor load, or is an RC filter / Schmitt buffer needed?** (→ R18)
- **[HW] Does the 1N5819 diode-drop reliably clear the WS2812 3.3 V logic-high threshold at the
  strip's real current, or is the 74AHCT125 needed?** (→ R20)
- **[HW] With both boards on the shared "clean" BEC rail, does board #2's WS2812 inrush (1000 µF
  cap) or I2S amp draw sag the rail enough to brown out board #1 at power-on?** (→ R12)
- **[HW] Confirm RP1 disconnect-declaration latency at the chosen packet rate + the LQ=0 burst
  timing keeps worst-case failsafe detection within the ~540 ms the firmware budgets.**
- **[HW] Is the OpenIPC camera H.264-capable, or is an ffmpeg transcode required?** (→ R15)

## Firmware / toolchain

- **[HW] Can the ESP32 LEDC achieve true 16-bit resolution at 50 Hz on this board, or does
  ledcSetup silently clamp (which would corrupt every pulse width)?** Analysis says 16-bit is well
  within the ~20.6-bit ceiling at 50 Hz, but confirm on a scope. (relates to R02 HAL)
- **[HW] On core 2.0.17, do `analogSetPinAttenuation` + `analogReadMilliVolts` (the eFuse-cal ADC
  path) behave as the battery code assumes?** Revalidate if ever moved to a 3.x pin.
- **[RESOLVED — stale, answered by R5-a] Is the Arduino `loopTask` subscribed to the task WDT in
  this platform/framework version, and is that the intended policy?** Both parts now have explicit
  answers in `src/main.cpp`. The pinned framework does **not** subscribe `loopTask` by default (it
  subscribes only core-0 idle, at 5 s); R5-a deliberately reconfigures that single global TWDT to
  **2 s (provisional)** and subscribes `loopTask` via the ESP-IDF C API, with every call fail-fatal.
  The policy is precise: the one and only feed is the **last** statement of the 50 Hz control tick,
  so a feed proves a complete actuator iteration ran — Arduino's `enableLoopWDT()`/`feedLoopWDT()`
  are avoided **because** they feed at the top of `loop()` before application code, which would
  defeat exactly that. Note the original worry is still half-true and worth keeping in view: there
  is no WDT-driven *safe-output write*; the response is panic-and-reboot, and reboot-to-safe-output
  timing is unmeasured Phase-B evidence (→ R04/R12, `docs/SIMULATION.md`).
- **[ANSWERED 2026-07-25 — no, and the comment claiming otherwise is an R07-class overstatement]
  Is the copy of the ERS/feel constants in `shared/feelConstants.js` guaranteed to match the
  firmware, and how is drift caught?** (relates to R06/R07) **Not guaranteed.** The values do agree
  today (verified: 26 %/s, 11 %/s, ×1.18, 4 gears across `lib/ers`, `docs/f1_hud.html`, and
  `feelConstants.js`), but nothing enforces it across the boundary. `feelConstants.js:5` says "A test
  guards these against drift" — that test (`test/replay.test.js:77`) only asserts the JS constants
  against **hardcoded literals** and never reads `ErsSystem.hpp`, so it guards the JS file against
  itself. Exactly the pattern already recorded for `crsf.js` under R07.
  This repo has done its half (`test_shared_feel_constants_pinned` pins the firmware side to the same
  written numbers, so both ends are now independently pinned). **Fixing the claim, or making the JS
  test actually derive from the firmware, is a ground-station job and is not done.**
- **[Q] When will the PlatformIO `espressif32` platform's newest release ship Arduino core 3.x?**
  (→ R02) That timing turns the unpinned control-fw from latent to a live build failure. Today it
  resolves to 7.0.1 / core 2.0.17.
- **[Q] Is there any committed record of the exact espressif32 version the running hardware was
  validated against, so a future rebuild reproduces the gifted binary?** (→ R02)

## Simulation / test / CI

- **[CLOSED 2026-08-04] Has the control-fw Wokwi sim ever actually been run to a live link
  (failsafe=0), or only built?** (→ R16) **Yes — it reaches a live link.** The owner loaded the
  `esp32dev_sim` build in Wokwi on 2026-08-04 and CRSF decoded over the 420000-baud `TX2→RX2`
  loopback with no baud override: `failsafe=0` appears on the second `[state]` line after the
  feeder starts sending, ≈2.5 s from boot, and the arm gate, gearbox, ERS overboost, both failsafe
  paths (frame-timeout **and** LQ=0-with-fresh-frames), and the fresh-neutral re-arm rule all
  behaved as `docs/SIMULATION.md` said they would. Full transcript and a phase-by-phase evidence
  table are in that file under "Observed run — 2026-08-04". The question that carried since the
  audit is answered; the *tooling* tag was right — one owner action closed it.
  **What the run did NOT close, and is now tracked below:** the R5-b watchdog observation (still
  never run — different binary), the battery-pot reading, and the spurious wheel pulses.
- **[OWNER/tooling] R5-b: stall → 2 s TWDT panic → reboot → `reset=TASK_WDT` with the retained
  boot counter incremented — still never observed.** The injector is built and ELF-verified
  (`W17_SIM_WDT_STALL`, re-verified 2026-08-04: once in the stall ELF, zero times in `esp32dev`,
  `esp32dev_tuning`, `esp32dev_sim`), but it is a *separate ad-hoc build* and nobody has started
  it. The 2026-08-04 sim run booted `reset=POWER_ON boots=1 retained=no`, making it a fourth
  POWER_ON data point rather than evidence for the crash-class branch — that branch and the RTC
  retained-counter increment are still native-test-only, zero-for-four in the real world.
  Recipe (one command, one `Wokwi: Start Simulator` click, ~6 s of simulated time):
  `docs/SIMULATION.md` → "Watchdog stall validation". Note this closing R5-b would **not** promote
  the 2 s timeout out of provisional, nor any of the three Phase-B items in that file's limits
  section — a sim confirms the mechanism fires, not that 2 s is the right number under real load.
- **[Q — new 2026-08-04] Why does the Wokwi battery pot read ≈37 % when `diagram.json` presets
  `value: "69"`?** Observed `batt=1384mV` flat for ~14 s, then a noisy climb to a steady 4588 mV
  (≈1240 mV at the pin through the 37/10 divider). `BatteryMonitor` behaved correctly on that
  input — seeded from the first sample, EMA-smoothed, `lowBatt` latched after 3 s below 7000 mV
  and correctly refused to clear below 7400 mV — so this is Wokwi's potentiometer/ADC model or
  `analogReadMilliVolts`'s calibration fallback with no eFuse Vref, not firmware. Telemetry-only,
  no control authority. **Consequence:** the sim cannot sanity-check the divider maths; battery
  ADC calibration stays a Phase-B bench item exactly as before.
- **[Q — new 2026-08-04] Why did GPIO35 produce two wheel-pulse edge pairs with nobody clicking
  the button?** `rpm` read 600 then 1500 during DRIVING (implied edge spacings 100 ms and 40 ms —
  real pairs, well outside the 2 ms ISR lockout, below the 5000 `maxPlausibleRpm` clamp) on a pin
  held at 3V3 through a 10 kΩ pull-up. The decay tail afterwards was textbook-correct
  (`176 → 71 → 44 → 0`, matching `60000/elapsed` then the 1500 ms hard-zero timeout), which is
  real evidence for that logic. Unresolved whether the edges are Wokwi's pin model or something
  the real A3144 wiring would also see; the sim cannot distinguish them, so Hall bounce under
  real pulses stays a bench item.
- **[Q] Does the Hall ISR `read()` snapshot need to be coherent? The period-based rpm math assumes
  count and period come from the same edge pair; the two-independent-atomic-loads design permits
  tearing — does the pure logic tolerate a mismatched pair?** (telemetry-only; relates to R18/R21)
- **[Q] Does soundlight's `esp32dev_sim` building in CI give a false sense of a runnable "sim" when
  no wokwi files exist? Was a `diagram.json` intended and dropped?** (→ R11)
- **[PARTLY RESOLVED 2026-07-25] Is there any orchestration that would catch link2 protocol drift
  between the control sender, soundlight receiver, and the ground station's third copy of the
  constants/labels?** (→ R06) **The question conflated two separate things**, which is why "one
  guard" never fit: the **wire format** (this repo ↔ soundlight) and the **feel constants**
  (`lib/ers` ↔ GS `feelConstants.js` ↔ this repo's `docs/f1_hud.html`, which never touch a wire).
  Now, per repo:
  - **Wire format, hermetic (runs in every `pio test -e native`):** `test_golden_frame_bytes` pins
    the exact 14 bytes and `test_crc_matches_crsf_implementation` pins the CRC against `lib/crsf`.
    Catches *this* repo changing the format; cannot see the sibling.
  - **Wire format, cross-repo:** `tools/link2_copy_check.sh` compares against a checked-out
    `../w17-soundlight-fw`. Exit 0 ok/skipped, 1 drifted, 2 could-not-check; `--strict`
    turns an absent sibling into a hard failure so CI cannot pass by absence. Two tiers: the four
    shared **code** files are fatal on any difference; **`docs/link2_protocol.md` — which is also a
    copy, a fact the original R06 entry missed** — is reported non-fatally, because it legitimately
    carries repo-local prose and only its normative parts must match.
  - **Feel constants:** `test_shared_feel_constants_pinned` pins the four cross-repo numbers from
    this side (26 %/s, 11 %/s, ×1.18, 4 gears).
  - **Still none:** any *automatic* orchestration. The copy-check only bites when someone runs it —
    the enforcing CI step belongs in soundlight-fw and is **not built**. Until then, run the script
    by hand after any `lib/link2/` change.
- **[Q] Does `npm ci` on CI build serialport's native binary against Node 20 at all, and is it ever
  exercised by `npm test` — or is serialport entirely absent from the tested path?** (→ R03/R17)

## Resolved during the verification pass (kept for the record)

- **[RESOLVED] Does the atlas agree with PinMap.hpp on every pin?** The atlas has **no GPIO
  numbers** (topology/channel diagram) and its footer says pin numbers are "illustrative — your
  firmware defines the real ones." PinMap.hpp agrees with CLAUDE.md + BOM. So there is no doc
  contradiction; only a physical continuity check remains. (→ R08)
- **[RESOLVED] Does espressif32 7.0.1 actually provide the legacy IDF-4.4 `driver/i2s.h` the audio
  HAL needs?** Yes — `pio pkg list` shows 7.0.1 → framework-arduinoespressif32 3.20017 (**core
  2.0.17 / IDF 4.4**), so the legacy I2S driver is present and the soundlight pin + HAL comment are
  self-consistent. The worry that "7.x ships core 3.x" is false for 7.0.1.
- **[RESOLVED] Is `crsf.js`'s "firmware golden vectors are reused" claim accurate?** No — the JS
  tests reconstruct frames locally; only the CRC catalog value 0xBC is shared. Comment overstates
  coupling. (→ R07)
- **[RESOLVED] Does the WheelSpeed decay "collapse to near-zero" one tick before timeout?** No —
  it's ~40 rpm at the boundary; the real effect is a benign sub-40 rpm resolution floor,
  telemetry-only. (→ R21)

## Still needs a dedicated look (not yet investigated to closure)

- **[Q] Adafruit_NeoPixel on core 2.0.x here: RMT backend (no interrupt disable) or bit-bang
  fallback?** The HAL header claims RMT; if so the "show() disables interrupts / disturbs audio"
  concern is moot — confirm on hardware. (soundlight)
- **[Q] Is 22050 Hz / 256-frame / 6-DMA-buffer (~70 ms) audio latency + task priority 5 on core 0
  enough to avoid underrun under real system load?** render() is integer O(partials·frames) and
  should be well under real-time, but unmeasured. (soundlight)
