# 09 — Build / Deploy / Configuration Review

Source: `_build_findings.json` (category-9 dimension, including its `_lead_verification`
block); severities per `_verification_results.md`. Findings reference `10_risk_register.md`.

## Verdict

The native-test-first, HAL-seam build architecture is genuinely well built and the docs are
unusually thorough — but the build/deploy layer carries **one latent time-bomb and several
deploy gaps that a green CI hides**. The headline: control-fw pins nothing
(`platform = espressif32`) while its LEDC HAL uses the channel API **removed in Arduino-ESP32
core 3.x**. **Lead-verified correction:** the unpinned platform *currently* resolves to
`espressif32 @ 7.0.1 → core 2.0.17` — identical to soundlight's pin — so it builds today and
the boards are *not* currently divergent; the original "critical" was downgraded to a **latent
High**. On the ground-station side, CI proves only that pure-logic tests pass: it never
packages the Electron app nor rebuilds serialport for Electron's ABI, and electron-builder.yml
references an `app:rebuild` step that **does not exist** — so the shippable `.exe` would
silently run telemetry-disabled (confirmed). `board = esp32dev` is correct for the WROOM-32
DevKit. The core theme is **reproducibility**: it builds today largely because the caches
happen to hold 7.0.1.

## What is genuinely well-designed

- **The PlatformIO footgun is documented at the point of use**: control-fw's comment that a
  child `build_flags` REPLACES the parent's (hence the `${env:esp32dev.build_flags}`
  interpolation).
- **Native env hardened three ways** against hardware code leaking into host tests
  (`test_build_src=no`, explicit `lib_ignore`, HAL `library.json` platform declarations).
- **soundlight's pin is exemplary**: `@ ~7.0.1` with an in-file comment tying the pin to the
  concrete dependency (legacy IDF-4.4 `driver/i2s.h`) — the right instinct; its absence in
  control-fw is the glaring asymmetry.
- **Gift-vs-bench firmware is a deliberate compile-time split**: tuning console + sim feeder
  behind `-D` flags; the delivered `esp32dev` build has no UART0 surface; D8 Phase 11 mandates
  reflashing plain esp32dev before gifting.
- **run.js / ensure-electron.js solve real environment hazards** (VS Code's
  `ELECTRON_RUN_AS_NODE` leak; script-gated Electron installs), cross-platform, with the why
  documented.
- **package-lock.json is committed**; mediamtx (v1.9.3) and NeoPixel versions pinned/commented.
- **Viewer-only design bounds the blast radius**: a ground-station build/deploy failure can
  never affect the car; the VLC fallback is documented.
- **D8 is a safety-ordered flashing runbook**: which env on which board, wheels-off gates,
  failsafe-proof-before-ESC-power.

## Findings (→ risk register)

| R## | Sev (verified) | Finding | Build angle |
|---|---|---|---|
| R02 | High (lead-downgraded from Critical) | Unpinned platform + core-2.x-only LEDC channel API | **Latent, not live**: currently resolves to 7.0.1/core 2.0.17 and builds; a future core-3.x platform release breaks the gift build on any fresh checkout. One-line fix, do now |
| R02 (merged) | Med | The matched board pair resolves toolchains independently | Currently identical (verified); divergence is unguarded — pin both to the same version |
| R03 | High (CONFIRMED) | serialport never rebuilt for Electron's ABI; referenced `app:rebuild` doesn't exist | `@electron/rebuild` is a dead devDependency; `build` = bare `electron-builder --win`; the packaged app degrades silently to telemetry-disabled |
| R17 | Med | CI gaps: `esp32dev_tuning` never built; ground CI never packages | The bench firmware (the one D8 actually flashes) can rot undetected; CI tests everything except the deliverable `.exe` |

**Carried Low (register appendix):**
- **NeoPixel `^1.12.0` caret** permits any 1.x — loose next to the platform pin's care; minor
  RMT/timing-change risk on a strip that shares timing with audio DMA.
- **Flashing workflow underspecified**: no control-fw README; no per-OS port guidance; three
  envs distinguished only by the `-e` flag — env-confusion hazard (gift must ship plain
  `esp32dev`; D8 Phase 11 mitigates).
- **No Node floor declared** (`engines`/.nvmrc): local 26 / CI 20 / Electron-bundled ~18 all
  differ; scripts silently assume Node 18+ (`fetch`).
- **Inert `allowScripts` key**: the lavamoat consumer isn't installed; the key implies a script
  gate this repo doesn't enforce (behavior depends on the developer's npm environment).
- **CI cache key hashes only platformio.ini** — with an unpinned platform the key never
  changes, so CI can keep passing on a stale cached core while a fresh checkout would break
  (compounds R02).

## Open questions

- When will the newest `espressif32` release ship Arduino core 3.x? That timing turns R02 from
  latent to live. (Today: 7.0.1 / core 2.0.17.)
- Does `npm ci` on CI build serialport's native binary at all, and does any test exercise it —
  or is serialport entirely absent from the tested path? (→ R03/R17)
- Is there a committed record of the exact espressif32 version the gifted hardware was
  validated against, for reproducible rebuilds?
- On core 2.0.17, do `analogSetPinAttenuation` + `analogReadMilliVolts` behave as the battery
  code assumes? Revalidate if ever moved to a 3.x pin.

## Hardware validation hooks

`11_hardware_validation_plan.md`: **A1.1** (pin the platform, then a clean-cache build on a
fresh machine — before power), **A1.2** (serialport rebuild), **A1.4** (CI additions), **CG2**
(packaged `.exe` proves "CRSF serial open" on the target machine), plus the "confirm both
boards flashed from the SAME pinned version, record it" and "gift ships plain esp32dev"
items (before-power / pre-gift).
