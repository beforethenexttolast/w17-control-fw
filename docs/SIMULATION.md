# Wokwi simulation (validation Stage 2)

End-to-end run of the real firmware with zero hardware: virtual servos on the PWM pins, a
potentiometer as the battery divider, a pushbutton as the Hall sensor, and a scripted CRSF
stream self-fed through a UART2 TX→RX loopback wire — so the genuine UART driver, parser,
failsafe, arm gate, gearbox, and outputs all run unmodified.

## Run status — READ FIRST (as of 2026-08-04)

**The sim has now been run.** The demo build was loaded in Wokwi by the owner on
2026-08-04 and produced the transcript in "Observed run" below. Everything ticked here is
traceable to a line of that transcript. The **watchdog stall run is still not done** — it is
a separate build and a separate run, and nobody has started it.

| Claim | Status | Evidence |
|---|---|---|
| `pio run -e esp32dev_sim` produces `firmware.bin` + `firmware.elf` | **VERIFIED** | 2026-08-04, this host. Also `esp32dev` + `esp32dev_tuning` green, native suite **229/229** (225 + the 4 cases added by `91f830f`). |
| Wokwi loads `diagram.json` without a pin-name error | **VERIFIED** | 2026-08-04 — boot ROM banner through to `[sim] phase:` narration, no diagram-load error. |
| CRSF decodes at 420000 baud over the `TX2→RX2` loopback → `failsafe=0` | **VERIFIED** | 2026-08-04 — `failsafe=0` on the second `[state]` line after the feeder starts sending (≈2.5 s from boot). **This closes the R16 question.** |
| Any phase of the 25 s script behaves as the table below says | **VERIFIED for the control-path claims**, with two analog-input anomalies | 2026-08-04 — every phase transition below is matched to a transcript line in "Observed run". The battery and rpm readings did **not** match; see "Anomalies". |
| Stall → 2 s TWDT panic → reboot → reset reason | **UNVERIFIED** | never run; see "Watchdog stall validation" below |

**Why the watchdog run is still open:** it needs a *different* binary
(`-DW17_SIM_WDT_STALL`) started in Wokwi, and no one has started it. This is an owner
action for the same reason as before: on this host `wokwi-cli` is not installed and
`WOKWI_CLI_TOKEN` is unset, and the VS Code extension (3.6.0, licensed) only starts from
the interactive `Wokwi: Start Simulator` command, which an automated session cannot drive.
The recipe is one command plus one click — see "Watchdog stall validation".

Do not tick a box in this file from reasoning. A box gets ticked when someone has the
serial transcript in front of them.

## How to run

```
pio run -e esp32dev_sim
```

Then either:
- **VS Code:** install the Wokwi extension (free license required), open the project,
  `Wokwi: Start Simulator` — it picks up `wokwi.toml` + `diagram.json`.
- **CLI:** `wokwi-cli .` (needs `WOKWI_CLI_TOKEN`).

The serial monitor (115200) narrates everything: `[sim] phase: ...` lines mark script
transitions, `[state] ...` lines print the live control state at 2 Hz.

The `esp32dev_sim` env is the real firmware plus `-DW17_SIM_CRSF_FEEDER`, which compiles in
`src/SimCrsfFeeder.cpp` (the scripted frame source) and the status prints. The plain
`esp32dev` build contains none of it.

## What the demo shows (~25 s loop)

| t (s) | phase | what to watch |
|---|---|---|
| 0–2 | SILENT | No CRSF at all. Servos hold center/neutral/closed — boot-safe (first cycle: never-received-a-frame latch; later cycles: outage). |
| 2–5 | DISARMED_STEERING | Steering servo sweeps while the ESC needle stays put: steering is live while disarmed, throttle is gated. |
| 5–6.5 | ARM_BLOCKED | Arm switch ON with throttle at 60% — ESC stays neutral (§6.2 "no arm-into-full-throttle"). Genuinely the ArmGate: the ESC's own 2 s boot hold expired long ago. |
| 6.5–8 | ARM_NEUTRAL | Throttle centered → arms. |
| 8–15 | DRIVING | Throttle sweeps move the ESC needle; gear-up pulses at 9 s and 12 s visibly raise the cap; DRS servo opens 10–14 s. At 13–14.5 s the mode switch flips to Gearbox+ERS with boost held: the needle jumps past the gear cap and the `[state]` line shows the store draining `(DEPLOY)` at ~26 %/s. |
| 15–17.5 | TIMEOUT_OUTAGE | Pure silence, **no** LQ=0 — watch the ~0.5 s **delayed** drop to safe: the frame-timeout path. |
| 17.5–19 | RECOVERY_1 | Stats (LQ=100) lead the recovery — the LQ latch clears **only** on good stats, never on RC frames. ~150 ms re-arm window, then Active. |
| 19–21 | HOLD_POSITION_FAILSAFE | LQ=0 stats **while RC frames keep flowing at 50% throttle** — instant drop despite fresh frames. This is the misconfigured-receiver (hold-position) mitigation and the most valuable thing this sim demonstrates. |
| 21–23 | RECOVERY_2 | Link good again but the stick is still at 50% — blocked until it centers at 22 s (fresh-neutral rule after every failsafe). |
| 23–25 | COOLDOWN | Two gear-down pulses so every cycle restarts from gear 1 (gear deliberately survives failsafe). |

## Observed run — 2026-08-04 (owner, Wokwi VS Code extension)

The first ever load of this sim. Raw serial (115200), whitespace-normalised — the Wokwi
monitor pads lines with CR runs; no line content is edited, and nothing is omitted from
boot to the point the capture was stopped, ~1.1 script cycles in. `[state]` lines are the
2 Hz status print; the leading `failsafe/armed/...` fields are quoted verbatim.

```
rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
...
entry 0x400805dc
[sim] W17 control firmware -- Wokwi Stage-2 demo build
[boot] reset=POWER_ON boots=1 retained=no
[   528][E][Preferences.cpp:50] begin(): nvs_open failed: NOT_FOUND
[sim] phase: SILENT
[state] failsafe=1 armed=0 mode=1 gear=1 thr=0 steer=0    ers=100% rpm=0 batt=1384mV lowBatt=0
[state] failsafe=1 armed=0 mode=1 gear=1 thr=0 steer=0    ers=100% rpm=0 batt=1384mV lowBatt=0
[state] failsafe=1 armed=0 mode=1 gear=1 thr=0 steer=0    ers=100% rpm=0 batt=1384mV lowBatt=0
[sim] phase: DISARMED_STEERING
[state] failsafe=1 armed=0 mode=1 gear=1 thr=0 steer=0    ers=100% rpm=0 batt=1384mV lowBatt=0
[state] failsafe=0 armed=0 mode=1 gear=1 thr=0 steer=307  ers=100% rpm=0 batt=1384mV lowBatt=0
[state] failsafe=0 armed=0 mode=1 gear=1 thr=0 steer=223  ers=100% rpm=0 batt=1384mV lowBatt=0
[state] failsafe=0 armed=0 mode=1 gear=1 thr=0 steer=-758 ers=100% rpm=0 batt=1384mV lowBatt=1
[state] failsafe=0 armed=0 mode=1 gear=1 thr=0 steer=307  ers=100% rpm=0 batt=1384mV lowBatt=1
[state] failsafe=0 armed=0 mode=1 gear=1 thr=0 steer=223  ers=100% rpm=0 batt=1384mV lowBatt=1
[sim] phase: ARM_BLOCKED
[state] failsafe=0 armed=0 mode=1 gear=1 thr=0 steer=0    ers=100% rpm=0 batt=1384mV lowBatt=1
[state] failsafe=0 armed=0 mode=1 gear=1 thr=0 steer=0    ers=100% rpm=0 batt=1384mV lowBatt=1
[state] failsafe=0 armed=0 mode=1 gear=1 thr=0 steer=0    ers=100% rpm=0 batt=1384mV lowBatt=1
[sim] phase: ARM_NEUTRAL
[state] failsafe=0 armed=1 mode=1 gear=1 thr=0 steer=0    ers=100% rpm=0 batt=1384mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=1 thr=0 steer=0    ers=100% rpm=0 batt=1384mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=1 thr=0 steer=0    ers=100% rpm=0 batt=1384mV lowBatt=1
[sim] phase: DRIVING
[state] failsafe=0 armed=1 mode=1 gear=1 thr=3   steer=-389 ers=100% rpm=0    batt=1384mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=1 thr=131 steer=-123 ers=100% rpm=0    batt=1384mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=2 thr=578 steer=142  ers=100% rpm=0    batt=1384mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=2 thr=209 steer=387  ers=100% rpm=600  batt=1384mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=2 thr=7   steer=120  ers=100% rpm=100  batt=1384mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=2 thr=231 steer=-143 ers=100% rpm=1500 batt=1384mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=2 thr=578 steer=-389 ers=100% rpm=176  batt=1384mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=2 thr=209 steer=-123 ers=100% rpm=71   batt=1384mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=3 thr=12  steer=142  ers=100% rpm=44   batt=1384mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=3 thr=353 steer=387  ers=100% rpm=0    batt=1384mV lowBatt=1
[state] failsafe=0 armed=1 mode=2 gear=3 thr=915 steer=120  ers=98%(DEPLOY)  rpm=0 batt=1384mV lowBatt=1
[state] failsafe=0 armed=1 mode=2 gear=3 thr=382 steer=-143 ers=85%(DEPLOY)  rpm=0 batt=1384mV lowBatt=1
[state] failsafe=0 armed=1 mode=2 gear=3 thr=14  steer=-389 ers=73%(DEPLOY)  rpm=0 batt=1384mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=3 thr=353 steer=-123 ers=62% rpm=0 batt=2669mV lowBatt=1
[sim] phase: TIMEOUT_OUTAGE
[state] failsafe=0 armed=1 mode=1 gear=3 thr=776 steer=120 ers=62% rpm=0 batt=4289mV lowBatt=1
[state] failsafe=1 armed=0 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=4429mV lowBatt=1
[state] failsafe=1 armed=0 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=3222mV lowBatt=1
[state] failsafe=1 armed=0 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=3931mV lowBatt=1
[state] failsafe=1 armed=0 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=4392mV lowBatt=1
[sim] phase: RECOVERY_1
[state] failsafe=1 armed=0 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=4470mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=4528mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=4557mV lowBatt=1
[sim] phase: HOLD_POSITION_FAILSAFE
[state] failsafe=1 armed=0 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=4573mV lowBatt=1
[state] failsafe=1 armed=0 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=4580mV lowBatt=1
[state] failsafe=1 armed=0 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=4584mV lowBatt=1
[state] failsafe=1 armed=0 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=4586mV lowBatt=1
[sim] phase: RECOVERY_2
[state] failsafe=1 armed=0 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=4587mV lowBatt=1
[state] failsafe=0 armed=0 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=4588mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=4588mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=4588mV lowBatt=1
[sim] phase: COOLDOWN
[state] failsafe=0 armed=1 mode=1 gear=3 thr=0 steer=0 ers=62% rpm=0 batt=4588mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=2 thr=0 steer=0 ers=62% rpm=0 batt=4588mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=2 thr=0 steer=0 ers=62% rpm=0 batt=4588mV lowBatt=1
[state] failsafe=0 armed=1 mode=1 gear=1 thr=0 steer=0 ers=62% rpm=0 batt=4588mV lowBatt=1
[sim] phase: SILENT
[state] failsafe=0 armed=1 mode=1 gear=1 thr=0 steer=0 ers=62% rpm=0 batt=4588mV lowBatt=1
[state] failsafe=1 armed=0 mode=1 gear=1 thr=0 steer=0 ers=62% rpm=0 batt=4588mV lowBatt=1
[state] failsafe=1 armed=0 mode=1 gear=1 thr=0 steer=0 ers=62% rpm=0 batt=4588mV lowBatt=1
[state] failsafe=1 armed=0 mode=1 gear=1 thr=0 steer=0 ers=62% rpm=0 batt=4588mV lowBatt=1
[sim] phase: DISARMED_STEERING
[state] failsafe=1 armed=0 mode=1 gear=1 thr=0 steer=0 ers=62% rpm=0 batt=4588mV lowBatt=1
```

### What each phase actually demonstrated

| Phase | Claim from the table above | Transcript evidence | Verdict |
|---|---|---|---|
| SILENT (cycle 1) | boot-safe on the never-received latch | first three `[state]`: `failsafe=1 thr=0 steer=0` | **shown** |
| DISARMED_STEERING | link comes up; steering live, throttle gated | `failsafe=0` on the 2nd line of the phase; `steer` sweeps 307 / 223 / −758 while `thr=0` throughout | **shown** — and this is the R16 answer |
| ARM_BLOCKED | arm ON at 60 % throttle must not arm | `armed=0 thr=0` for all 3 lines, ≈5–6.5 s, long after the ESC's 2 s boot hold | **shown** — genuinely the ArmGate |
| ARM_NEUTRAL | throttle centred → arms | `armed=1` on the first line of the phase | **shown** |
| DRIVING | throttle sweeps; two gear-ups; ERS overboost past the gear cap | `thr` sweeps 3→578; `gear` 1→2 at ≈9 s and 2→3 at ≈12 s; `mode` 1→2 with `thr=915` against gear 3's `maxOutput=800` | **shown** — the 915 > 800 line is the overboost |
| DRIVING (ERS drain) | store drains ≈26 %/s | `98 → 85 → 73 → 62` `(DEPLOY)` at 0.5 s per line = **26 / 24 / 22 %/s** | **shown**, matches the documented rate |
| DRIVING (DRS) | DRS servo opens 10–14 s | not in the `[state]` print | **not observable** — visual-only |
| TIMEOUT_OUTAGE | *delayed* drop on frame timeout | last active line `thr=776`, next line `failsafe=1 armed=0 thr=0 steer=0` | **direction shown; the ~0.5 s figure is not measured** — the 2 Hz print cannot resolve it |
| RECOVERY_1 | link returns, re-arms | `failsafe=1` → `failsafe=0 armed=1` | **shown**; the "clears only on good stats, never on RC frames" mechanism is *not* separable here (the feeder sends both) |
| HOLD_POSITION_FAILSAFE | LQ=0 drops instantly **despite fresh RC frames at 50 % throttle** | `failsafe=1 armed=0 thr=0` on the first line of the phase and every line after | **shown** — the most valuable result in the run after R16 |
| RECOVERY_2 | fresh-neutral rule: link good ≠ armed | `failsafe=0 armed=0` for one line, then `armed=1` | **shown** — the disarmed gap is visible |
| COOLDOWN | two gear-downs back to gear 1 | `gear` 3→2→2→1 | **shown** |
| SILENT (cycle 2) | later cycles take the *outage* path, not the never-received latch | `failsafe=0 armed=1` for one line after frames stop, then `failsafe=1` | **shown** — delayed, so timeout not never-received |
| — | sustained-low battery warning latches after 3 s | `lowBatt` 0→1 between the 3rd and 4th line of DISARMED_STEERING, ≈3.5 s after the first sample | **shown** (on a bogus input voltage — see Anomalies) |
| — | low-battery hysteresis does not clear below `warnMv + 400` | `lowBatt=1` holds at `batt=4588mV` (< 7400) for the rest of the run | **shown** |
| — | wheel-speed decay tail + hard zero at `zeroSpeedTimeoutMs` | after the last spurious edge: `176 → 71 → 44 → 0`, i.e. `60000/elapsed` for 341/845/1364 ms, then zero past 1500 ms | **shown** (on spurious edges — see Anomalies) |

### Anomalies — both analog inputs, neither a firmware defect

**1. Battery reads nothing like the pot preset.** The first-run checklist expected
`batt≈8400mV` at boot from `potBattery`'s `value: "69"`. Observed: a flat **1384 mV** for
the first ~14 s, then a noisy climb (2669 → 4289 → 4429 → 3222 → 3931 → …) settling at a
steady **4588 mV** for the rest of the run. 4588 mV through the 37/10 divider is a pin
voltage of ≈1240 mV ≈ 37.6 % of 3.3 V — not 69 %, and the flat-then-step shape is not what
a 10 Hz sampler with `emaShift=3` (≈0.8 s time constant) does to a constant input.

`BatteryMonitor` itself behaved exactly to spec *given that input*: seeded from the first
sample rather than 0, EMA-smoothed, latched `lowBatt=1` after 3 s continuously below
`warnMv=7000`, and correctly refused to clear at 4588 mV (needs > 7400). Battery telemetry
is monitoring-only and has no control authority, so this changes nothing about the control
path. **Unresolved question:** what Wokwi's potentiometer `value` attribute and ADC model
actually present on GPIO34, and whether `analogReadMilliVolts`'s calibration fallback (no
eFuse Vref in the sim) is being applied. This is a Wokwi-fidelity question, and it means
the sim **cannot** be used to sanity-check the divider maths — that stays a Phase-B item.

**2. Spurious wheel pulses with nobody touching the button.** `rpm` went 600 and then 1500
during DRIVING with no button click in the whole run — GPIO35 sits at 3V3 through the 10 kΩ
pull-up and should see no edges at all. Implied edge spacings are 100 ms and 40 ms, i.e.
real edge *pairs*, well outside the ISR's 2 ms lockout, and both are below
`maxPlausibleRpm=5000` so the glitch clamp never engaged. The decay tail afterwards is
textbook-correct (see the table above), which is the one genuinely useful thing this
produced. Again telemetry-only — rpm feeds ERS harvest gating and the link2 report, never
an output. **Unresolved:** whether this is Wokwi's pin model or something the real A3144
wiring would also see. The sim cannot tell them apart, so the Hall bounce question stays a
bench item.

## Interactive bits

- **Battery pot** (GPIO34): *intended* to start at ≈69% ≈ 2.27 V pin ≈ 8.4 V battery — but
  the 2026-08-04 run showed `batt=1384mV` at boot settling to 4588 mV, i.e. the preset does
  **not** land where this line says (see "Anomalies"). The mechanism still works: hold the
  reading below `warnMv` (7000 mV) for 3 s → `lowBatt=1` (sustained-low warning, observed);
  it clears only above 7400 mV (hysteresis, also observed — it never cleared). Treat the
  absolute voltages here as unverified until someone works out the pot/ADC model.
- **Hall button** (GPIO35): one full click = one wheel pulse, counted on **release** (rising
  edge through the 10 kΩ pull-up, exactly like the real A3144 wiring). Rhythmic clicking
  shows plausible low rpm; the ISR's 2 ms lockout absorbs simulated contact bounce.
  Note: ERS **harvest** requires wheel rpm > 0, so in the sim the store only recharges
  while you're clicking the Hall button (braking/coasting at standstill charges nothing —
  same rule as the real car).

## Known cosmetic quirks

- `wokwi-servo` maps roughly 544–2400 µs → 0–180°, so the steering's 500–2500 µs endpoints
  visually clamp for the last few percent of travel. **Cosmetic only — do not "fix"
  `ServoOutput`.**
- The ESC "servo" is just a PWM visualizer: needle center = neutral, right = forward.

## First-run verify checklist (low-confidence Wokwi platform facts)

**Status after the 2026-08-04 run: 3 of 5 pass, 1 fails, 1 untried.** Each tick points at a
line of the transcript above.

- [x] Pin labels: diagram uses devkit-v1 silkscreen names (`D13`, `TX2`, `RX2`, `GND.1`…).
      **Pass** — the diagram loaded and the firmware ran; no pin-name error.
- [x] CRSF decodes at 420000 baud over the loopback (first `[state]` line shows
      `failsafe=0` within ~3 s). **Pass** — `failsafe=0` on the second `[state]` line after
      the feeder starts at t=2 s, so ≈2.5 s from boot. No baud override needed.
- [ ] Pot `value` attr actually presets the position (expect `batt≈8400mV` at boot).
      **FAIL** — observed `batt=1384mV` at boot, settling to 4588 mV. See "Anomalies".
      This box stays unticked and the *reason* is still unknown.
- [x] `firmware.bin` boots as-is; if Wokwi wants a merged image, point `wokwi.toml` at an
      esptool `merge_bin` output instead. **Pass** — booted straight from
      `.pio/build/esp32dev_sim/firmware.bin`, no `merge_bin` needed.
- [ ] Button bounce pulses land inside the 2 ms ISR lockout (rpm counts not inflated).
      **NOT ATTEMPTED** — the button was never clicked in this run. Separately, two
      *unclicked* edge pairs appeared anyway (see "Anomalies"), which makes this test
      harder to interpret rather than easier.

One incidental note for anyone re-running: `nvs_open failed: NOT_FOUND` at boot is expected
and benign on a fresh sim — Wokwi starts with a blank NVS partition, so `loadOrDefault`
takes its guard-chain path to complete compiled defaults, which is the designed behaviour.

## Watchdog stall validation (R5-b) — the fault-injection run

The stall injector is **already implemented** and needs no new code: `src/main.cpp` carries a
`#ifdef W17_SIM_WDT_STALL` block at the top of `loop()` that, after 3 s of runtime, prints a
unique marker and busy-spins forever without ever reaching the single `esp_task_wdt_reset()`
feed at the end of the 50 Hz control tick. The subscribed `loopTask` therefore misses its 2 s
deadline and the TWDT must panic-and-reset.

The macro is deliberately **not** set in any checked-in environment — it is supplied ad hoc:

```
PLATFORMIO_BUILD_FLAGS="-DW17_SIM_WDT_STALL" pio run -e esp32dev_sim   # then start Wokwi
pio run -e esp32dev_sim                                                # REBUILD after, so
                                                                       # wokwi.toml's elf is
                                                                       # the clean sim again
pio run -e esp32dev -e esp32dev_tuning                                 # and these too, see below
```

**Gotcha found 2026-08-04:** setting `PLATFORMIO_BUILD_FLAGS` changes PlatformIO's project
checksum, which makes it **wipe every environment's build directory**, not just the one being
built. So the stall build silently deletes `.pio/build/esp32dev/` and
`.pio/build/esp32dev_tuning/`, and the clean rebuild afterwards deletes them again. Rebuild all
three when you are done, or the next ELF scan will read "0 matches" off files that do not
exist.

**Re-verified 2026-08-04 (build-level, no run):** the stall build compiles clean, and a
`strings` scan of freshly built ELFs proves the injector is where it should be and nowhere else
— `W17_SIM_WDT_STALL_DELIBERATE_LOOPTASK_HANG` appears **once** in the stall ELF and **zero**
times in `esp32dev`, `esp32dev_tuning`, and `esp32dev_sim`. So no delivery, tuning, or normal
sim binary can stall.

**Still UNVERIFIED — what a run must capture** (paste the transcript here when it happens):

- [ ] the marker line prints at ~3 s
- [ ] a TWDT panic follows within ~2 s of the last fed tick, naming the loopTask
- [ ] the board reboots
- [ ] the post-reboot `[boot]` line reads `reset=TASK_WDT`, with `boots` **incremented** and
      `retained=yes`

That last box is the interesting one: **no board or sim has ever produced anything but
`reset=POWER_ON boots=1 retained=no`** (3× ESP32-D0WD-V3, bare-board smoke test 2026-07-22).
The crash-class branch of `reset_diag::classify` and the RTC retained-counter increment are
covered by native tests only — they have never been exercised on a real reset of any kind,
emulated or physical.

**The 2026-08-04 sim run did not change this.** Its `[boot]` line reads
`reset=POWER_ON boots=1 retained=no` — a fourth data point on the same branch, and the run
never rebooted, so the retained counter was never incremented. The crash-class path remains
zero-for-four in the real world.

To close this, someone runs the stall build once and pastes the transcript into the boxes
above. It is one command and one `Wokwi: Start Simulator` click, and the run is over inside
6 s of simulated time.

## What this sim can never settle (stays Phase-B evidence)

Even a fully green Wokwi run does **not** promote any of these; they are silicon/analogue
facts and Wokwi models the peripherals rather than reproducing them:

- **Reboot-to-safe-output timing.** The real bound on the watchdog policy is
  `TWDT timeout + panic/reboot-to-safe-output interval`, and the second term is hardware-only
  (panic text prints before the reset, and LEDC may keep driving the previous duty throughout).
- **GPIO13 / GPIO14 logic level during the reset → `ledcAttachPin` window.** Whether the
  steering and ESC signal pins float, pull high, or pull low across a reset — and for how long
  — decides whether a powered ESC twitches. Wokwi's pin model is not evidence for this (→ R04).
- **Real ESC signal-loss behaviour.** Whether the QuicRun 10BL120 treats absent/garbage PWM as
  disarmed-neutral, and how fast it stops the motor (→ R12).

The 2 s TWDT timeout also stays **provisional** regardless of any sim result, for exactly the
first reason above.

## Future hook (not built)

`wokwi-cli` supports scenario YAML + serial-output assertions — automated Stage-2 regression
runs of this exact script in CI. Worth wiring up if the firmware keeps evolving after the
gift ships.

Note the prerequisite before anyone plans this work: it needs a `WOKWI_CLI_TOKEN` (from
wokwi.com's CI dashboard) and network egress from the runner, the **same blocker** that keeps
the manual run above unexecuted. The scenario YAML is the easy half.
