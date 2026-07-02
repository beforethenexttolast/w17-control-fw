# Wokwi simulation (validation Stage 2)

End-to-end run of the real firmware with zero hardware: virtual servos on the PWM pins, a
potentiometer as the battery divider, a pushbutton as the Hall sensor, and a scripted CRSF
stream self-fed through a UART2 TX→RX loopback wire — so the genuine UART driver, parser,
failsafe, arm gate, gearbox, and outputs all run unmodified.

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

- [ ] Pin labels: diagram uses devkit-v1 silkscreen names (`D13`, `TX2`, `RX2`, `GND.1`…).
      Wrong names fail fast at diagram load with a list of valid pins — fix and reload.
- [ ] CRSF decodes at 420000 baud over the loopback (first `[state]` line shows
      `failsafe=0` within ~3 s). Fallback if not: sim-only baud override build flag.
- [ ] Pot `value` attr actually presets the position (expect `batt≈8400mV` at boot).
- [ ] `firmware.bin` boots as-is; if Wokwi wants a merged image, point `wokwi.toml` at an
      esptool `merge_bin` output instead.
- [ ] Button bounce pulses land inside the 2 ms ISR lockout (rpm counts not inflated).

## Future hook (not built)

`wokwi-cli` supports scenario YAML + serial-output assertions — automated Stage-2 regression
runs of this exact script in CI. Worth wiring up if the firmware keeps evolving after the
gift ships.
