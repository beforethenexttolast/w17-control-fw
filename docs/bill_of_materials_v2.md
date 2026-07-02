# W17 FPV RC F1 — Bill of Materials (v2 · current)

Consolidated, verified buy-list for the 1/10 3D-printed FPV **Mercedes W17 (#63 Russell)** build.
Supersedes v1. Prices are approximate UAH snapshots from the saved cart (they fluctuate); links are canonical item pages.

**Status:** verified against the saved AliExpress cart (40 lines parsed) and the detailed parts list. Camera owned + flashed. Nothing printed yet. Gift deadline: **21 Jul 2026**.

---

## Changes since v1 (why this version exists)
- **Battery envelope corrected → ≤75×45×25 mm** (was 115×35×24 mm). The 2024-body decision shrank the pack bay; both v1 files still carried the old 2023-body number.
- **Tyres resolved → genuine Tamiya F104** (54198 front + 51400 rear) on **printed rims**, sourced from **rcMart** — replaces v1's unresolved "1/10 on-road 68 mm / 12 mm-hex" AliExpress wheel line.
- **Camera cooling added** → 5 V blower + thermal paste (these postdated v1).
- **Voltage divider → 27 kΩ / 10 kΩ** (was 20 k/10 k) for a safer, more linear ADC range.
- **Removed** a stray non-build item (tungsten ring) that was sitting in the cart.

---

# A. AliExpress order (ships to UA) — ~17,400 ₴

## 1. Video / FPV
- **BL-M8812EU2 USB WiFi module** `High-Power` ×1 — ~697 ₴ — provides the WiFi AP (camera has no radio)
  https://www.aliexpress.com/item/1005007386940533.html
- **5.8 GHz U.FL linear omni antennas** `70mm / 5pcs` ×1 — ~139 ₴
  https://www.aliexpress.com/item/1005004050120482.html
- **Heatsink 28×28×3 mm** `Black / 2pcs` ×1 — ~147 ₴ — **for the WiFi module** (runs hot); fit before first power-on
  https://www.aliexpress.com/item/1005003237910399.html
- **FT232RL USB-UART** `Type-C` ×1 — ~84 ₴ — camera console/flash + PC↔ELRS-module CRSF link (set 3.3 V jumper)
  https://www.aliexpress.com/item/1005006445462581.html
- *Camera — OpenIPC SSC338Q (MC800S-V3), already flashed to APFPV greg10.2 + tuned (`majestic_fpv.yaml`). Free / owned.*

## 2. Control / Radio
- **RadioMaster RP1 ELRS receiver** `RP1` ×1 — ~855 ₴
  https://www.aliexpress.com/item/1005004862029754.html
- **HappyModel ES24TX Pro TX module** ×1 — ~1,890 ₴ — run at low power (25–100 mW).
  *Cost option: a fanless ~250 mW nano (ES24TX Slim / BetaFPV Micro / Ranger Nano) is cheaper + lighter for the PC route — your files list it as an equal pick.*
  https://www.aliexpress.com/item/1005007477012165.html
- *Transmitters: DualShock (gift-time, via PC) + TX16S (bench/backup) — owned.*

## 3. Drive / ESC / Motor
- **Hobbywing QuicRun 10BL120 + Rocket 540 V3 sensored combo** `17.5T` ×1 — ~5,620 ₴ — **motor included**, set ESC to sensored mode
  https://www.aliexpress.com/item/1005007759728528.html

## 4. Brains / Audio / Light
- **ESP32-WROOM-32 DevKit V1** `3PCS` ×1 — ~646 ₴ — #1 control + #2 sound/light + 1 spare
  https://www.aliexpress.com/item/1005008503831020.html
- **MAX98357A I2S amplifier** `1PCS` ×1 — ~93 ₴
  https://www.aliexpress.com/item/1005007629020891.html
- **Speaker 4 Ω 3 W** ×1 — ~162 ₴
  https://www.aliexpress.com/item/1005008626624201.html
- **WS2812B addressable LED strip** `Black PCB / 1m / 30 LED / IP30` ×1 — ~179 ₴ — brake + halo; driven by one ESP32 GPIO (no separate controller)
  https://www.aliexpress.com/item/1005007982624217.html

## 5. Power / Telemetry
- **UBEC 5 A** `2PCS 5A` ×1 — ~350 ₴ — Rail A (clean) + Rail B (servos)
  https://www.aliexpress.com/item/1005006414925607.html
- **XT connectors** (one listing, two lines):
  - `5 pair XT30` ×1 — ~85 ₴ — low-current BEC/accessory taps
  - `5 pairs XT60 (male+female)` ×1 — ~93 ₴ — **battery↔ESC main link** *(confirm variant shows XT60 at checkout)*
  https://www.aliexpress.com/item/1005004628891959.html
- **BX100 voltage buzzer** `BX100` ×1 — ~115 ₴ — *optional independent low-voltage alarm*
  https://www.aliexpress.com/item/1005006863873350.html
- **Resistor kit** `600pcs / 30 values / 1/4W / 1%` ×1 — ~174 ₴ — covers **330 Ω** (LED), **10 kΩ** (Hall pull-up + divider low), **27 kΩ** (divider high)
  https://www.aliexpress.com/item/1005006706508149.html

## 6. Servos
- **DSServo DS3235SG steering servo** `180°` ×1 — ~1,234 ₴ — ships a 25T horn (reuse it)
  https://www.aliexpress.com/item/1005003446676428.html
- **MG90S micro servos** `90-180° / 3PCS` ×1 — ~186 ₴ — pan / tilt / DRS (positional, NOT 360° continuous)
  https://www.aliexpress.com/item/1005007104370383.html

## 7. Speed sensor
- **A3144 Hall sensor** `10pcs` ×1 — ~80 ₴ — wheel-speed pickup → ESP32 #1 GPIO (10 kΩ pull-up from kit)
  https://www.aliexpress.com/item/1005006208104090.html
- **Neodymium magnets** `3×1mm N35 / 20pcs` ×1 — ~108 ₴ — glue to rear axle
  https://www.aliexpress.com/item/1005010132819665.html

## 8. Drivetrain
- **Belt-drive full set (pulleys + 140 mm belt)** `3.17mm` ×1 — ~389 ₴ — designer's exact part; **includes the rear output shaft** (so no separate rear-axle rod to buy). ⚠ stock runs low
  https://www.aliexpress.com/item/1005003812517480.html
- **Pinion 48DP / 3.175 mm bore** `28T` ×1 — ~109 ₴
  https://www.aliexpress.com/item/1005009966199887.html
- **Spur — 3Racing Sakura 48P POM** `75T` ×1 — ~364 ₴ — *confirm bolt holes match the belt-set pulley on arrival*
  https://www.aliexpress.com/item/1005009458482481.html

## 9. Bearings
- **MR128ZZ front bearings** `8×12×3.5mm / 10pcs` ×1 — ~166 ₴ (need 4 + spares)
  https://www.aliexpress.com/item/32962032067.html
- **APE 6801 rear bearings** `12×21×5mm / ZZ / 5pcs` ×1 — ~208 ₴ (need 2 + spares)
  https://www.aliexpress.com/item/1005008176682099.html

## 10. Suspension
- **Front oil shocks (1/10 on-road)** `52mm / 4-set` ×1 — ~889 ₴ — eye-to-eye ≈ your 51 mm target (need 2 + spares)
  https://www.aliexpress.com/item/1005009636594515.html
- **Rear oil shock (HSP, 1/10 on-road)** `68mm / 2pc` ×1 — ~221 ₴ — single central rear damper (need 1 + spare)
  https://www.aliexpress.com/item/1005006151304968.html

## 11. Steering
- **M4 fully-threaded rod** `40mm / 10pcs` ×1 — ~163 ₴ — steering arm + cut for 22 mm tie rods
  https://www.aliexpress.com/item/1005008168724880.html
- **M4 rod-end ball joints** `24mm / 10pcs` ×1 — ~129 ₴
  https://www.aliexpress.com/item/1005005616106307.html
- **Turnbuckles 3×32mm** ×1 — ~116 ₴ — adjustable links (set toe). *Parts list wanted ×2 (1 + crash spare); confirm per-order count.*
  https://www.aliexpress.com/item/1005006046564340.html
- **King pins (dowel pin + circlip)** `M3 / 30mm / 5pcs` ×1 — ~91 ₴ — *confirm 3 mm knuckle bore from STL*
  https://www.aliexpress.com/item/1005003990055624.html
- **M3 ball studs (Tamiya/Sakura)** `10pcs` ×1 — ~103 ₴ — the pivot balls the rod ends clip onto
  https://www.aliexpress.com/item/1005006808302333.html
- **M3 tie-rod ends (3Racing Sakura set)** ×1 — ~204 ₴
  https://www.aliexpress.com/item/1005006806232995.html

## 12. Fasteners / hardware
- **Countersunk M3 bolt+nut kit** `392pcs` ×1 — ~433 ₴ — incl. M3×20 mm + ~195 nuts
  https://www.aliexpress.com/item/1005005966167022.html
- **Heat-set brass inserts** `M3 × 5mm / 50pcs` ×1 — ~114 ₴ — soldering-iron installed
  https://www.aliexpress.com/item/32890237459.html
- **Metal sleeves D5×M3** `5mm / 20pcs` ×1 — ~139 ₴ — front guide-rod replacement (need 4)
  https://www.aliexpress.com/item/1005005962614409.html
- **Aluminium tube** `OD 16 × ID 14mm / 300mm` ×1 — ~268 ₴ — **cut into the 14 mm rear-axle spacers ×4** (stops the printed spacer melting onto the axle)
  https://www.aliexpress.com/item/1005005174363008.html

## 13. Cooling (camera)
- **Blower fan 5 V 20 mm (ACP2006-class)** `XH2.54` ×1 — ~278 ₴ — ducted, powers off Rail B / decoupled 5 V (never the clean camera rail)
  https://www.aliexpress.com/item/1005009104924743.html
- **Thermal paste** `15g / 12.4 W·mK` ×1 — ~102 ₴ — small amount; rest is spare
  https://www.aliexpress.com/item/1005007037912738.html

---

# B. rcMart order (genuine Tamiya, ships to UA) — ~$28 incl. shipping
- **Tamiya 54198 — F104 Rubber Tyres (Front / Hard)** — **2 pcs · 30 mm wide · 64 mm OD · foam insert** — ~$8.80
  https://www.rcmart.com/tamiya-f104-rubber-tires-f-hard-54198-00031146
- **Tamiya 51400 — F104 Rubber Tyres (Rear)** — **2 pcs · 35 mm wide · 64 mm OD · slick + foam insert** — ~$8.10
  https://www.rcmart.com/tamiya-f104-rubber-tires-r-51400-00030315

> **1 pack of each = exactly the 4 tyres needed** (2 front + 2 rear). Widths match the printed rims (30 mm front / 35.5 mm rear); IDs seat on the printed bead (Ø44 front / Ø47 rear). **Test-fit one before gluing all four.**

---

# C. Printed at work (free — not bought)
- **Wheels:** Front_Rim_F1_2022, Rear_Rim_F1_2022; Front_Right_Wheel_Hub_2022_F104; Front/Rear_Locking_Nut_F1_2022; tyre-slot hub adapters.
- **Body:** 2024 shell (W14 mould, painted W17 livery); new halo 2.1; camera top 1.1; rearbacklightdiffuser (WS2812 brake-light hole).
- **Chassis (original oil-shock):** floor; front suspension + steering (Suspension_Block_10, Arm4, Crossarm3, GuideRod, Steering_Block4, servosaverv7, upright); rear-axle holders (L/R) + spacers; belt-drive mounts (beltdrivemotorlock).

---

# D. Sourced locally / from stock
- **2S LiPo ×2** — 7.4 V, ~1300–1500 mAh, **soft-case**, **≤75×45×25 mm**, XT60. *(Charger at work — confirm it does 2S balance.)*
- **Build-from-stock:**
  - **XT60 Y-split** — solder from your XT60 connectors + silicone wire (one pack → ESC + both BECs), or buy a ready-made XT60 parallel lead.
  - **1000 µF / 16 V electrolytic cap** ×1–2 — WS2812 supply reservoir + servo-rail decoupling.
  - *(optional)* **1N5819** ×few — diode-drop on the LED 5 V rail to harden the 3.3 V→5 V data line.
- **Consumables:** Dupont jumpers, 28 AWG silicone wire, heatshrink, double-sided foam tape, zip ties, 100 nF ceramics.
- **Tools:** hex drivers 1.5 / 2 / 2.5 mm, silicone shock oil, blue thread-lock, gear grease.
- **Body finish:** primer (grey + white); paint — black (~TS-14), silver (~TS-17), Petronas turquoise (~#00A19C); matte/satin clear lacquer (also seals decals).
- **Decals:** **#63** + W17 livery; waterslide paper matched to your printer (mostly white-backed + a few clear). *Custom decals can have lead time — order early.* (Use official Mercedes-AMG F1 / sponsor references; don't reproduce trademarked logos or the F1 number font.)

---

# Open confirmations (verify as you build — no purchase)
1. **Spur ↔ belt-pulley bolt pattern** — confirm when the belt set arrives (item 12 ↔ 17).
2. **King-pin bore = 3 mm** — measure the knuckle bore in the slicer.
3. **XT line (07)** shows **XT60** at checkout (06 is the XT30 accessory taps).
4. **TX16S internal module = ExpressLRS** (EdgeTX → Model → Internal RF). If MULTI-only, move the ES24TX Pro to it for the backup role.
5. **Test-fit one printed rim + Tamiya tyre** before gluing all four.

---

# Key build reminders (the bench fixes — don't lose these)
- **Battery divider:** 27 kΩ (battery side) + 10 kΩ (to GND); ADC tap in the middle → ESP32 #1, ADC1 channel, 11 dB attenuation. 8.4 V → ~2.27 V (comfortably in the linear region). *[updated from 20 k/10 k]*
- **Hall sensor:** A3144 on **5 V**; **10 kΩ pull-up to 3.3 V**; output → spare GPIO on ESP32 #1.
- **LED data:** **330 Ω** series on the data line + **1000 µF** across 5 V/GND at the strip input. *(optional 1N5819 diode-drop on the LED 5 V for a cleaner 3.3 V logic-high.)*
- **ESC throttle:** **isolate the ESC's BEC +5 V (red) wire** — the two UBECs power the rails; ESP32 #1 feeds the ESC signal, ground common.
- **Servo rail:** **1000 µF** decoupling cap.
- **Camera ↔ WiFi-module solder:** D+→DP, D−→DM, GND→GND, **5 V→VDD5.0 from clean BEC Rail A** (never the camera USB rail).
- **WiFi module:** antennas on J0/J1 **before** power; heatsink on first. Tune `bitrate_max=12, bitrate_min=2, dbm_threshold=-52`.
- **Power rails — Rail A (clean):** camera + WiFi module + both ESP32 + RX + LEDs. **Rail B:** steering servo + 3 micro servos + blower. **All grounds common.**
- **Drivetrain:** pinion + spur **both 48-pitch** — the one mesh-killer if mismatched.
- **ELRS:** TX/RX on the **same major.minor firmware** + the **same bind phrase** (TX16S backup matches).

---

**Approx. spend:** AliExpress ~17,400 ₴ (≈ $425; the QuicRun combo is ~⅓ of it) · rcMart tyres ~$28 · battery + local bits ~$40–60. **All-in ≈ $500.**
