# 06 — Ground Station (Electron App) Review

Source: "Ground station" dimension in `_raw_audit_findings.json`; severities/verdicts per
`_verification_results.md`. Findings reference `10_risk_register.md`.

## Verdict

The app is **genuinely viewer-only** — verified: no code path opens the control port for
writing, the preload exposes only `getConfig` + `onTelemetry`, and the renderer runs with
contextIsolation/sandbox and no nodeIntegration. The CRSF decode is a faithful
constant-for-constant port, and the investigator **verified byte-for-byte** that the JS
decoders for BATTERY/GPS/FLIGHTMODE/LINK_STATISTICS match what the firmware actually emits
(this tempers R07: agreement is human-verified today, just not machine-enforced). The real
problems are **behavioral/UX, not wire-format**: the HUD's most safety-relevant indicator
("LINK LOST") keys off a `failsafe` field the real telemetry path never populates — only the
demo can show it; label/count mismatches persist (RACE vs Gearbox, 8 vs 4 gears); a fresh
launch shows a fully-animated simulated HUD distinguishable only by a small "· sim" tag; and as
a bring-up diagnostic the app is thin (no raw-frame/CRC-error/frames-per-second readout —
failures are near-silent). Separately, the packaged `.exe` path is broken by the missing
serialport rebuild (R03, from the build dimension).

## What is genuinely well-designed

- **Viewer-only, verified**: preload exposes read-only channels; `contextIsolation:true`,
  `nodeIntegration:false`, `sandbox:true`; gate text + code comments state it cannot command
  the car. A ground-station bug can never stop the vehicle.
- **Faithful CRSF port**: SYNC 0xC8, CRC-8/DVB-S2 0xD5, payload lengths, CRC-over-[type+payload]
  — all identical to the firmware; GPS groundspeed at [8..9]/10, battery BE decivolts, tolerant
  G/M/E regex matching the firmware's exact `snprintf`.
- **Merge-not-replace telemetry snapshot** in CrsfSerialSource (Object.assign accumulator) — a
  battery frame doesn't blank speed/gear arriving in other frame types; the comment shows the
  hazard was understood.
- **Graceful native-module degrade**: serialport is optional + lazy-required in try/catch → the
  app falls back to a gamepad-only HUD instead of crashing; asarUnpack configured for the
  ABI-native load.
- **Robust process lifecycle**: mediamtx supervisor restarts on crash with a stopping-guard and
  clean teardown; WHEP client auto-retries with a single-timer guard and low-latency hint.
- **Real environment hazards solved**: run.js strips `ELECTRON_RUN_AS_NODE` (VS Code terminal
  trap); ensure-electron.js repairs a script-gated install — both documented with the why.
- **Honest docs**: SETUP.md makes H.265-vs-H.264 the #1 bench item with a VLC fallback;
  TELEMETRY.md correctly explains COM-port exclusivity; CODESIGNING.md is accurate about
  self-signed trust.

## Findings (→ risk register)

| R## | Sev (verified) | Finding | Ground-station angle |
|---|---|---|---|
| R01 | High | "LINK LOST" can never fire from the real telemetry source | `telem.failsafe` is set only by the demo replay source; a real car failsafe or ELRS drop leaves the HUD looking live/green. Merges the README overstatement ("…and failsafe") |
| R05 | Med | HUD `FEEL.gears=8` vs firmware 4 | The gamepad "mirror" doesn't mirror the car's gearing; live G≤4 renders mid-ring on an 8-gear scale |
| R19 | Low | driveMode 1 shown as "RACE" where firmware means "Gearbox" (ERS off) | Numbers agree; labels contradict across HUD vs protocol doc |
| R15 | Med | mediamtx source is a placeholder IP; H.265/H.264 gate unverified | The entire video half hinges on an unverified camera fact; VLC fallback documented |
| R03 | High | (from build dim) packaged `.exe` never rebuilds serialport for Electron's ABI | The shipped app would silently run telemetry-disabled — the graceful degrade masks the packaging bug |

**Carried Low (register appendix, not re-verified):**
- **Default launch is a fully-simulated HUD** mistakable for real car data (only "km/h · sim" +
  "Telemetry: sim" distinguish it) — for a bring-up tool the default shouldn't look like a
  working car. (Related to / merged under R01's expectations gap.)
- **Stale merged fields are never pruned**: if one frame type wedges (e.g. GPS stops, battery
  continues), the last speedKmh persists and re-emits as live; only *full-source* staleness is
  caught by the renderer's 1 s fallback.
- **Non-Windows port default (`/dev/ttyUSB0`) and the reader's 420000 baud** are unproven
  assumptions; wrong values fail gracefully with a retry log.
- **No bring-up diagnostics**: CRC-bad frames are silently dropped, nothing counts frames/sec or
  decode errors — during real bring-up the operator sees only "battery didn't light up".

## Open questions

- Should the ground derive link-loss from `LINK_STATISTICS uplinkLinkQuality==0` + staleness
  instead of a `failsafe` field the firmware never sends? (→ R01 — owner decision)
- Does elrs-joystick-control expose a telemetry forward, or is com0com required? (→ R14)
- Is the OpenIPC camera H.264-capable? (→ R15)
- Which gear count is authoritative? (→ R05)

## Hardware validation hooks

`11_hardware_validation_plan.md`: **A1.2** (serialport rebuild — before power, software),
**CG1** (camera codec + WHEP actually renders), **CG2** (COM reader path), **CG3** (frame types
actually relayed), **CG4** (JS decode vs real frames), **CG5** (deliberate link drop — observe
and decide HUD behavior), **CG6** (live gear/label agreement).
