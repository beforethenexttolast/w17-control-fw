# 07 — Communication Protocols Review (CRSF in · link2 · CRSF telemetry backchannel)

Source: "All communication protocols end to end" dimension in `_raw_audit_findings.json`;
severities/verdicts per `_verification_results.md`. Findings reference `10_risk_register.md`.

## Verdict

The wire layer is **unusually disciplined**: both CRSF and link2 use stateful assemblers that
resync on the next start byte after any failure, hard-reject a bad length byte the instant it
arrives (a corrupt 0xFF cannot swallow ~1 s of stream), and validate CRC before trusting
anything. **Byte order matches on every encode/decode pair checked**: CRSF channels LE
bit-packed (pack/unpack are exact inverses), CRSF telemetry payloads BE (verified against the
ground decoders with explicit offset tests), link2 LE on both sides. The CRC is a correct
CRC-8/DVB-S2 pinned by the 0xBC catalog KAT in all three repos. Both sim feeders drive the
**real** parser path, not a shortcut. No acks/retransmit exist — correctly, since both
receivers implement staleness timeouts. The real defects are at the **semantic/agreement
layer**: label and gear-range divergence, an overstated shared-golden-vector claim, a
duplicated link2 with no drift guard, and — the one genuinely open wire question — whether
ELRS actually **relays** the `0xC8`-addressed telemetry frames at all (hardware-gated).

## What is genuinely well-designed

- **Type-agnostic CRSF assembler**: FrameReady = "CRC-valid frame of any type", so interleaved
  LINK_STATISTICS isn't treated as corruption; per-type payload-length checks live in the
  receiver (a CRC-valid RC frame with the wrong length is ignored, tested).
- **Immediate bad-length rejection in both assemblers** — explicitly tested, including link2
  resync when 0xA5 legally appears inside a payload.
- **CRC pinned four ways**: the same DVB-S2 loop in crsf, link2, settings, and JS — all proven
  mutually consistent by the 0xBC KAT; link2's deliberate CRC duplication is cross-checked
  against crsf's in a test.
- **Clean forward-compat semantics in link2**: start → length → CRC → version order means
  `BadVersion` = "well-formed frame from a newer sender"; reserved bit7 is masked, not rejected.
- **Sim feeders exercise production parsing**: SimCrsfFeeder → `buildRcChannelsFrame` → real
  Serial2 loopback → `feedByte`; SimLink2Feeder → real `encodeFrame` → `monitor.feedByte`.
- **Telemetry byte order verified on both sides with concrete offsets** (e.g. GPS groundspeed
  361 = 0x0169 at payload[8..9] agrees end-to-end).
- **Regression guards that matter**: LINK_STATISTICS does not bump the RC-frame timestamp (an
  LQ=0 burst can't extend the failsafe timeout); the receiver owns its channel copy rather than
  pointing into the assembler buffer.
- **Staleness-timeout design on both one-way links** (500 ms) — the correct substitute for acks
  on real-time streams.

## Findings (→ risk register)

| R## | Sev (verified) | Finding | Protocol angle |
|---|---|---|---|
| R07 | Med (ADJUSTED High→Med) | `crsf.js` claims firmware golden vectors are reused; no shared fixture exists | The one cross-repo shared truth is pinned by prose + a shared CRC algorithm, not a byte fixture — though the GS review verified byte-for-byte agreement *today* |
| R13 | Med | `0xC8`-addressed battery/GPS/FLIGHTMODE frames pushed out UART2 — ELRS relay unverified | ELRS receivers are selective about relayed telemetry types/addressing; possibly needs an extended (dest/origin) header. The one open **wire-level** question; hardware-gated |
| R06 | Med | Duplicated `lib/link2` with no CI drift guard | A one-sided payload/flag/CRC-span edit silently desynchronizes the boards; both repos' tests stay green |
| R05 | Med | Gear range 4 (fw) / 6 (link2 doc) / 8 (HUD); FLIGHTMODE `G<n>` unbounded | The number is consistent on the wire; the *promises* about its range are not |
| R19 | Low | driveMode labels: fw/doc "Gearbox/Gearbox+ERS" vs HUD "RACE/ERS" | Same 0/1/2 on the wire; contradicting human-facing names |

**Carried Low (register appendix):** battery mV integer-divided by 100 to decivolts with no
rounding (max ~0.09 V display loss on a monitoring-only value).

## Open questions

- Does RP1/ELRS relay `0xC8`-addressed sensor frames, or is an extended header/device address
  (e.g. `0xEC`) required? (→ R13 — the key bench question)
- If a handset displays the raw FLIGHTMODE string, the label divergence surfaces there too —
  acceptable? (→ R19)
- Is a link2 v2 field planned for reserved bit7? Old receivers would report BadVersion and hold
  last-known state — intended degrade for mixed-firmware bring-up?
- If FLIGHTMODE is ever truncated mid-token at `kFlightModeMaxLen`, does `parseFlightMode`'s
  per-field fallback behave gracefully? (Only the clean-ASCII path is tested.)

## Hardware validation hooks

`11_hardware_validation_plan.md`: **CG3** (each frame type actually relayed — the R13 gate),
**B3.3/B3.4** (link2 on the wire), **B1.1/B1.2** (CRSF RX at 420000 non-inverted + LQ=0 latch),
**C4** (telemetry cadence doesn't stall the control tick), **CG6** (live gear/label sweep).
