# W17 — Bench Bring-Up Runbook (Stage 3 / D8)

> **Phase -1 — HARD STOP: A2 CLOSED and Phase B APPROVED, or stop here.** This entire
> runbook is Phase-B-and-later work. It assumes `project-review/13_phase_a_a2_no_power_checklist.md`
> has been filled in with real readings, its §12 two-part PASS is recorded, and
> `../../CURRENT_STATUS.md` says Phase B is open. **As of this revision, neither is true: A2
> is NOT-EXECUTED and Phase B is BLOCKED** (`../CLAUDE.md` hardware-gates section). No
> battery, no USB, no bench PSU, nothing flashed, until that status changes — re-check
> `../../CURRENT_STATUS.md` immediately before Phase 1 and again before Phase 3, since gate
> state can move between sessions. Standalone first-power sequencing (which board, which
> order, what "logic only" means before the ESC gets motor power) lives in
> `PHASE_B_FIRST_POWER.md`; this document is the phase-by-phase detail Phase B's own
> ledger points back to.

The single ordered checklist to take the firmware from "builds + passes tests" to "driving on
the car," once hardware arrives and the gate above has actually cleared. **Do the phases in
order** — each is a safety/dependency gate for the next. The golden rule threads through all of
it: **wheels off the ground, and no ESC power, until the failsafe + arm gate are proven live
(Phase 5).**

Sound/light board: `w17-soundlight-fw` `esp32dev` — flash it together with the control board
per `COORDINATED_FLASH.md` (the two boards' `lib/link2` copies must agree, or board #2 rejects
every frame as invalid the instant a length byte disagrees). Ground station:
`w17-ground-station` (`npm run demo` until the camera is wired; `npm run demo:low-battery`
exercises the HUD's low-battery path with a scripted timeline — no car, no battery, see
Phase 8).

Tools: multimeter, oscilloscope/logic analyzer, a serial monitor at 115200, the `elrs-joystick-
control` PC setup, and a way to spin the rear axle by hand (Hall test).

---

## Phase 0 — Pre-power electrical fixes (before ANY battery is connected)

From `docs/00_BUILD_SHEET.md` "bench fixes" + the design-review adds. Getting these wrong risks
hardware on first power.

- [ ] **ESC throttle: RED (+5V BEC) wire isolated** at ESP32 #1 — signal + GND only (6V BEC
      back-feed damages the ESC). The two UBECs power the rails.
- [ ] **Battery divider 27 kΩ / 10 kΩ** on GPIO34 (not 20k/10k). Tap in the middle → GPIO34.
- [ ] **100 nF cap GPIO34 → GND** (divider source impedance ~7.3 kΩ makes single reads noisy).
- [ ] **A3144 Hall: VCC = 5 V, 10 kΩ pull-up to 3.3 V**, open-collector out → GPIO35.
- [ ] **WS2812 (board #2):** 330 Ω series on data, 1000 µF across strip 5V/GND, 1N5819 on strip
      VDD (or a level shifter) for a clean 3.3 V→data logic high.
- [ ] **1000 µF cap on the servo rail** (DS3235SG stall spikes).
- [ ] **All grounds common:** battery, ESC, both BECs, both ESP32s, camera, WiFi module, RP1.
- [ ] Rails: A (clean) = camera + WiFi + both ESP32 + RX + LEDs; B = steering + 3× MG90S + blower.

## Phase 1 — Power rails smoke (no firmware dependency)

> **Stop — first battery connection of this bring-up.** Re-confirm the Phase -1 gate
> (A2 closed, Phase B approved in `../../CURRENT_STATUS.md`) before this step. Everything
> before this line needed no battery; everything from here on does.

- [ ] Battery → XT60 Y-split → ESC + BEC#1 + BEC#2. Confirm BEC#1 ≈ 5 V, BEC#2 ≈ 5–6 V under a
      light load, before connecting the ESP32s.
- [ ] Confirm no rail sag / brownout when a servo moves (that's what the 1000 µF is for).

## Phase 2 — ELRS link (bench, no actuators connected)

- [ ] Flash/bind **RP1 + ES24TX Pro (+ TX16S backup)** to the **same major.minor ELRS version
      and the same bind phrase**.
- [ ] **Set the RP1 failsafe mode to "No Pulses"** (review finding A8 — "Set Position" would
      keep sending hold frames and defeat the firmware's frame-timeout failsafe).
- [ ] Serial-dump the RP1 CRSF output: confirm **420000 baud, 8N1, NOT inverted**, sync 0xC8,
      RC_CHANNELS_PACKED (0x16) frames arriving.
- [ ] **Characterize link loss** (D4 assumptions): power the TX off and capture — the LQ=0
      LINK_STATISTICS burst on disconnect (count + timing), the ~100 ms stats cadence while
      connected, the disconnect-declaration latency at your packet rate, and that the RX emits
      **no RC frames before first connection**. Note anything that diverges from the D4 design.

## Phase 3 — Control board, actuators DISCONNECTED

> **Stop — first USB / first flash of this bring-up.** This is Phase B: re-check the
> Phase -1 gate one more time. The delivered gift firmware is plain **`esp32dev`** (no
> console); the bench build below adds a console for calibration only and is never what
> ships (Phase 11a).

- [ ] Firmware to flash for the bench: **`pio run -e esp32dev_tuning -t upload`** on the
      control board (adds the serial tuning console — `steer.min`/`steer.max` endpoints,
      `steer.center`/`steer.trim`, `batt.ppt`, `gimbal.decay`, `sound.profile`/`sound.volume`,
      gear table, `save`). On the serial console you should see the tuning banner + a
      boot-safe state. **ESC signal disconnected, servos disconnected, wheels off ground.**
- [ ] Confirm CRSF reception: `status` on the console shows the decoded channels updating as you
      move sticks/switches on the TX.
- [ ] Confirm **boot is safe** (A1 regression, live): at power-on with the TX **off**, the
      board must sit in failsafe — no spurious "active." (This is the bug that used to slam
      steering to full-lock; verify it's gone on real hardware.)
- [ ] **Know what `save` does to the link.** The NVS commit runs INLINE on the control tick:
      the flash write disables the flash cache, and the CRSF UART ISR was never confirmed to be
      IRAM-resident under the pinned core, so RX may go deaf for the duration of the write (the
      128-byte FIFO covers only ~3 ms at 420 kbaud). A dropped frame or two around `saved` is
      expected; a write long enough to pass the 500 ms failsafe timeout would show as a
      failsafe blip, which is the **safe** direction, not a fault. Save only while
      **DISARMED** — the console already refuses otherwise. Nothing has measured this yet
      (`lib/console/src/ConsoleRunner.cpp`, finding timing-3, carried UNVERIFIED), so record
      what you actually see: this bullet is the only place that observation gets made.

## Phase 3b — Boot-mode selector (SP3T, GPIO27/GPIO32) — skip if not wired

> **Stop — BT1 must be open. This phase IS BT1's first item, not a precondition BT1 waits
> on.** Flashing a `W17_BT_SHOWOFF`-family image and booting the SOLO position brings up the
> pad stack (`btPadSource.begin()`, `src/main.cpp:813-815`) — that is BT code running on
> powered hardware, which `BT1_BENCH_GATE.md`'s own gate line forbids before BT1 is opened by
> the owner. Confirm `BT1_BENCH_GATE.md`'s gate (A2 closed, Phase B approved) before flashing
> anything below, the same as every other first-flash step in this document. **Actuators stay
> disconnected** here exactly as in Phase 3 — ESC signal disconnected, servos disconnected,
> wheels off ground — boot mode changes nothing about that.

**Applies only if the §S4c selector from the A2 checklist is physically wired AND you are
bench-testing a `W17_BT_SHOWOFF`-family image** (`esp32dev_btshowoff` or `esp32dev_simbt`).
The three delivery-lineage builds (`esp32dev`, `esp32dev_tuning`, `esp32dev_sim`) never read
GPIO27/32 — they resolve to Drive at compile time (`src/main.cpp:147-157`) — so on those
builds this phase is **N/A by design**, not skipped for lack of time; record it that way
rather than leaving it blank. Full bench-gate context (pairing, RAM/flash budget, the
reconnect-without-input probe): `BT1_BENCH_GATE.md` — this phase is BT1's boot-mode slice,
not a substitute for the rest of it.

- [ ] **Flash `esp32dev_btshowoff`** (real Bluepad32/BTstack pad; needs the physical switch)
      to exercise the selector below. **`esp32dev_simbt` cannot be used for this step:** under
      `-DW17_SIM_PAD_FEEDER` the strap read is skipped entirely and `g_bootMode` is forced to
      `BtSolo` unconditionally (`src/main.cpp:711-715`) — it boots BT_SOLO no matter what the
      selector reads. That makes it useful for exercising BT_SOLO pad/arm-gate wiring with no
      Bluetooth hardware at all (see `BT1_BENCH_GATE.md`), but it proves nothing about the
      selector itself.
- [ ] **Selector at CENTER (LAPTOP):** boot resolves to **Drive**, byte-identical to a plain
      `esp32dev` boot (`BootMode.hpp` center = both straps open on internal pull-ups →
      `StrapReading::DrivePosition` → Drive). CRSF UART opens normally; console (if the
      `_tuning`-style build is used) behaves as always.
- [ ] **Selector at SOLO (GPIO27 grounded, GPIO32 open):** boot resolves to **BtSolo**. Confirm
      the CRSF UART is **never opened** (`crsfUart.begin()` is skipped — `src/main.cpp:770-772`,
      `if (g_bootMode != bootmode::BootMode::BtSolo) { crsfUart.begin(); }`) — the RP1 stays
      powered but nothing reads it. Arming requires the pad ritual (L1+R1 hold or OPTIONS per
      `docs/bt_showoff_design.md` §3), not the CRSF arm switch.
- [ ] **Selector at SHOW (GPIO32 grounded, GPIO27 open):** boot resolves to **Showcase**.
      Confirm the car **cannot arm by any input** — `armSwitchInput()` structurally returns
      `false` in Showcase (`BootMode.hpp` Policy 1), so the arm gate's neutral-seen latch can
      never set and the ESC never leaves neutral, regardless of what the handset or pad sends.
- [ ] **Ground both throws simultaneously with jumper wires (bypassing the switch itself) →
      must resolve to Drive.** This is the both-grounded harness-fault case the SP3T part
      cannot produce on its own (`combineStrapPins()`, `BootMode.hpp:125-136` — the
      fail-toward-Drive rule) and is only reproducible by wiring around the part. If it
      resolves to anything else, this is a bench finding, not a documentation gap — stop and
      report rather than proceeding to Phase 4.
- [ ] **From SOLO or SHOW, disconnect the GROUNDED throw's wire and re-boot → must resolve to
      Drive.** Removing the ground lets that pin's pull-up return it to Open, so both pins now
      read Open (`combineStrapPins(Open, Open)` → `DrivePosition`, `BootMode.hpp:125-136`). If
      it does not, stop and report as above.
- [ ] **Disconnecting the IDLE throw's wire changes nothing, by design.** That pin was already
      Open (floating on its pull-up, not grounded) before the disconnect, so removing its wire
      leaves the reading — and the resolved mode — exactly as it was. This is not a fault case
      and is not gated on any particular outcome; do not treat "it didn't switch to Drive" as a
      finding here.
- [ ] Record which of SOLO/SHOW physically corresponds to which slider throw (this is what A2's
      §S4c note calls "the labels" — A2 proves the wires, this phase proves what they mean).

## Phase 4 — Channel map + switch thresholds

- [ ] Confirm the `ChannelMapConfig` defaults in `lib/channels/include/channels/ChannelDecoder.hpp` match your
      **actual TX mapping**: steering ch1, throttle ch3, arm ch5, DRS ch6, gearUp ch7, gearDown
      ch8, boost ch11, overtake ch12, drive-mode ch13. Remap in that header + reflash if needed.
- [ ] Every 2-pos switch **crosses both hysteresis thresholds (±250)** — *especially the ARM
      switch's OFF direction* (a TX mix that never goes below −250 makes ARM impossible to turn
      off). Watch `status` as you flip each.
- [ ] Drive-mode 3-pos hits all three detents → mode 0 (Training) / 1 (Gearbox) / 2 (ERS).

## Phase 5 — Failsafe + arm gate PROOF (still NO motor power) — THE GATE

Do not power the ESC until every box here passes. **Re-arm rule below is the 2026-08-20
OWNER-RATIFIED invariant** — a failsafe episode latches a disarm that a fresh neutral alone
cannot clear; the contract lives in `lib/channels/include/channels/ArmGate.hpp`, and
`docs/SIMULATION.md`'s `RECOVERY_1_LATCHED`/`RECOVERY_1_TOGGLE` narrative (§"Timeline",
~:61-64) is the model to compare your bench observations against.

- [ ] Arm switch **OFF** → throttle output stays neutral regardless of stick.
- [ ] Arm switch **ON with throttle already high** → still neutral (no arm-into-throttle);
      only after throttle returns to neutral does it arm.
- [ ] **Boot with the arm switch already ON** → stays disarmed even once throttle is seen at
      neutral; the FSM boots Safe, so this is itself a failsafe episode with the switch ON —
      it requires the same OFF→ON toggle as recovery, below, before it will arm.
- [ ] **TX off mid-"drive"** → failsafe: steering centers, throttle neutral, DRS closed.
- [ ] **Recovery with the arm switch left ON through the whole episode** → link back, stick
      centered, switch still ON → **MUST stay disarmed**. A fresh neutral alone does **not**
      re-arm; this is the invariant this phase exists to prove, not an edge case.
- [ ] **Recovery re-arm, the only path that works:** switch **OFF, then ON** (the toggle) with
      the stick centered → arms. Confirm arming will not complete on the toggle if the stick is
      still displaced at the ON edge — it still waits for a fresh neutral after the toggle.
- [ ] Hold-position case (if reproducible): LQ=0 while frames still arrive → still drops to safe.

## Phase 6 — Steering servo

- [ ] **Center the servo in firmware BEFORE attaching the linkage** (atlas MECH-02) — power up
      disarmed, servo sits at center; *then* fit the tie-rod so the wheels are straight at
      neutral.
- [ ] Trim toe/center over the console: `set steer.center <us>`, `set steer.trim <±us>`, then
      `save`. (The firmware rejects a trim that would push center past an endpoint — A11.)
- [ ] **Calibrate the travel endpoints** over the console: `set steer.min <us>` /
      `set steer.max <us>`. Work **conservatively from center outward** — start well inside
      (e.g. center ±200 µs), steer to full lock, and widen an endpoint in small steps only
      while the linkage moves freely. **Stop at the first sign of mechanical binding or servo
      stall/buzz against the stops** and back the endpoint off. The console rejects any
      endpoint outside the absolute 500–2500 µs window, out of order with the other endpoint,
      or that would exclude center (or center+trim) — a rejected `set` leaves everything
      unchanged.
- [ ] `save`, power-cycle, confirm `get steer.min` / `get steer.max` read back the calibrated
      values, then re-check full left/right doesn't bind the linkage or stall the servo.
      The final endpoint numbers are **hardware-calibration evidence for this specific car**
      (record them in the bring-up log) — not values the software can prove safe.

## Phase 7 — ESC + motor (wheels OFF the ground)

- [ ] ESC configured in **sensored mode** + **forward/brake** (NOT forward/reverse — the gearbox
      doesn't govern reverse; brake/reverse PWM is indistinguishable below neutral). Motor
      sensor cable plugged.
- [ ] ESC neutral/range calibration per its manual (it's the ESC's own config; firmware just
      emits 1000–2000 µs, neutral 1500).
- [ ] Power-on arm sequence: firmware holds neutral ~2 s before accepting throttle; ESC arms.
- [ ] Gears cap throttle (Gearbox): low gear gentle, top gear full; shift up/down on ch7/ch8.
      Tune the feel later via `set gear.<N>.max` / `gear.<N>.expo` + `save`.
- [ ] Brake: stick below neutral brakes; brake flag/light triggers (see Phase 9).
- [ ] ERS (mode 2): boost/overtake raise the ceiling and drain the store; harvest recharges
      while braking/coasting with the wheels turning.
- [ ] Only after all the above feel right and safe: wheels on the ground.

## Phase 7b — Camera gimbal (right stick)

- [ ] In **elrs-joystick-control**, map the **right DualShock stick** X → ch9, Y → ch10 (the
      right stick is otherwise unused; steering is the left stick).
- [ ] Fit the two MG90S to the camera pod; power up disarmed — they center. Move the right
      stick: pan (GPIO19) and tilt (GPIO23) follow. Flip `invertPan`/`invertTilt` in
      `ChannelMapConfig` + reflash if an axis is backwards.
- [ ] On a link drop (TX off) the camera **glides to center over ~2 s** — a smooth ramp,
      not a snap and not a hold (vision decision 11; rate is the `gimbal.decay` tunable,
      default 2000 ms full-deflection-to-center). On link recovery it glides back to
      wherever the right stick is aiming now, again without a snap. Aiming stays allowed
      while disarmed. Confirm the pod doesn't bind at the travel extremes.

## Phase 8 — Telemetry sensors

- [ ] **Battery ADC two-point calibration**: measure real pack voltage with a multimeter at
      ~6.5 V and ~8.4 V, compare to the console's reading, set `batt.ppt` to correct, `save`.
      Log which eFuse cal type the ADC reports (default-Vref fallback = worse accuracy).
- [ ] **Hall wheel-speed**: spin the axle by hand → rpm reads sane; then at full throttle
      **scope the Hall line near the motor** for EMI double-counts. Add 1–10 nF across the
      sensor output if the edge is ugly (the 2 ms ISR lockout absorbs mild bounce).
- [ ] **Hall interrupt-rate margin, full-throttle half (OD-11; continues
      `PHASE_B_FIRST_POWER.md` B4.4's motor-free half):** with the ESC configured and the motor
      connected (Phase 7's rule: **wheels still off the ground**), log
      `hallSensor.isrEntries()`, `lastWindowEntries()`, and `guardFaults()` — the bench
      console's `status` line prints all three — over a full-throttle pass, then repeat with
      the GPIO35 10 kΩ pull-up lifted (scope the pin). Record entries per 100 ms in normal
      running (expect ≲ 9) and confirm it sits far below the guard's 180 entries/100 ms bound
      (20× the 5000 rpm maximum); if it is ever approached, confirm the fault path (detach →
      rpm 0 → re-arm after 1 s) behaves as the native tests describe. A window that ran more
      than 10× long is judged only when it carries ≥ 10× the elapsed-scaled allowance (a 2 s
      stall: 36 000 entries). `[bench-TBD]` — nothing has measured this yet.
- [ ] **No-hardware cross-check for the HUD's low-battery path:** on the ground station,
      `npm run demo:low-battery` (`w17-ground-station/scripts/run.js --demo-low-battery`,
      `W17_REPLAY_TIMELINE=low-battery`) replays a scripted low-battery timeline without a
      car or a pack attached — use it to confirm the HUD's warning UI *before* trusting a
      real low reading off the divider above, so a HUD bug and a hardware defect are never
      diagnosed as the same thing.

## Phase 9 — link2 → board #2 (sound + light)

**Two-board flash order and NVS-migration detail:** `COORDINATED_FLASH.md`. That document is
the full procedure (why order matters, what a version mismatch looks like, the settings keys
that must survive the flash); the steps below are the on-the-bench summary.

- [ ] Flash `w17-soundlight-fw`. Wire ESP32 #1 TX (GPIO25) → ESP32 #2 RX (GPIO16), **common
      ground**, 115200 8N1.
- [ ] MAX98357A: GAIN strap (start 9 dB floating), speaker connected. Engine sound rises with
      throttle; gear shifts + ERS whine audible.
- [ ] WS2812: brake bar on braking; indicators blink from steering; halo teal armed;
      **failsafe → all-amber hazard blink**; rain light flashes while ERS is harvesting.
- [ ] **Cut the UART mid-run → board #2 goes to its own local failsafe within 500 ms** (engine
      to idle/off, hazard blink). This is the protocol's mandatory receiver rule.

## Phase 10 — Ground station (Windows)

- [ ] **#1 RISK — camera codec:** check `majestic.yaml` `.video0.codec`. **Chromium WebRTC
      generally can't decode H.265** — reconfigure the SSC338Q to **H.264** if possible, else
      transcode H.265→H.264 in mediamtx/ffmpeg. (VLC decodes H.265, so the fallback survives it.)
- [ ] Grab the real camera RTSP URL from `majestic_fpv.yaml` → set it in `mediamtx/mediamtx.yml`
      `paths.cam.source`. `npm run setup`, then `npm start`; confirm WHEP video in the HUD.
- [ ] **Zero-code fallback works:** elrs-joystick-control + VLC on the raw stream.
- [ ] **Telemetry (battery + LQ):** the FT232 CRSF port is held exclusively by
      elrs-joystick-control. Either confirm it forwards telemetry (a flag), or install
      **com0com/hub4com** to mirror the port; set `W17_TELEMETRY_SOURCE=crsf-serial
      W17_TELEMETRY_PORT=<reader>`; `npx electron-rebuild` for serialport. Battery + LQ go live
      on the HUD; speed/gear/ERS stay gamepad-simulated by design.

## Phase 11 — On the car

- [ ] Mount both boards, camera, battery centrally (mass balance). Re-confirm Phase 5 on the car.
- [ ] Short low-gear shakedown, then open it up. Re-trim steering + gear feel over the console
      as needed and `save`.
- [ ] **Before gifting:** run the delivery hand-off below to move from the bench build to the
      console-free delivery firmware **without losing the calibration.**

### Phase 11a — Delivery hand-off (calibrate on tuning → ship on plain `esp32dev`)

This is the **single canonical delivery procedure** — don't duplicate it elsewhere; other docs
point here. It works because **both** the tuning and the delivery builds load the *same* NVS
blob through the *same* validated loader (`settings::loadOrDefault`: length → CRC → version →
`Settings::valid()`; any failure ⇒ complete compiled defaults). The tuning build additionally
opens a UART0 console that can **change / save / reset** that blob; the delivery build only
**reads** it and carries no console/command surface at all.

1. **Flash the bench build:** `pio run -e esp32dev_tuning -t upload`.
2. **Calibrate** (Phases 6/7b/8/9): `set steer.min`/`steer.max` (travel endpoints),
   `set steer.center`/`steer.trim`, `set batt.ppt` (two-point ADC cal),
   `set gear.<N>.max`/`gear.<N>.expo`, `set gimbal.decay` (link-loss camera-center rate,
   ms full-deflection-to-center, default 2000 — Phase 7b), `set sound.profile` (0=V10,
   1=V6) and `set sound.volume` (0–100, the giftee's shipped volume preset) — all only
   while **DISARMED**. The last two are board #1 settings carried to board #2 over link2,
   not board #2 settings — there is nothing to calibrate on the sound/light board itself.
3. **Save to NVS:** `save` (must print `saved`). `set` alone is RAM-only until this.
4. **Read back the final values:** run `get steer.min`, `get steer.max`, `get steer.center`,
   `get steer.trim`, `get batt.ppt`, `get gimbal.decay`, `get sound.profile`,
   `get sound.volume`, and `get gear.<N>.max` / `get gear.<N>.expo` for each gear. `status` is a
   useful quick sanity read but is **not** a substitute for the per-gear `get`s: it prints a
   7-line snapshot (`lib/console/src/Console.cpp:132-146`) that includes only gear 1
   (`g1.max`/`g1.expo`) — the per-gear `get`s above are still required for the authoritative
   multi-gear record.
5. **Record those `get` values in the bring-up evidence** (A2 / Phase-B log) as the calibrated
   set — this is the authoritative record of what the car shipped with, **including the
   `sound.volume` the giftee will hear at first power-on**, not only the steering/battery
   numbers.
6. **Reboot the tuning build** (power-cycle) and confirm the banner prints
   `[tune] loaded settings from flash` and `get` shows the same values — proves the blob
   round-trips from NVS.
7. **Flash plain delivery firmware:** `pio run -e esp32dev -t upload`. (No `-D` flags; no
   console; UART0 stays closed.)
   - **ELF spot-check — console-free AND BT-free (turns the invariant from asserted into
     verified):** on the freshly built delivery ELF, run
     `xtensa-esp32-elf-nm -C .pio/build/esp32dev/firmware.elf | grep -c -E "console::|btpad|luepad|btstack"`
     — it must print `0`. `console::` proves no tuning-command surface shipped; the
     `btpad`/`luepad`/`btstack` patterns prove no BT show-off code shipped (that prototype
     exists only in `env:esp32dev_btshowoff` and its custom core — never in a delivery flash;
     `docs/bt_showoff_design.md` §2.1). Positive control if the check ever looks too quiet:
     the same command against the `esp32dev_tuning` ELF must report a non-zero `console::`
     count.
     > **`OWNER-DECISION(SHIP-IMAGE)` — ruled 2026-09-03 (OD-2).** The ship image is **plain
     > `esp32dev` by default**; upgrading to `esp32dev_btshowoff` is conditional on
     > **BT1 (`BT1_BENCH_GATE.md`) having PASSED before handover** — not a free choice made at
     > flash time. Until BT1 passes, this step's single combined `0` spot-check is the correct
     > and only pass condition: today's delivery build is both console-free and BT-free by
     > construction (measured at this revision, R6: `esp32dev` RAM 7.0%/22948 B, Flash
     > 23.2%/304013 B of the default 1.25 MB app slot; `esp32dev_btshowoff` RAM 26.6%/87076 B,
     > Flash 23.6%/742305 B of the 3 MB `huge_app.csv` slot it requires — `pio run -e <env>`,
     > re-run this revision if the code has changed). If the SP3T selector from A2 §S4c is
     > wired into the delivered car but the ship image stays `esp32dev` (the OD-2 default), the
     > switch is **electrically inert** — that build never reads GPIO27/32
     > (`src/main.cpp:147-157`) — **by design under OD-2**, not an oversight to flag.
     >
     > **If BT1 passes before handover and the owner exercises the upgrade path:** re-run this
     > step against `esp32dev_btshowoff` instead of `esp32dev`. The single combined grep above
     > becomes **the wrong check** there — `btpad`/`luepad`/`btstack` are then *expected*
     > non-zero by design — so split into two independent assertions: `console::` count
     > **must** still be `0` (verified this revision: `esp32dev_btshowoff`'s own `console::`
     > count is `0`, since `W17_TUNING_CONSOLE` is a separate flag never set in that env) and
     > the BT-pattern count, expected **non-zero**, becomes the new positive control (`216`
     > combined / `btpad` alone `27` this revision — `BT1_BENCH_GATE.md`'s resource table). The
     > RAM/flash budget above and the `huge_app.csv` no-OTA-slot trade-off then become the
     > shipped car's real numbers, not a prototype's. **Record the BT1 PASS date and evidence
     > here** before switching the ship image in practice — OD-2 conditions the upgrade on that
     > evidence existing, not on the owner's say-so alone.
8. **Verify the tuning is still live on the plain build** — the delivery firmware loaded the
   NVS blob at boot: steering sits at the trimmed center, the battery reading matches the
   calibrated `batt.ppt`, the gears feel as tuned (low gear gentle, top gear full), the
   camera glides to center at the calibrated `gimbal.decay` rate on a link drop, and the
   engine voice/volume over link2 match the recorded `sound.profile`/`sound.volume`. If the
   blob were missing/corrupt it would silently fall back to compiled defaults, so a match here
   confirms the load path worked.
9. **Re-run the safe-state checks (Phase 5) on the delivery firmware:** TX-off boot sits in
   failsafe (no phantom "active"); arm gate holds throttle neutral until arm-ON *and* a fresh
   neutral; boot with the arm switch already ON stays disarmed until one OFF→ON toggle;
   mid-run TX-off → failsafe; **recovery with the switch left ON stays disarmed — only an
   OFF→ON toggle with the stick centered re-arms** (the 2026-08-20 episode-latch invariant,
   `lib/channels/include/channels/ArmGate.hpp`; a fresh neutral alone is **not** sufficient
   and must not appear to work). These are unchanged by tuning and must pass on the shipped
   build.

**Reset to defaults / rollback (keep this on the delivery card):**

- **Wipe the calibration back to compiled defaults:** flash `esp32dev_tuning`, `reset` (RAM
  only) then `save` (writes the defaults blob), or clear the NVS namespace (`w17tune`) with an
  erase. On the next boot the loader then falls back to compiled defaults on **any** build.
  (Note: the delivery `esp32dev` build itself has **no** way to reset or save — that is
  deliberate; rolling back tuning requires temporarily returning to `esp32dev_tuning`.)
- **Return to the tuning environment at any time:** re-flash `esp32dev_tuning` — it reads the
  same NVS blob, so the car comes back up on its saved calibration with the console re-enabled;
  re-tune and `save`, then repeat steps 7–9 to ship again.
- **Corrupt/undervalidated blob is self-healing:** if the stored blob ever fails the guard
  chain, every build boots on complete compiled defaults rather than a partial/mixed config —
  the car is never bricked or left half-tuned by a bad NVS state.

---

### Deferred / optional (won't block the gift)
MSP telemetry for speed/gear/ERS over the radio, code-signing the ground-station .exe. See
`docs/ROADMAP.md`. (Camera gimbal pan/tilt is wired and bench-tested in Phase 7b above — it is
no longer deferred.)

### Cross-repo pointers
- Control firmware + this runbook: `w17-control-fw`.
- Sound/light: `w17-soundlight-fw` (its `docs/SIMULATION.md` bench notes).
- Ground station: `w17-ground-station` (`docs/SETUP.md` codec/mediamtx, `docs/TELEMETRY.md`
  the com0com/COM-sharing detail).
- Standalone first-power sequencing (link order, "logic only" scope, stop conditions):
  `PHASE_B_FIRST_POWER.md`.
- Two-board coordinated flash + NVS migration detail: `COORDINATED_FLASH.md`.
- BT show-off / showcase bench gate (BT1 — pairing, RAM/flash budget, reconnect-without-input
  probe): `BT1_BENCH_GATE.md`.
