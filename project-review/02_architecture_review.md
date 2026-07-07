# 02 — Repository-Level Architecture Review

Source: "Repository-level architecture" dimension in `_raw_audit_findings.json`; severities per
`_verification_results.md`. Findings reference `10_risk_register.md` (`R##`).

## Verdict

The three-repo split is **coherent**: control-fw owns CRSF-in / actuators / link2-out;
soundlight-fw is a link2 receiver; the ground station is a read-only HUD. The HAL-seam
architecture is **genuinely clean** — the investigator grepped every pure-logic library in both
firmwares and found **zero** Arduino/ESP/driver/FreeRTOS includes (only comment-text matches),
so the native test builds are legitimately hardware-free. The serious weakness is **drift**:
three copies of the same wire truth (link2 codec, CRSF byte layout, ERS/gear numbers) are
maintained by hand across three independent git remotes with **no CI check, no submodule, no
shared package**. The link2 copies are byte-identical today only by discipline of copy-paste;
the CRSF C++ and JS are parallel reimplementations *not* tested against literally-shared golden
vectors (a `crsf.js` comment overstates the coupling). None of this is hardware-gated — all of
it is statically verifiable and fixable now.

## What is genuinely well-designed

- **Leak-free HAL seam**: pure logic in both firmwares includes only `<cstdint>`-class headers
  and HAL interfaces; hardware code lives solely in `*_hal_esp32` libs referenced from main.cpp.
- **Defensive native env**: `test_build_src = no` + explicit `lib_ignore` of every `*_hal_esp32`
  lib + HAL `library.json` declaring `platforms: espressif32` — three independent guards against
  hardware code entering host tests.
- **link2 cleanly split**: pure codec (frameworks/platforms `*`) vs HAL-touching `Link2Sender`
  (control-fw only) — which is exactly why the copy into soundlight-fw was mechanically possible
  and stayed byte-identical.
- **feelConstants.js drift guard is numerically correct**: JS 26/11/1.18 matches firmware
  permille 260/110/180, and `replay.test.js` hard-asserts the JS numbers.
- **Dependency direction respected throughout**: logic → interface, impl → interface; main.cpp
  holds the only concrete impls.
- **CRSF battery layout is human-cross-checked**: firmware and ground tests use the identical
  7.9 V = 0x004F / 72% vector with comments pointing at each other — not machine-enforced, but a
  deliberate breadcrumb.

## Findings (→ risk register)

| R## | Sev (verified) | Finding | Architecture angle |
|---|---|---|---|
| R06 | Med | No mechanism prevents `lib/link2` diverging between the two firmware repos | The ONLY runtime contract between the boards is hand-synced across independent remotes; identical today (lead-verified diff), guarded by nothing |
| R07 | Med (ADJUSTED High→Med) | CRSF decoder reimplemented C++↔JS with no shared fixture | Verification confirmed: `crsf.test.js` builds frames with its own local helper; only the CRC catalog value 0xBC is truly shared; the `crsf.js` "golden vectors are reused" comment overstates it |
| R05 / R19 | Med / Low | Gear counts and drive-mode labels inconsistent, no shared enum | Same wire numbers, three human-facing truths — the cost of triple-maintained constants |
| R02 | High | control-fw platform unpinned vs soundlight `~7.0.1` | Cross-firmware toolchain skew is *latent* (both currently resolve to 7.0.1/core 2.0.17 — lead-verified) but unguarded |

**Carried Low (appendix of the register):**
- soundlight `lib/link2/library.json` description is a stale verbatim copy (describes the
  *sender* role in a receiver-only repo) — direct evidence of copy-paste, misleading to readers.
- `feelConstants.js`/`replay.test.js` cite a firmware source path as truth but the guard
  hard-codes numbers; a firmware-side change keeps the JS test green.

## Open questions

- Is the link2 copy-paste permanent (two evolving copies) or a bootstrap toward a submodule? (→ R06)
- Which gear count / label set is authoritative? (→ R05, R19 — owner decision)
- Should the ground station's backchannel `decodeFrame` share the RC parser's sync-byte naming
  despite an intentionally different contract, or be renamed to avoid confusion?

## Hardware validation hooks

See `11_hardware_validation_plan.md`: B3.3/B3.4 (live link2 wire agreement — the real check that
the copied trees haven't drifted), CG4 (JS decoder vs real firmware frames — validates the
parallel reimplementation), CG6 (live gear/label agreement).
