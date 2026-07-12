# W17 — Bench Bring-Up Runbook (Stage 3 / D8)

The single ordered checklist to take the firmware from "builds + passes tests" to "driving on
the car," once hardware arrives. **Do the phases in order** — each is a safety/dependency gate
for the next. The golden rule threads through all of it: **wheels off the ground, and no ESC
power, until the failsafe + arm gate are proven live (Phase 5).**

Firmware to flash for the bench: **`pio run -e esp32dev_tuning -t upload`** on the control board
(adds the serial tuning console — `steer.min`/`steer.max` endpoints, `steer.center`/`steer.trim`,
`batt.ppt`, gear table, `save`). The delivered
gift firmware is plain **`esp32dev`** (no console). Sound/light board: `w17-soundlight-fw`
`esp32dev`. Ground station: `w17-ground-station` (`npm run demo` until the camera is wired).

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

- [ ] Flash `esp32dev_tuning`. On the serial console you should see the tuning banner + a
      boot-safe state. **ESC signal disconnected, servos disconnected, wheels off ground.**
- [ ] Confirm CRSF reception: `status` on the console shows the decoded channels updating as you
      move sticks/switches on the TX.
- [ ] Confirm **boot is safe** (A1 regression, live): at power-on with the TX **off**, the
      board must sit in failsafe — no spurious "active." (This is the bug that used to slam
      steering to full-lock; verify it's gone on real hardware.)

## Phase 4 — Channel map + switch thresholds

- [ ] Confirm the `ChannelMapConfig` defaults in `lib/channels/ChannelDecoder.hpp` match your
      **actual TX mapping**: steering ch1, throttle ch3, arm ch5, DRS ch6, gearUp ch7, gearDown
      ch8, boost ch11, overtake ch12, drive-mode ch13. Remap in that header + reflash if needed.
- [ ] Every 2-pos switch **crosses both hysteresis thresholds (±250)** — *especially the ARM
      switch's OFF direction* (a TX mix that never goes below −250 makes ARM impossible to turn
      off). Watch `status` as you flip each.
- [ ] Drive-mode 3-pos hits all three detents → mode 0 (Training) / 1 (Gearbox) / 2 (ERS).

## Phase 5 — Failsafe + arm gate PROOF (still NO motor power) — THE GATE

Do not power the ESC until every box here passes.

- [ ] Arm switch **OFF** → throttle output stays neutral regardless of stick.
- [ ] Arm switch **ON with throttle already high** → still neutral (no arm-into-throttle);
      only after throttle returns to neutral does it arm.
- [ ] **TX off mid-"drive"** → failsafe: steering centers, throttle neutral, DRS closed.
- [ ] **Recovery** → does NOT resume throttle until the stick is centered again (fresh-neutral).
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
- [ ] Camera holds its last aim on a link drop (not safety-gated; `controls` freezes on
      failsafe). Confirm the pod doesn't bind at the travel extremes.

## Phase 8 — Telemetry sensors

- [ ] **Battery ADC two-point calibration**: measure real pack voltage with a multimeter at
      ~6.5 V and ~8.4 V, compare to the console's reading, set `batt.ppt` to correct, `save`.
      Log which eFuse cal type the ADC reports (default-Vref fallback = worse accuracy).
- [ ] **Hall wheel-speed**: spin the axle by hand → rpm reads sane; then at full throttle
      **scope the Hall line near the motor** for EMI double-counts. Add 1–10 nF across the
      sensor output if the edge is ugly (the 2 ms ISR lockout absorbs mild bounce).

## Phase 9 — link2 → board #2 (sound + light)

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
2. **Calibrate** (Phases 6/8): `set steer.min`/`steer.max` (travel endpoints),
   `set steer.center`/`steer.trim`, `set batt.ppt` (two-point ADC cal),
   `set gear.<N>.max`/`gear.<N>.expo` — all only while **DISARMED**.
3. **Save to NVS:** `save` (must print `saved`). `set` alone is RAM-only until this.
4. **Read back the final values:** run `get steer.min`, `get steer.max`, `get steer.center`,
   `get steer.trim`, `get batt.ppt`,
   and `get gear.<N>.max` / `get gear.<N>.expo` for each gear (or `status` for the summary).
5. **Record those `get` values in the bring-up evidence** (A2 / Phase-B log) as the calibrated
   set — this is the authoritative record of what the car shipped with.
6. **Reboot the tuning build** (power-cycle) and confirm the banner prints
   `[tune] loaded settings from flash` and `get` shows the same values — proves the blob
   round-trips from NVS.
7. **Flash plain delivery firmware:** `pio run -e esp32dev -t upload`. (No `-D` flags; no
   console; UART0 stays closed.)
8. **Verify the tuning is still live on the plain build** — the delivery firmware loaded the
   NVS blob at boot: steering sits at the trimmed center, the battery reading matches the
   calibrated `batt.ppt`, and the gears feel as tuned (low gear gentle, top gear full). If the
   blob were missing/corrupt it would silently fall back to compiled defaults, so a match here
   confirms the load path worked.
9. **Re-run the safe-state checks (Phase 5) on the delivery firmware:** TX-off boot sits in
   failsafe (no phantom "active"), arm gate holds throttle neutral until arm-ON + fresh
   neutral, mid-run TX-off → failsafe, recovery needs a fresh neutral. These are unchanged by
   tuning and must pass on the shipped build.

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
