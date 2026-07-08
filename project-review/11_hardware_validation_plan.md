# 11 — Hardware Validation Plan

Ordered bench runbook derived from the audit's hardware-validation items (safety + build + 8
dimension findings, deduped) and the `10_risk_register.md` `R##` entries. This **complements**
the firmware's own `docs/D8_BENCH_BRINGUP.md` (the 11-phase runbook) — it does not replace it;
D8 phase numbers are referenced where they apply.

**Golden rule (from D8):** wheels off the ground and **ESC motor power disconnected** until the
failsafe + arm chain is proven live on the bench (Phase A→B). Do not power the ESC until Phase B
passes.

Legend: **R##** = risk-register link · items are grouped by `before_power` / `first_power` /
`later`.

---

## Phase A — BEFORE powering anything (desk + continuity)

Mostly software/config + a multimeter. Clear these first; several are one-line software fixes.

### A1. Software/config to settle at the desk (no hardware)
| # | Do | R## |
|---|---|---|
| A1.1 | **Pin `platform = espressif32 @ 7.0.1`** in control-fw so the gift build is reproducible (LEDC channel API depends on core 2.x). | R02 |
| A1.2 | **Wire the serialport Electron-ABI rebuild** (`app:rebuild` script + build/beforeBuild hook) or the packaged `.exe` runs telemetry-disabled. | R03 |
| A1.3 | Decide + apply the **canonical gear count** (fw 4 / doc 6 / HUD 8) and **drive-mode labels** (Gearbox vs RACE). | R05, R19 |
| A1.4 | Add CI steps: build `esp32dev_tuning`; package + serialport-rebuild smoke on the ground station. | R17 |
| A1.5 | Decide whether HUD `armed`/`failsafe` should be real (encode in FLIGHTMODE) or documented as sim-on-loss. | R01 |
| A1.6 | Run the **control-fw Wokwi sim once end-to-end**; confirm the serial log reaches DRIVING with `failsafe=0` (validates the 420000-baud loopback the Stage-2 story rests on). | R16 |

### A2. Physical checks before battery (multimeter, power OFF)

> **Step-by-step runbook:** `13_phase_a_a2_no_power_checklist.md` (tools, per-pin continuity,
> divider/Hall/ESC-isolation/ground/WS2812 measurements, table template, PASS/FAIL, hard-stops).
> **A2 has NOT been executed; Phase B stays blocked until the filled checklist is reviewed and approved.**

| # | Do | R## |
|---|---|---|
| A2.1 | **Diff PinMap.hpp against the actual soldered board** — continuity-check every signal: steering 13, ESC 14, DRS 18, gimbal 19/23, battery 34, Hall 35, CRSF 16/17, link2 25→16. (Atlas is illustrative-only; PinMap+BOM are the authority.) | R08 |
| A2.2 | **Confirm the ESC's +5 V BEC (red) wire is physically isolated** at ESP32 #1 before any battery — BEC back-feed damages the ESC. | — |
| A2.3 | **Confirm one common ground** across battery, ESC, both BECs, both ESP32s, camera, WiFi module, RP1 — the cross-board UART (25→16) and CRSF both depend on it. | R06 |
| A2.4 | Confirm the **WS2812 level path** (1N5819 diode *or*, preferred, 74AHCT125 shifter) is populated per the build sheet. | R20 |
| A2.5 | (Recommended) add a **pull-down/RC on the ESC (GPIO14) + steering (GPIO13) signal lines** so the boot float reads as safe/no-pulse. | R04 |

---

## Phase B — FIRST power, LOGIC ONLY (ESC motor power still disconnected)

The safety-critical bring-up. Corresponds to D8 Phases 2–6.

### B1. Links & signals
| # | Do | R## |
|---|---|---|
| B1.1 | **CRSF RX**: confirm RP1 output is **420000 8N1 NOT inverted**; `CrsfReceiver` produces `NewRcFrame`, `linkUp()` true, channels move. (Firmware has no inversion path.) | — |
| B1.2 | Antenna-off / RX-down event drives `uplinkLinkQuality→0` and **latches `rxSignalsFailsafe`**. | — |
| B1.3 | **Scope GPIO13/14/18** pulse widths (1000/1500/2000 µs) + 50 Hz period on the real ESP32 **before** connecting ESC/servos; confirm LEDC did not silently reduce 16-bit resolution. | R02 |
| B1.4 | **Scope GPIO14 + GPIO13 from power-on through first setup() write** — confirm no ESC arm/twitch or servo kick in the pre-`ledcAttachPin` float window. | R04 |

### B2. The safety chain (do NOT skip — this is the whole point)
| # | Do | R## |
|---|---|---|
| B2.1 | Power up with **no CRSF** → expect ESC neutral, DRS closed, steering centered. | — |
| B2.2 | Bring the link up **with the throttle stick forward** → motor command stays off until the stick returns to neutral (**ArmGate**) and the rearm window elapses. | R09 |
| B2.3 | Confirm the **ESC arms every boot** with the neutral-hold sequence (motor still disconnected); reconcile `bootArmHoldMs=2000` against the QuicRun 10BL120 manual; confirm **forward/brake** ESC mode (not forward/reverse). | R09 |
| B2.4 | Confirm worst-case **failsafe detection latency** at the chosen RP1 packet rate + LQ=0 burst stays within the ~540 ms budget (D8 Phase 2). | — |

### B3. Actuators (bench, unloaded) & board #2
| # | Do | R## |
|---|---|---|
| B3.1 | **Narrow steering endpoints** to the linkage's mechanical travel; sweep full L/R with linkage fitted — no bind/stall on the DS3235SG (endpoints need a code edit + reflash). | R10 |
| B3.2 | Confirm **DRS 1000 µs = wing closed** (failsafe-safe); swap open/closed if reversed. | — |
| B3.3 | **link2 on the wire**: capture control TX (GPIO25→16) at 115200 8N1; soundlight `Link2Monitor` reports `FrameReady` (not BadVersion/Invalid) — proves the two copied `lib/link2` trees are still in sync in flashed firmware. | R06 |
| B3.4 | Decode a **live link2 frame** on board #2 identical to sender intent (throttle%, brake bit, ERS-deploy bit, gear, driveMode). | R06 |
| B3.5 | **I2S audio** at 22050 Hz through the legacy IDF driver on the pinned `~7.0.1`; check `i2s.begin()` return codes; confirm no fail-silent/block. (7.0.1 → core 2.0.17 → legacy `driver/i2s.h` present.) | R11 |
| B3.6 | **WS2812 `show()` does not glitch while audio DMA runs** (dual-core + DMA/RMT interaction — no sim coverage). | R11 |
| B3.7 | Board #2 with **link2 RX disconnected at boot** → confirm the calm "breathe" (never-connected) is acceptable; then cut the wire mid-run → confirm **Lost→hazard within 500 ms**. | R22 |
| B3.8 | Board #2 **mid-frame power-on ordering** (board #1 already transmitting) → boots NeverConnected, syncs on the next start byte. | R16 |

### B4. Sensors (bench)
| # | Do | R## |
|---|---|---|
| B4.1 | **ADC battery divider extremes**: open/disconnected divider and full 8.4 V → sane `batteryMv`, no spurious low-voltage latch; log eFuse cal type; check the 8.4 V point isn't compressed by the 11 dB ceiling. | — |
| B4.2 | Confirm the **Hall (GPIO35)** counts on a rolling bench (single magnet); baseline before motor EMI. | R18 |

---

## Phase C — LATER (motor connected / on the car / calibration)

Corresponds to D8 Phases 7–11. Only after Phase B passes.

| # | Do | R## |
|---|---|---|
| C1 | With the ESC powered (wheels off ground): confirm first post-arm throttle spins the motor; test **brownout/reset mid-throttle** — measure motor coast time + ESC signal-loss behavior. | R09, R12 |
| C2 | **Hall EMI at full throttle** near the brushless motor: log raw edge count vs known revolutions, scope GPIO35; add 1–10 nF / RC / Schmitt if double-counting; reconcile `maxPlausibleRpm`/`kLockoutUs`. | R18 |
| C3 | **Battery two-point calibration** (~6.5 V and ~8.4 V), set `calibrationPpt`; confirm low-voltage warning latches ~7.0 V under load and clears with hysteresis. | — |
| C4 | Confirm the **telemetry send cadence (200 ms)** + link2 do not stall the 50 Hz control tick or overflow the CRSF TX FIFO at 420000 baud. | R13 |

### C-Ground — video + telemetry return path (ground station)
| # | Do | R## |
|---|---|---|
| CG1 | **Camera codec**: confirm OpenIPC emits **H.264** (WebRTC won't decode H.265) or add an ffmpeg transcode into mediamtx; confirm the WHEP endpoint actually **renders** video in Electron. | R15 |
| CG2 | **Telemetry reader path**: prove elrs-joystick-control can forward CRSF off the FT232 **or** set up com0com/hub4com; set `W17_TELEMETRY_PORT`; confirm `CrsfSerialSource` logs "CRSF serial open". | R14 |
| CG3 | Confirm ELRS actually **relays each frame type** (battery 0x08, GPS 0x02, FLIGHTMODE 0x21, LINK_STATISTICS 0x14) with the `0xC8` address; some builds forward only a subset / need an extended address. | R13 |
| CG4 | With the backchannel live, confirm **shared/crsf.js decodes real firmware frames** correctly (battery V, wheel km/h, gear/mode/ERS from "G# M# E#") — validates the C++↔JS reimplementation. | R07 |
| CG5 | **Deliberately drop the ELRS link** → observe HUD behavior (today it reverts to "Telemetry: sim", NOT "LINK LOST"); decide if that's acceptable gift-day behavior. | R01 |
| CG6 | Confirm the **live gear** and **driveMode label** on the HUD match the car across a full shift sweep (guards the 4/6/8 divergence). | R05, R19 |

### C-Robustness (later, optional)
| # | Do | R## |
|---|---|---|
| CR1 | **Corrupt the NVS partition** (write garbage) + power-cycle → guard chain falls back to `kDefaults` (real-flash path untested). | R16 |
| CR2 | Sustained max-rpm + overrun + limiter render with lights active → **no audio underrun/glitch**; confirm the speaker doesn't clip on overrun crackle bursts at full volume. | R11 |
| CR3 | Low-speed wheel reporting on a rolling bench doesn't saw-tooth toward zero between pulses (consider a 2nd magnet). | R21 |

---

## Top 10 — verify BEFORE connecting real hardware (desk / software)

1. Pin `platform = espressif32 @ 7.0.1` in control-fw — reproducible gift build. **(R02)**
2. Wire the serialport Electron-ABI rebuild or the `.exe` ships telemetry-disabled. **(R03)**
3. Diff PinMap.hpp vs the soldered board — continuity-check every signal. **(R08, A2.1)**
4. Confirm ESC +5 V BEC isolated + single common ground before any battery. **(A2.2/A2.3)**
5. Run the control-fw Wokwi sim once to a live link (`failsafe=0`). **(R16)**
6. Decide canonical gear count + drive-mode labels; apply. **(R05, R19)**
7. Add CI: build `esp32dev_tuning`; package/serialport smoke on the ground station. **(R17)**
8. Add a link2 drift-guard (CI diff or submodule) across the copied trees. **(R06)**
9. Add ESC/steering signal pull-down; decide on the WS2812 74AHCT125 shifter. **(R04, R20)**
10. Confirm RP1 CRSF = 420000 8N1 **non-inverted** (firmware has no inversion path). **(B1.1)**

## Top 10 — test IMMEDIATELY when parts arrive (bench, motor power OFF first)

1. CRSF RX parses real RP1 frames; link up, channels move; RX-down latches failsafe. **(B1.1/B1.2)**
2. Scope GPIO13/14/18 pulse widths + 50 Hz before connecting ESC/servos. **(B1.3, R02)**
3. Scope GPIO14/13 through the boot float window — no ESC twitch/servo kick. **(R04)**
4. Full safety chain: no-CRSF-safe, arm-into-throttle blocked, fresh-neutral rearm. **(B2.1/B2.2)**
5. ESC arms each boot (motor disconnected); reconcile 2 s hold + forward/brake mode. **(R09)**
6. Narrow + sweep steering endpoints — no linkage bind/stall. **(R10)**
7. link2 on the wire: soundlight decodes control's frames identically (no drift). **(R06)**
8. I2S audio + WS2812 show() coexist without glitch on board #2. **(R11)**
9. Board #2 failsafe: link cut mid-run → hazard + engine-off within 500 ms. **(R22)**
10. ADC battery extremes sane; Hall counts on a rolling bench. **(B4.1/B4.2)**

## Unknown-unknowns (hardware truths the code assumes but cannot prove)

- **ELRS relays locally-originated `0xC8` sensor/GPS/flight-mode frames** — the whole telemetry
  feature silently produces nothing if not (may need an extended device address). **(R13)**
- **The QuicRun's arming + signal-loss behavior** (2 s hold, forward/brake, neutral-on-signal-loss)
  — assumed, not from the manual; governs both arm reliability and brownout coast. **(R09, R12)**
- **LEDC truly gives 16-bit @ 50 Hz on this board** (analysis says yes, ~20.6-bit ceiling) —
  a silent clamp would corrupt every pulse width. **(R02)**
- **The camera's codec (H.264 vs H.265)** — gates all WebRTC video. **(R15)**
- **Shared-rail power integrity** — WS2812 inrush / I2S draw / servo stall spikes browning out
  board #1 on the common BEC rail. **(R12)**
- **Real-world EMI on the Schmitt-less Hall input** under motor load. **(R18)**
