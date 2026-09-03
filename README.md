# w17-control-fw

Firmware for **ESP32 #1 "Control"** of the 1/10 FPV Mercedes W17 RC car — takes the ELRS/CRSF
radio link in, runs failsafe + arm gate + virtual gearbox + ERS, drives the steering servo /
ESC / DRS / camera-gimbal servos, and streams vehicle state to ESP32 #2 (sound + light) over
the one-way **link2** UART. Companion repos: `w17-soundlight-fw`, `w17-ground-station`.

Full brief: `CLAUDE.md`. Bench bring-up: `docs/D8_BENCH_BRINGUP.md`. Pin map:
`lib/config/include/config/PinMap.hpp`.

## Build environments

| env | purpose |
|---|---|
| **`esp32dev`** | **the real / gift firmware** (default). Loads validated NVS tuning at boot; no serial console, no sim feeder, cannot change tuning. |
| `esp32dev_sim` | `esp32dev` + `-DW17_SIM_CRSF_FEEDER` — scripted CRSF self-feed for the Wokwi Stage-2 sim (also loads NVS tuning). |
| `esp32dev_tuning` | `esp32dev` + `-DW17_TUNING_CONSOLE` — adds a UART0 serial console that can **change/save/reset** the same NVS tuning, used for bench bring-up. |
| `esp32dev_btshowoff` | `-DW17_BT_SHOWOFF`, real Bluepad32 stack + custom core (`platform_packages`), `huge_app` partitions — the BT show-off **prototype, never a delivery target** (`docs/bt_showoff_design.md`). Only env that wires and reads the GPIO27/32 boot-mode strap for real. |
| `esp32dev_simbt` | `esp32dev` + `-DW17_BT_SHOWOFF -DW17_SIM_PAD_FEEDER` on the **stock pinned core** (`platformio.ini:96-101` — it `extends = env:esp32dev`, not `esp32dev_btshowoff`, so it inherits `esp32dev`'s `lib_ignore = btpad_hal_esp32` and gets none of `esp32dev_btshowoff`'s custom core / `huge_app.csv` / re-enabled lib), scripted `SimPadFeeder` session instead of real Bluepad32. **Not** three-mode: under `-DW17_SIM_PAD_FEEDER` the boot-mode strap read is skipped entirely and `g_bootMode` is forced to `BtSolo` unconditionally (`src/main.cpp:711-715`) — useful for exercising BT_SOLO pad/arm-gate wiring with no Bluetooth hardware at all, never for the boot-mode selector. |
| `native` | host Unity unit tests over the pure-logic libs (no hardware). |

**Boot modes:** three modes (Drive / Solo / Showcase), resolved once at boot from a physical
selector and never switched at runtime — see `lib/bootmode/include/bootmode/BootMode.hpp` for
the resolution logic and `lib/config/include/config/PinMap.hpp:36-56` for the GPIO27 (Solo) /
GPIO32 (Show) strap pins. Every delivery-lineage env (`esp32dev`, `esp32dev_tuning`,
`esp32dev_sim`) hardcodes a Floating reading at compile time and always resolves to Drive.
Only `esp32dev_btshowoff` reads the strap pins for real; `esp32dev_simbt` never reads them
either — it forces `BtSolo` unconditionally under `-DW17_SIM_PAD_FEEDER`
(`src/main.cpp:711-715`), skipping the strap read entirely.

## Build / test / flash

```bash
pio test -e native          # unit tests, no hardware
pio run  -e esp32dev        # compile the gift firmware
pio run  -e esp32dev -t upload   # compile + flash over USB
pio device monitor -b 115200     # serial monitor (monitor_speed = 115200)
```

Flash a specific env with `-e <env>` (e.g. `pio run -e esp32dev_tuning -t upload` for the
bench build). The bench firmware is `esp32dev_tuning`; board #2 lives in `w17-soundlight-fw`.

### Serial port

PlatformIO auto-detects the port in the common case. On a multi-adapter bench (e.g. an FT232
for CRSF plus the ESP32's own USB), select it explicitly:

- **macOS:** `/dev/tty.usbserial-XXXX` (or `/dev/cu.usbserial-XXXX`) — `pio device list` to find it.
- **Windows:** `COMx` — check Device Manager.
- Set with `--upload-port <port>` / `--monitor-port <port>`, or `upload_port`/`monitor_port` in
  `platformio.ini`.

You may need the USB-UART **driver** for the DevKit's bridge chip: **CP210x** (Silicon Labs) or
**CH340** (WCH), depending on the board.

## ⚠️ The gift ships plain `esp32dev` — never `_sim` or `_tuning`

The delivered car must be flashed with **`esp32dev`**. `esp32dev_sim` compiles in a fake CRSF
feeder (the car would ignore the real radio), and `esp32dev_tuning` opens a UART0 console
surface that has no place on a gift. After any bench tuning on `esp32dev_tuning`, **reflash
plain `esp32dev` before delivery** (D8 Phase 11). Plain `esp32dev` **loads the validated
NVS-saved tuning at boot** (steering trim, battery calibration, gear feel) through the same
guard chain the bench build uses — so the calibrated car keeps its tuning — but it exposes **no
console** to change, save, or reset it. If the stored blob is missing or fails validation, it
falls back to the complete compiled defaults. Full delivery / reset / rollback procedure:
**`docs/D8_BENCH_BRINGUP.md` Phase 11** (the single canonical home for that runbook).
