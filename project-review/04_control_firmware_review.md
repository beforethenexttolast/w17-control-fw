# 04 — Control Firmware (ESP32 #1) Review

Source: "Control firmware embedded correctness" dimension in `_raw_audit_findings.json`;
severities/verdicts per `_verification_results.md`. Findings reference `10_risk_register.md`.

## Verdict

The control firmware is **unusually disciplined for a hobby project**: fully non-blocking loop,
clean HAL seam, unsigned millis() deltas throughout, an atomic IRAM Hall ISR, and a genuinely
**layered safety chain traced end-to-end and found to hold** — FailsafeStateMachine forces
neutral → ArmGate re-neutral latch → EscOutput boot-arm hold → multiplicative ERS boost that
cannot bypass the gate. At boot and on link loss the ESC is commanded neutral before any
feature runs. The serious residual concerns are **hardware-gated, not logic bugs**: the LEDC
PWM has never been scope-verified, the ESC arm-hold is a guess against an unread ESC manual,
and the Hall input is untested against real ESC EMI. The pure-logic "defects" found were minor
and one was **refuted on re-check** (the WheelSpeed decay claim). Tests pass, but several test
intended behavior rather than corner cases — the corner cases are what the bench must close.

## What is genuinely well-designed

- **Failsafe cannot be bypassed**: the Safe branch (main.cpp:340-348) unconditionally commands
  steering 0 / throttle 0 / DRS closed with no early-return; ArmGate runs every tick with
  `forceDisarm = (state==Safe)`; `applyBoost(0)==0` so ERS can't resurrect throttle. Matches
  CLAUDE.md §6.1/6.2 exactly.
- **FailsafeStateMachine** latches Safe until `everReceivedFrame_` AND fresh link AND no RX
  failsafe flag, with immediate drop-to-Safe and a 150 ms continuous-validity rearm — the
  `everReceivedFrame_` latch directly fixes the documented boot-full-lock bug (ROADMAP A1).
- **Fully non-blocking control path**: only `now-last>=period` guards; deliberately
  phase-accumulating tick guard (no catch-up burst); clock-injected boot-arm, no `delay()`.
- **Hall ISR concurrency done right**: single-word relaxed atomics (torn-free on a 32-bit MCU),
  IRAM_ATTR (survives flash-cache-off NVS writes), seed-on-first-update to avoid a boot spike.
- **Every output defensively clamps** to [-1000,1000] then to [min,max] µs regardless of trim;
  `ServoConfig::valid()` rejects trim past an endpoint; LEDC `begin()` writes a safe pulse.
- **NVS never-brick guard**: length → CRC → version → `valid()` chain, any failure ⇒ defaults;
  the console is compile-gated so the gift firmware has no UART0 surface.
- **Integer scaling overflow-analyzed** in code: battery `valid()` proves u32/u16 fit; gearbox
  expo bounds x³ ≤ 1e9 in int32; ERS micro-permille integrator needs no division, with a stall
  guard.

## Findings (→ risk register)

| R## | Sev (verified) | Finding | Firmware angle |
|---|---|---|---|
| R09 | Med | ESC boot-arm hold (2000 ms) is a guess vs the QuicRun 10BL120 | The hold only guarantees the ESC *sees* neutral first; arming is governed by the ESC's own firmware — bench-only truth |
| R18 | Low (adjusted Med→Low) | Hall GPIO35 2 ms lockout unverified against real ESC EMI | Telemetry/ERS only — not in the control/safety path; D8 already plans the scope + RC-filter check |
| R21 | Low — **mechanism REFUTED** | WheelSpeed "decay collapses to near-zero one tick before timeout" | Re-check: ceiling is ~40 rpm at the boundary, not near-zero; real behavior is a benign sub-40 rpm resolution floor, telemetry-only |
| R04 | High | (from safety dim) ESC/steering pins float pre-`begin()` — setup() ordering confirmed here | `escPwm.begin()` at main.cpp:181 runs after four other begins |
| (—) | HW-verify | "LEDC 16-bit@50 Hz is at/near the limit" — **largely refuted**: 50 Hz ceiling is ~20.6 bits, 16-bit is comfortable | What survives: *no scope verification exists*; plan item B1.3. Also feeds R02 (the LEDC channel API is the core-2.x-only one) |

**Carried Low (register appendix, not re-verified):**
- FLIGHTMODE telemetry reads `controlSnapshot` fields refreshed only in the 20 Hz link2 block —
  the "authoritative" up-linked gear/mode/ERS can be one cycle (~50 ms) stale. Cosmetic.
- `WheelPulseSnapshot` count/period can tear (documented in the header); one update can compute
  RPM from a stale period after a sudden speed change. Telemetry-only, self-corrects next edge.
- No explicit task-WDT policy; loop is non-blocking so likely fine, but no defense-in-depth if a
  future change spins the UART drain.
- Gimbal servos are driven in all modes and **hold last position on failsafe** (documented
  choice); never re-centered after an outage. Cosmetic/UX.

## Open questions

- Does the QuicRun arm with a plain 2 s neutral hold, or need endpoint calibration/gestures? (→ R09)
- Does LEDC actually deliver 16-bit @ 50 Hz on this board (scope), and is `0xC8` the right
  address byte for device→RX telemetry? (→ R13; feeds proto review)
- Is 2 ms an adequate Hall lockout under motor load, or is an RC/Schmitt front-end needed? (→ R18)
- Is the Arduino loopTask subscribed to the task WDT in this platform version — and is that the
  intended policy?

## Hardware validation hooks

`11_hardware_validation_plan.md`: **B1.3** (scope PWM before connecting actuators), **B1.4**
(boot-float window), **B2.1–B2.4** (the full safety chain — the single most important bench
session), **B4.2/C2** (Hall baseline + EMI at load), **C3** (battery calibration).
