# iPhone → Windows → pan/tilt: firmware readiness review

**Status: readiness review only. No firmware was changed; nothing here is implemented.**
Date: 2026-07-08. Claims tagged [C] confirmed (file cited) / [I] inferred / [A] assumption.

Phase rules this review assumes: the iPhone talks **only to Windows**; Windows is the
authority and initially only **logs** head-tracking; the firmware stays **unaware of the
iPhone** and consumes only final, already-arbitrated control channels over the existing
CRSF radio link.

---

## 1. Readiness status: **GREEN for this phase — YELLOW before active pan/tilt**

**Green** because the firmware *already is* the target architecture. Camera pan/tilt is
implemented, bench-tested (ROADMAP item 7, DONE 2026-07-03 [C: `docs/ROADMAP.md:240`]),
and driven purely by CRSF channels 9/10 — the firmware has no idea (and no way to know)
whether those channels came from a right stick, a keyboard, or a future validated
head-tracking mapper inside Windows. **Zero firmware changes are required for the
log-only bridge phase**, and none are required for Windows to *eventually* source ch9/10
— the arbitration point is entirely on the PC side (elrs-joystick-control's channel
output), exactly where the architecture wants it.

**Yellow** before *active* head-tracked pan/tilt, for three reasons detailed in §5:
(1) the gimbal servo endpoints are the full-throw defaults (500–2500 µs), never reconciled
against the physical mount's mechanical limits; (2) there is no smoothing/rate limiting
anywhere in the firmware path, acceptable for a human thumb but untested against a
head-tracking-shaped signal; (3) failsafe behavior for the camera is hold-last-position —
fine today, but worth an explicit re-decision once the signal source changes.

---

## 2. Current firmware pan/tilt architecture

### 2.1 Files / classes / functions [C]

| Piece | Where |
|---|---|
| Channel map (ch9/ch10 → pan/tilt, 0-based indices 8/9) | `lib/channels/include/channels/ChannelDecoder.hpp:20-21` (`panIndex = 8`, `tiltIndex = 9`) |
| Bench sign flips | `ChannelMapConfig::invertPan` / `invertTilt` (`ChannelDecoder.hpp:30-31`) |
| Decode → normalized | `channels::ChannelDecoder::decode()` → `Controls::pan` / `Controls::tilt`, int16 −1000…+1000 (`ChannelDecoder.hpp:55-59`) |
| Servo scaling | `outputs::ServoOutput::setPosition()` (`lib/outputs/src/ServoOutput.cpp:7-31`) — linear, split-slope around trimmed center, double-clamped |
| Servo config | `outputs::ServoConfig` (`lib/outputs/include/outputs/ServoOutput.hpp:9-23`) with `valid()` guard |
| Pins / PWM | `pinmap::kGimbalPanPin = 19`, `kGimbalTiltPin = 23` (`lib/config/include/config/PinMap.hpp:28-31`), LEDC channels 3/4, 50 Hz (`src/main.cpp:91-92`) |
| Wiring + actuation | `src/main.cpp:97-98` (instances), `176-177`/`182-183` (boot), `370-376` (50 Hz tick) |
| Tests | `test/test_channels/test_main.cpp:177-202` — absent-channel decode-to-neutral, full-throw decode, inversion |
| Docs | `docs/ROADMAP.md:240-242` (item 7), `docs/D8_BENCH_BRINGUP.md` Phase 7b (bench procedure incl. invert flags) |

### 2.2 Answers to the checklist (A)

1. **Pan/tilt supported?** Yes — implemented, wired, and marked bench-tested. Not
   "optional/deferred" anymore (the CLAUDE.md §1 table is stale on this point). [C]
2. **Input channels:** `controls.pan` / `controls.tilt` from the `ChannelDecoder`,
   fed by the standard CRSF `RC_CHANNELS_PACKED` (0x16) frame. [C]
3. **CRSF ch9/ch10?** Yes — 0-based indices 8/9 = ch9/ch10; the main.cpp comment
   confirms "right stick → ch9/ch10 in elrs-joystick-control" (`main.cpp:370`). [C]
4. **GPIO/servo outputs:** pan GPIO19 (LEDC ch3), tilt GPIO23 (LEDC ch4), MG90S servos,
   50 Hz servo PWM. [C]
5. **Neutral/min/max:** normalized input −1000/0/+1000 (CRSF raw anchors 172/992/1811,
   exact at the anchors); servo output uses the *default* `ServoConfig`: 500 µs min /
   1500 µs center / 2500 µs max, trim 0 (`main.cpp:86` `gimbalConfig{}`). [C]
6. **Limits enforced?** Yes, twice: input clamped to ±1000, then a defensive µs clamp to
   `[minMicros, maxMicros]` regardless of trim (`ServoOutput.cpp:8-28`). **But** the
   enforced envelope is the full 500–2500 µs throw — there are no gimbal-specific
   mechanical soft limits (§5.1). [C]
7. **Smoothing/rate limiting?** None. `setPosition()` is a memoryless linear map,
   re-commanded every 20 ms control tick. Step input = step output (slew-limited only by
   the servo's own mechanics). [C]
8. **Input lost/stale:** during failsafe, decode of new frames stops arriving (no frames)
   or actuation of drive outputs is forced safe — but pan/tilt actuation is **deliberately
   not safety-gated**: `controls` freezes at its last decoded value and the camera
   **holds its last position** rather than snapping to center. This is an explicit,
   commented decision: "aiming the camera is harmless armed or disarmed"
   (`main.cpp:370-376`). Note the asymmetric case: if the RX itself sends hold-position
   frames during an outage (LQ==0 latched → drive outputs Safe), decode continues and the
   camera keeps following whatever the RX repeats. [C]
9. **Startup before valid input:** LEDC attaches with an explicit center pulse
   (`panPwm.begin(gimbalConfig.centerMicros)`), then `setPosition(0)` — camera centered.
   `controls` defaults to all-zero until the first decoded frame, so the camera stays
   centered until real input arrives. The failsafe machine boots in `Safe` and can never
   report Active before a real frame (`FailsafeStateMachine.hpp:34-39`), but pan/tilt
   doesn't depend on that — center-until-first-frame comes from the zero defaults. [C]

### 2.3 Answers to the checklist (B): authority and failsafe

1. **Final command authority today** sits on the PC: elrs-joystick-control converts the
   DualShock into CRSF channels and owns the FT232→ELRS TX path. The firmware is a
   consumer of whatever arrives on UART2 from the RP1. [C: architecture docs; firmware
   reads only `CrsfReceiver::channels()`]
2. **Does firmware assume direct CRSF input?** Yes — `RC_CHANNELS_PACKED` on UART2 at
   420 k is its only control input. It is source-agnostic beyond that: nothing in the
   decode path knows or cares what generated the channel values. This is precisely the
   "firmware consumes final arbitrated channels" property the target architecture needs —
   **already true, by construction**. [C]
3. **Manual vs auxiliary distinction:** yes, by gating policy per output —
   throttle: failsafe-gated AND arm-gated (`ArmGate`, neutral-seen latch, re-latch after
   every failsafe episode, `main.cpp:320-331`); steering: failsafe-gated (center on Safe)
   but live while disarmed; DRS: failsafe-gated (closed on Safe); **pan/tilt: not gated
   at all** (`main.cpp:343-376`). [C]
4. **Failsafe timeout:** 500 ms no-valid-frame timeout OR the latched RX failsafe flag
   (uplink LQ==0 from LINK_STATISTICS); drop to Safe is immediate, recovery requires
   150 ms of continuously good link (`FailsafeStateMachine.hpp:9-19`). [C]
5. **Safe state for pan/tilt on failsafe:** hold last commanded position. [C]
6. **Disabled / held / centered?** Held (PWM keeps pulsing the last µs; servo stays
   powered and position-holding). Not disabled, not centered. [C]
7. **Documented?** Yes: the code comment (`main.cpp:370-374`), ROADMAP item 7
   ("Not safety-gated (aiming a camera is harmless…)", `ROADMAP.md:240-242`), and the
   D8 bench procedure Phase 7b. [C]

---

## 3. Integration contract proposal (C) — for the *later*, separately-approved milestone

The one-sentence contract: **Windows changes; the firmware and the radio protocol do
not.** Head-tracking intent, once validated and arbitrated inside Windows, becomes
ordinary ch9/ch10 values in the same CRSF `RC_CHANNELS_PACKED` stream
elrs-joystick-control already transmits. The firmware cannot tell the difference — that
is the design goal, and it already holds.

1. **What Windows sends:** CRSF 11-bit raw channel values on ch9 (pan) and ch10 (tilt),
   range **172…1811, neutral 992** — inside the frames it already sends continuously at
   the ELRS packet rate. [C: `ChannelDecoder` anchors]
2. **Channel numbers:** ch9 = pan, ch10 = tilt (indices 8/9). Any remap happens in
   `ChannelMapConfig` only, at the bench. [C]
3. **Neutral:** 992 raw (→ normalized 0 → trimmed-center µs → camera straight ahead). [C]
4. **Safe min/max:** the full 172–1811 raw range is *protocol-safe* (firmware clamps at
   both the normalized and µs layers), but the *mechanically* safe range is unverified —
   the gimbal uses full-throw 500–2500 µs endpoints (§5.1). Until bench-verified, Windows
   should conservatively cap its mapping well inside full throw (e.g. ±50–60 % of range),
   and the firmware should later get real per-axis endpoints. [I]
5. **Angles, normalized, or CRSF values?** On the wire: CRSF channel values — it's the
   only control input the firmware has, and inventing a second input path would violate
   the architecture. Internally on the PC, the bridge chain should be: iPhone sends
   normalized intent (per the ground-station readiness doc) → Windows validates/smooths →
   Windows converts to CRSF raw at the last step. Angles should not cross any wire:
   degrees-per-count depends on servo + mount geometry, which only the bench knows. [I]
6. **Servo calibration ownership: firmware.** Endpoints, center, trim per physical servo
   live in `ServoConfig` next to the hardware they describe (and `valid()` guards them).
   Windows must never compensate for servo geometry. [I — follows the existing repo
   pattern: steering calibration already lives firmware-side, tunable via the bench console]
7. **Sign flips: firmware** owns physical direction via the existing `invertPan` /
   `invertTilt` bench flags — "positive pan = camera looks right, positive tilt = looks
   up" should be made true at the bench once, there. Windows/iPhone then treat the
   convention as fixed. Two layers both flipping signs is how direction bugs are born;
   one owner, at the layer closest to the mechanics. [I]
8. **Deadband/smoothing/rate limiting: Windows** (the intent authority) owns shaping the
   raw head-tracking signal — deadband around center, low-pass smoothing, and a max
   rate-of-change — because only Windows knows the signal's provenance and quality. The
   firmware *may later* add a modest slew limiter on the gimbal outputs as
   defense-in-depth (§4 item 2 below), but it must be a backstop, not the primary shaper. [I]
9. **Stale packet behavior: split by layer.** Windows owns iPhone-staleness: intent older
   than the bridge's stale threshold (400 ms per the ground-station readiness doc) decays
   the *commanded channels back to center* — the radio link stays alive, so the firmware
   just follows the decay. The firmware owns radio-staleness exactly as today: link loss
   ≥ 500 ms → drive outputs Safe, camera holds last position. These compose cleanly:
   iPhone dies → camera re-centers via Windows; radio dies → camera freezes via firmware. [I]
10. **Safest split, summarized:**

| Concern | Owner |
|---|---|
| intent validation, staleness, smoothing, rate limit, arbitration, manual-override | Windows |
| wire format (CRSF ch9/10, 172/992/1811) | fixed protocol — neither side reinterprets |
| servo endpoints, center, trim, direction | firmware |
| radio-loss behavior | firmware (existing failsafe) |
| update rate | ELRS packet rate (continuous); Windows updates its ch9/10 values at its own cadence — no new timing contract needed |

---

## 4. Required firmware changes before *active* pan/tilt (identified only — NOT implemented)

Numbered per the task; "exists" = already in the codebase today.

1. **Explicit pan/tilt enable flag** — does not exist; pan/tilt always follows ch9/10.
   Decide whether firmware needs one at all: the architecture puts enable/disable in
   Windows (it simply holds ch9/10 at 992 when head-tracking is off). A firmware-side
   compile-time or config flag would be belt-and-braces for the bench. [Gap — decision]
2. **Servo output limits** — clamps exist [C], but per-axis *mechanical* endpoints for
   the real gimbal mount do not (defaults are full-throw 500–2500 µs). Needs a bench
   session + real `ServoConfig` values for `gimbalConfig`. [Gap — hardware-gated]
3. **Failsafe behavior** — exists and documented (hold-last). Needs an explicit
   re-decision for the head-tracked era: hold vs return-to-center on radio loss (a
   centered camera may be the better FPV recovery view). Document the decision either way. [Decision]
4. **Neutral return behavior** — center-on-boot exists [C]. Center-on-stale-intent is a
   Windows job (§3.9). Firmware-side neutral return only enters if item 3 chooses it. [Mostly exists]
5. **Manual override channel** — not a firmware concept: ch9/10 *are* the final
   arbitrated values, so override (right-stick beats head-tracking) is Windows-side
   arbitration. Firmware needs nothing. Confirm this explicitly in the bridge design doc. [Not a firmware gap]
6. **Debug telemetry** — no pan/tilt in any telemetry today: not in the link2
   `ControlSnapshot`, not in the CRSF uplink frames [C: `main.cpp:343-390`]. If the HUD
   should show real camera aim (it currently mirrors the right stick locally,
   `renderer/hud.js` camDot), the FLIGHTMODE status string or a spare field would need
   extending — protocol change, both repos + golden vectors. [Gap — optional]
7. **Logging/diagnostics** — the firmware has no runtime logging by design (no UART0 in
   the delivered build); the tuning-console build could gain a pan/tilt readout line.
   Primary diagnostics belong in the Windows bridge logs. [Minor]
8. **Startup state** — exists and safe (center pulse at LEDC attach, zero controls). [Exists]
9. **Configurable servo endpoints** — the mechanism exists (`ServoConfig` +
   `ServoOutput::setConfig`), but `gimbalConfig` is not wired into the tuning console
   (`applyTuning()` pushes steering/gearbox/battery only, `main.cpp:158-162`) and not in
   `Settings`. Add gimbal to the console for bench endpoint-finding. [Gap — small]
10. **Unit/simulation tests** — decode + inversion + absent-channel tests exist
    (`test/test_channels`), and `ServoOutput` scaling is tested generically
    (`test/test_outputs`). Missing: gimbal-specific endpoint/clamp cases once real
    endpoints exist, a slew-limiter test if item 2/8 adds one, and a Wokwi Stage-2
    scenario driving ch9/10 through a scripted sweep. [Partial]

---

## 5. Safety gaps (the YELLOW items)

1. **Unverified mechanical envelope.** Full-throw 500–2500 µs into a 3D-printed gimbal
   mount can stall an MG90S against a hard stop (continuous stall current, heat, stripped
   gears). Today a human thumb rarely parks the stick at full deflection; a head-tracker
   *routinely* saturates. Real endpoints must be measured before any head-tracked drive. [I]
2. **No rate limiting anywhere in the chain yet.** Head-tracking is jittery in a way
   sticks are not; unshaped, it means constant servo hunting (noise, wear, current spikes
   on the shared BEC rail). Windows owns primary shaping (§3.8), but that code doesn't
   exist yet — until it does, nothing prevents a raw mapping from being "tried out."
   This is exactly why the do-not-do-yet list below exists. [I]
3. **Hold-last-position on radio loss** was decided when pan/tilt meant "where the driver
   last aimed"; with head-tracking it means "wherever the head happened to be at the
   moment of link loss" — potentially fully deflected. Revisit (item 4.3) before the
   active milestone. [I]
4. **Stale CLAUDE.md pin table** marks the gimbal "optional/deferred" while the code has
   it wired and bench-tested — a doc drift that could mislead a future session into
   treating pan/tilt as unbuilt. Documentation-only fix. [C]

None of these affect the *current* phase: with Windows log-only and no iPhone mapping,
the firmware's pan/tilt continues to serve the right stick exactly as bench-tested.

---

## 6. Recommended next firmware commits (in order — none of them now)

1. **Documentation-only:** (a) fix the CLAUDE.md §1 gimbal rows (wired, not deferred);
   (b) add a short `docs/` note or ROADMAP entry recording the §3 contract — "ch9/10 are
   the final arbitrated pan/tilt; any future head-tracking arbitration is PC-side;
   firmware stays source-agnostic" — plus the open failsafe hold-vs-center decision.
2. **Tests/simulation next:** gimbal endpoint/clamp unit tests against real (bench-measured)
   `ServoConfig` values; a Wokwi ch9/10 sweep scenario; if adopted, a firmware slew-limiter
   as a pure, test-first module.
3. **Implementation only later, after the Windows log-only bridge is validated and the
   safety milestone is approved:** wire `gimbalConfig` into the tuning console, apply real
   endpoints, optionally the slew limiter and the failsafe-centering decision. Still no
   iPhone awareness in firmware — ever, per the architecture.

---

## 7. Explicit do-not-do-yet list

- **No iPhone-to-servo active mapping** — head-tracking intent terminates at the Windows
  log in this phase; it must not reach elrs-joystick-control's channel output.
- **No CRSF ch9/10 activation from iPhone intent** — ch9/10 remain exclusively the
  right stick until the separate safety milestone approves the mapper.
- **No real servo movement from head-tracking until** (a) the real iPhone bench test and
  (b) the Windows log-only bridge are both validated, and (c) the gimbal's mechanical
  endpoints are measured and configured. Until then, head-tracked pan/tilt exists only on
  paper (this document and the ground-station readiness report).
- No firmware changes of any kind for the log-only bridge phase — none are needed.

---

## 8. Active pan/tilt safety milestone blockers

Every item below must be cleared **before any servo movement is driven from head
tracking**. Until all are green, head-tracked pan/tilt exists on paper only; the gimbal
continues to serve the right stick exactly as bench-tested.

1. **Physical servo endpoint validation.** Measure the real mechanical limits of the
   assembled 3D-printed gimbal mount and set per-axis `gimbalConfig` endpoints inside the
   full-throw 500–2500 µs default so a saturated head-tracking signal cannot stall an
   MG90S against a hard stop. (§5.1) — hardware/bench-gated.
2. **Smoothing / rate-limiting ownership confirmed and built.** Windows owns primary
   shaping of the raw head-tracking signal (deadband, low-pass, max slew — §3.8); an
   optional firmware slew limiter may be added as defense-in-depth only. Neither exists
   yet; a raw unshaped mapping must not be trialed on real servos. (§5.2)
3. **Stale-decay-to-center policy.** On stale iPhone intent (≥400 ms per the ground-station
   readiness doc) Windows decays commanded ch9/ch10 back to center 992; the firmware keeps
   its existing radio-loss failsafe. This split must be implemented and verified on the PC
   side before active use. (§3.9)
4. **Manual override.** Right-stick input must beat head-tracking via Windows-side
   arbitration (ch9/ch10 are the final arbitrated values; the firmware has no override
   concept and needs none). Confirm the override rule in the bridge design. (§4.5)
5. **Windows log-only bridge validated.** The log-only receiver (no servo mapping) must be
   built and validated end-to-end first, per `w17-ground-station/docs/iphone_bridge_readiness.md`.
6. **Real iPhone axis / mount validation.** Confirm iPhone head-pose axis conventions,
   sign, and range against the physical camera mount at the bench (which axis is pan vs
   tilt, which direction is positive) — sign ownership stays firmware-side via
   `invertPan`/`invertTilt` (§3.7), but the mapping must be proven against the real rig.
7. **Bench-only servo sweep before any driving use.** A scripted ch9/ch10 sweep (Wokwi
   Stage-2 and then real servos on the bench, car elevated / wheels off ground) must
   exercise the full commanded range and confirm no stall, no rail brown-out, and correct
   direction — before head-tracked pan/tilt is used while driving.

Re-decision to record alongside these: **failsafe hold-last vs return-to-center** for the
camera on radio loss (§4.3) — hold-last was chosen for the stick era; a centered recovery
view may be preferable once the source is a head pose.

---

*Sources: `src/main.cpp`, `lib/channels/include/channels/ChannelDecoder.hpp`,
`lib/outputs/include/outputs/ServoOutput.hpp`, `lib/outputs/src/ServoOutput.cpp`,
`lib/config/include/config/PinMap.hpp`, `lib/failsafe/include/failsafe/FailsafeStateMachine.hpp`,
`test/test_channels/test_main.cpp`, `docs/ROADMAP.md`, `docs/D8_BENCH_BRINGUP.md`, `CLAUDE.md`,
and `w17-ground-station/docs/iphone_bridge_readiness.md` (companion report).*
