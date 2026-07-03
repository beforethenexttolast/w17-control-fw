# Open Questions

Questions raised across the audit that the code/docs alone can't answer. Tagged:
**[HW]** = needs the physical bench to resolve · **[OWNER]** = a decision only you can make ·
**[RESOLVED]** = answered during the verification pass (kept for the record).

Each links to the risk-register entry (`R##`) it feeds where applicable.

## Owner decisions (no hardware needed — you choose)

- **[OWNER] Should the HUD's `armed`/`failsafe` indicators be driven from something real, or are
  you content that a real link loss silently reverts the HUD to simulated values?** (→ R01) The
  car never transmits these today; the "LINK LOST" alarm only fires in demo mode.
- **[OWNER] Which gear count is authoritative — gearbox 4, link2 doc 6, or HUD 8?** (→ R05) This
  sets what the FLIGHTMODE `G%u` string, the protocol doc, and the HUD dial should all use.
- **[OWNER] Which drive-mode labels ship — "Gearbox / Gearbox+ERS" (firmware/doc) or "RACE / ERS"
  (HUD)?** (→ R19) The HUD is the user-facing surface, so its labels are what the recipient reads.
- **[OWNER] Is the `lib/link2` copy into soundlight-fw meant to be permanent (two evolving copies)
  or a bootstrap toward a shared submodule?** (→ R06) Determines whether a CI drift-guard or a
  submodule is the right fix.

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
- **[Q] Is the Arduino `loopTask` subscribed to the task WDT in this platform/framework version,
  and is that the intended policy?** No blocking calls were found in loop(), but there is no
  explicit WDT-driven safe-output on a hang for the control board.
- **[Q] Is the copy of the ERS/feel constants in `shared/feelConstants.js` guaranteed to match the
  firmware, and how is drift caught?** (relates to R06/R07)
- **[Q] When will the PlatformIO `espressif32` platform's newest release ship Arduino core 3.x?**
  (→ R02) That timing turns the unpinned control-fw from latent to a live build failure. Today it
  resolves to 7.0.1 / core 2.0.17.
- **[Q] Is there any committed record of the exact espressif32 version the running hardware was
  validated against, so a future rebuild reproduces the gifted binary?** (→ R02)

## Simulation / test / CI

- **[HW] Has the control-fw Wokwi sim ever actually been run to a live link (failsafe=0), or only
  built? The SIMULATION.md checklist boxes are all unchecked.** (→ R16)
- **[Q] Does the Hall ISR `read()` snapshot need to be coherent? The period-based rpm math assumes
  count and period come from the same edge pair; the two-independent-atomic-loads design permits
  tearing — does the pure logic tolerate a mismatched pair?** (telemetry-only; relates to R18/R21)
- **[Q] Does soundlight's `esp32dev_sim` building in CI give a false sense of a runnable "sim" when
  no wokwi files exist? Was a `diagram.json` intended and dropped?** (→ R11)
- **[Q] Is there any orchestration that would catch link2 protocol drift between the control
  sender, soundlight receiver, and the ground station's third copy of the constants/labels?**
  (→ R06) Today: none.
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
