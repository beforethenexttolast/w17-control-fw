# 05 — Sound/Light Firmware (ESP32 #2) Review

Source: "Sound/light firmware embedded correctness" dimension in `_raw_audit_findings.json`;
severities/verdicts per `_verification_results.md`. Findings reference `10_risk_register.md`.

## Verdict

The cross-core design is **genuinely sound**: the only shared mutable surface between the
core-1 control loop and the core-0 audio task is one `std::atomic<uint32_t>` packed-param word
plus one atomic heartbeat — no torn reads, no shared VehicleState, no locks. The audio dead-man
(heartbeat stale >500 ms → forced-silent params, ramped down by the smoother) is implemented
and correct **including at boot**. Link2Monitor implements the mandated 500 ms staleness →
local failsafe with a thoughtful per-field projection, and the assembler hard-rejects bad
length bytes the instant they arrive. The audit's headline worry — that enginesim would treat
WHEEL rpm as engine rpm and sound wrong — is **not borne out**: engine rpm derives purely from
throttlePercent; the decoded wheel-rpm field is simply dead on this board. Remaining defects
are all low-severity diagnostics/robustness items. This board has **no motion-safety role**;
its failure modes are sound/light UX and bring-up friction.

## What is genuinely well-designed

- **Cross-core contract is exactly as CLAUDE.md claims, and airtight**: core 1 only `.store()`s
  the two atomics, core 0 only `.load()`s them and exclusively owns the synth. A 32-bit atomic
  is torn-free on ESP32 — a correct lock-free hand-off.
- **Audio dead-man is real and boot-safe**: params stale >500 ms → `setParams(0,…)`; at boot
  heartbeat=0 so the first 500 ms renders silence — a wedged or never-started control loop
  cannot leave the engine screaming.
- **Mandatory 500 ms link staleness → local failsafe** (Link2Monitor.cpp:31-54) with per-field
  projection: commands/rpm forced safe, latched judgments (lowBattery, gear…) held — gear held
  deliberately to avoid a phantom shift-blip on recovery. Boot = NeverConnected + failsafe=true.
- **Length byte hard-rejected immediately** (State::ReadingLength resyncs on a non-11 length) —
  a corrupt 0xFF length cannot swallow ~1 s of frames, exactly per the protocol doc.
- **enginesim does NOT confuse wheel and engine rpm**: `targetRpm` maps throttle 0..100 to
  idle..max; `state.rpm` is never read by enginesim (grep-confirmed).
- **Clip safety is layered**: compile-time `peakSum() ≤ 30000` static_assert + a final int16
  hard clamp + tests proving no clipping across full drive scripts.
- **Lights failsafe precedence correct and tested**: `failsafe || link==Lost` short-circuits to
  all-amber hazard before any functional layer; WS2812 power budget enforced at compile time
  (maxBrightness=255 is rejected by test).
- **Per-sample param smoothing** (one-pole, >>6) kills zipper on 50 Hz steps; milli-Hz phase
  accumulator keeps pitch precise at low rpm; deterministic seeded noise makes render()
  reproducible under test.

## Findings (→ risk register)

| R## | Sev (verified) | Finding | Board-#2 angle |
|---|---|---|---|
| R22 | Low (dimension said Med; register carries the safety-review's Low) | Dead board #1 at power-on → calm "breathe" forever, no fault escalation | Intentional per the code comment; benign because a dead board #1 = non-moving car + silent engine; diagnostics/UX gap only |
| R11 | Med | (from sim/test dim) no Wokwi sim; I2S/NeoPixel HALs unproven | This dimension's I2S/show() items are the concrete instances — see below |

**Carried Low (register appendix, not re-verified):**
- **Production `volumeFor` is untested** — the integration test defines its *own* differing
  formula (60/80+175% vs shipped 70/90+165%); a typo in the shipped curve passes all 40 tests.
- **Overrun crackle burst (noiseAmpMax×3) is not in the compile-time headroom sum** — survives
  today only via the runtime clamp; a future retune could pass `valid()` yet clip.
- **Decoded wheel-rpm field is dead on board #2** — board #1 spends link2 bytes on rpm nobody
  consumes; engine note is throttle-derived (defensible, but undocumented dead weight).
- **NeoPixel `show()` vs link2 UART RX on core 1**: ~1 ms interrupt-disable window vs the
  128-byte UART FIFO at 115200 — should be fine, timing-unverified (likely moot if the RMT
  backend is in use; see open question).
- **`i2s.begin()` return codes ignored** — a failed driver install leaves the audio task blocked
  in `i2s_write(portMAX_DELAY)` forever with no diagnostic: no sound, no error, harder bring-up.

## Open questions

- Does Adafruit_NeoPixel on this core use the **RMT backend** (no interrupt disable — concern
  moot) or bit-bang? Header claims RMT; confirm on hardware.
- ~~Does espressif32 7.0.1 actually ship the legacy `driver/i2s.h`?~~ **RESOLVED** during
  verification: 7.0.1 → core 2.0.17 / IDF 4.4 — the legacy driver is present; the pin and the
  HAL comment are self-consistent (see `open_questions.md`).
- Is 22050 Hz / 256-frame / 6-DMA-buffer (~70 ms) + priority-5 audio task enough to avoid
  underrun under real load? Unmeasured.

## Hardware validation hooks

`11_hardware_validation_plan.md`: **B3.5** (I2S audio + init return codes), **B3.6** (show()
vs audio DMA glitch), **B3.7** (never-connected breathe + cut-wire→hazard within 500 ms),
**B3.8** (mid-frame power-on ordering), **CR2** (sustained max-rpm render, no underrun/clip).
