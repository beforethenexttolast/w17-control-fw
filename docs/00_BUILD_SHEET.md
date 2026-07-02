# W17 FPV RC F1 — Build Sheet (one-pager)

**Locked config:** RC-01 chassis · **oil-shock** suspension (original floor + arms) · **2024 body** (Mercedes W17 livery) · belt drive, sensored 17.5T · F104 rubber tyres on **printed** rims · 2× ESP32 (control + sound/light) · OpenIPC FPV + ELRS. Detail in runbooks 02–06.

---

## PRINT PLAN — order, filament, settings
**Golden rule:** orient each stressed part so load runs *along* the layers, not across. 0.2 mm / 0.4 mm nozzle / gyroid default (rectilinear only at 100%).

| # | Print | Filament | Settings |
|---|---|---|---|
| **1 — fit test (first!)** | 1× F104 tyreslot + 1× front hub | ASA | 100% rect, 4–5 walls → **measure the 8×12×3.5 bearing seat + F104 tyre fit before printing the rest** |
| **2 — chassis structural** *(print while parts ship)* | Suspension Block_10, Arm4, Steering Block4, Crossarm3, GuideRod, servosaverv7; Left/Rightrearaxle, beltdrivemotorlock, RearSpringMountREV4, springblock; wheel hubs; **all 4 F104 tyreslots**; Servoholder; long-axle spacers | **ASA** (no PLA here — heat) | 0.2 mm, **100% rectilinear, 4–5 walls**, enclosed + **ventilated** (styrene) |
| **3 — floor** *(while parts ship)* | Front floor, Back floor (+Part2), Floorboard, Diffuser, side vents | **PETG** (or Tough PLA if indoor-only) | 0.2 mm, 40–60% gyroid |
| **4 — body** *(start early — paint is the long pole)* | 2024 shell (front+rear), nose, 2024 front wing, **halo 2.1**, mirrors, **camera-top 1.1** pod, **rear-light diffuser**, sharkfin | **PLA, BLACK** (W17 base); silver-nose piece in **white/grey** | **0.12–0.16 mm** visible, 20–40% gyroid; **prime all visible** |
| **5 — functional/last** | DRS arm (2023 servo arm, match to rear wing), brackets, `wall_mount` display | PETG (DRS arm) / any | 0.2 mm |

*Hybrid-rear option: if you confirmed the revised rocker seats your 68 mm coilover, print `Rear_*_Motor_Cover_REVISION_1` instead of the original holders (sturdier, still damped).*

---

## PACKING LAYOUT (measured)
- **Central floor tub (~84 × 60 mm, flat):** battery (≤75 mm) forward → freed length behind for boards.
- **Rear engine-cover cavity (~50 × 40 mm + 70 mm spine):** **two ESP32s stacked**.
- **Sidepod pockets (×2):** BEC · MAX98357A amp · RP1 RX.
- **Nose / airbox:** OpenIPC camera (in resized camera-top pod).  **Sidepod:** speaker.
- **Rails:** A (clean) = camera + WiFi + 2× ESP32 + RX + LEDs; B = steering + 3× MG90S. **All grounds common.**

---

## BENCH FIXES — before first power-on (all confirmed)
1. **ESC throttle RED wire isolated** at ESP32 #1 (6 V BEC back-feed = ESC damage). Signal + GND only.
2. **Battery divider 27k/10k** (not 20k/10k — 20k clips the top half of the 2S range).
3. **A3144 VCC = 5 V**, open-collector output pulled to **3.3 V**.
4. **WS2812:** 1N5819 series diode on strip VDD (→ ~3.0 V threshold) *or* 74AHCT125 shifter.
5. **1000 µF cap** on the servo rail (DS3235SG stall spikes).
6. **ELRS:** flash RP1 + ES24TX Pro + TX16S module to the **same major.minor + same bind phrase**.
7. **ESC in sensored mode**, motor sensor cable plugged (the smooth-low-speed reason you chose it).

---

## SORT BEFORE BUILD WEEK
- ☐ **Shop stocks ASA** (un-substitutable for axle parts; else any high-temp filament).
- ☐ **F104 tyres = front + rear SET** (narrow front, wide rear) + tyre glue; glue to printed rims.
- ☐ Spur ↔ belt-pulley bolt pattern matches; ESC is the **Sensored** 10BL120 (HW30125002).
- ☐ Battery **≤75 × 45 × 25 mm** (2024 body) — **carry 2** for runtime.
- ☐ TX16S internal module type (4-in-1 vs ELRS) → whether a 2nd ELRS module is needed.

## FINISH (gift)
Prime visible (grey; **white** under silver nose + Petronas turquoise) → cured paint → **gloss clear** → decals → final clear (matte/satin) → cure → assemble.
**Logos / F1 number font:** apply from official/licensed sources — not reproduced here.
