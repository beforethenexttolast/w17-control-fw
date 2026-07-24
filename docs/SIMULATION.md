# Wokwi simulation (validation Stage 2)

End-to-end run of the real firmware with zero hardware: virtual servos on the PWM pins, a
potentiometer as the battery divider, a pushbutton as the Hall sensor, and a scripted CRSF
stream self-fed through a UART2 TX→RX loopback wire — so the genuine UART driver, parser,
failsafe, arm gate, gearbox, and outputs all run unmodified.

## Run status — READ FIRST (as of 2026-07-25)

**The sim builds. It has never been run.** Nothing in this file below the build line is
observed behaviour; the demo table and the interactive notes are *design intent* derived
from the firmware and `src/SimCrsfFeeder.cpp`, not a transcript.

| Claim | Status | Evidence |
|---|---|---|
| `pio run -e esp32dev_sim` produces `firmware.bin` + `firmware.elf` | **VERIFIED** | 2026-07-25, this host. Also `esp32dev` + `esp32dev_tuning` green, native suite 225/225. |
| Wokwi loads `diagram.json` without a pin-name error | **UNVERIFIED** | never loaded |
| CRSF decodes at 420000 baud over the `TX2→RX2` loopback → `failsafe=0` | **UNVERIFIED** | never run — this is the open question in `project-review/open_questions.md` (→ R16) and it stays open |
| Any phase of the 25 s script behaves as the table below says | **UNVERIFIED** | never run |
| Stall → 2 s TWDT panic → reboot → reset reason | **UNVERIFIED** | never run; see "Watchdog stall validation" below |

**Why it has not been run here:** every Wokwi route needs a credential plus network egress
to Wokwi's servers (the firmware image is uploaded there to be simulated). On this host
`wokwi-cli` is not installed and `WOKWI_CLI_TOKEN` is unset; the VS Code extension *is*
installed (3.6.0, licensed) but only starts from an interactive `Wokwi: Start Simulator`
command, which an automated session cannot drive. Running it is therefore an owner action
— see "How to run".

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

## Interactive bits

- **Battery pot** (GPIO34): starts ≈69% ≈ 2.27 V pin ≈ 8.4 V battery. Turn it below ≈1.9 V
  pin (≈57%) and hold for 3 s → `lowBatt=1` in the status line (sustained-low warning);
  raise above ≈2.0 V to clear (hysteresis).
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

**Status: none of these have been attempted** (see "Run status" — the sim has never been
loaded). They are genuine Wokwi-platform unknowns, so they can only be closed by a run;
code review cannot close any of them.

- [ ] Pin labels: diagram uses devkit-v1 silkscreen names (`D13`, `TX2`, `RX2`, `GND.1`…).
      Wrong names fail fast at diagram load with a list of valid pins — fix and reload.
- [ ] CRSF decodes at 420000 baud over the loopback (first `[state]` line shows
      `failsafe=0` within ~3 s). Fallback if not: sim-only baud override build flag.
- [ ] Pot `value` attr actually presets the position (expect `batt≈8400mV` at boot).
- [ ] `firmware.bin` boots as-is; if Wokwi wants a merged image, point `wokwi.toml` at an
      esptool `merge_bin` output instead.
- [ ] Button bounce pulses land inside the 2 ms ISR lockout (rpm counts not inflated).

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
```

**Verified 2026-07-25 (build-level, no run):** the stall build compiles clean, and a `strings`
scan of the ELFs proves the injector is where it should be and nowhere else —
`W17_SIM_WDT_STALL_DELIBERATE_LOOPTASK_HANG` appears **once** in the stall ELF and **zero**
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
