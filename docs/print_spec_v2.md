# W17 FPV RC F1 — Print Spec (v2 · current)

Slicer settings **and print order** for the 1/10 3D-printed Mercedes W17 (#63) on the RC-01 chassis. Supersedes v1 and folds in the decisions made since: **printed wheels, the brake-light diffuser, the camera-cooling duct,** and the **oil-shock + 2024-body part selection**.

**Golden rule (matters more than infill):** layer lines are the weak axis — a part fails *between* layers. Orient each stressed part so the **load runs along the layers, not across them**. Often beats going 40% → 100% infill.

---

## Changes since v1
- **Wheels resolved → printed rims.** v1's open "printed rims vs bought wheels?" question is answered: **print the rims, buy bare Tamiya tyres.** New material group added below.
- **Brake-light diffuser is a light-transmitting exception** to the "avoid translucent / paint everything" rules.
- **Camera-cooling duct** added as a (small) new printed part for the ACP2006 blower.
- **Part selection locked:** original **oil-shock** front/rear, generic **2024** body (W17 paint) — *skip* the revision-steering and team-livery folders.

---

## Universal settings
- **Layer height:** 0.2 mm default; finer where noted.
- **Nozzle:** 0.4 mm.
- **Infill pattern:** gyroid default; rectilinear only on 100% parts.

## Material ladder (by part group)

### Body / shell — cosmetic
- **PLA**, **0.12–0.16 mm** layers on visible bodywork (less sanding before primer — it's a gift), 20–40% gyroid.
- **Black** filament (matches the W17 base → fewer coats, chips hide). Exception: print the **silver-nose piece in white/light grey** so metallic + turquoise pop.
- Avoid translucent / silk / glitter (telegraph through paint, sand poorly).

### Brake-light diffuser — **light-transmitting exception** ⚠ NEW
- `rearbacklightdiffuser` (the revised part with the **WS2812 hole**) must glow, so it breaks two body rules: **don't paint the lens, and don't use opaque black.**
- **White or natural-translucent PLA/PETG**, **1–2 perimeters** over the lens, ~15–20% infill so light passes and diffuses evenly.
- Leave the lens **unpainted** (mask it when you paint the surrounding body). Let the **WS2812 produce red in firmware** — a clear/white lens then also lets the same LEDs do amber/white for other effects.

### Floor — load-bearing
- **PETG** preferred (heat — a black car may bake in summer sun); **Tough PLA** if it stays cool/indoors. 0.2 mm, 40–60% gyroid.
- Use the **original floor** (NOT `NewFrontFloorSuspensionUpgrade REVISION_1.1` — that's the revision front, which we're not running).

### Wheels — rims / hubs / nuts / tyre-slot adapters ⚠ NEW
- **These are loaded parts, not cosmetic — do NOT print in display PLA.**
- **PETG** for the rims, locking nuts, and tyre-slot adapters; **ASA** for the **rear hub** (it's both hot from the drivetrain and torque-loaded). 4–5 walls, 40–60% infill.
- Hidden under the tyre → color irrelevant. Glue the Tamiya tyres on after a **test-fit** (bead Ø44 front / Ø47 rear).

### Front suspension + steering — loaded (oil-shock, original) 
- **PETG** (loaded but away from motor heat); ASA is fine if you have it dialed in. 4–5 walls, high infill.
- Orient uprights/arms so cornering + steering loads run **along** the layers.

### Rear axle + drivetrain mounts — hot + loaded (CRITICAL)
- **ASA** (or any high-temp filament). **No PLA here** — this is the documented failure point (motor/drivetrain heat softens PLA; the metal axle sleeves exist for the same reason).
- 0.2 mm, **100% rectilinear**, 4–5 walls. Print slow, **enclosed** (ASA warps in drafts) and **well-ventilated** (styrene fumes). Color irrelevant.

### Camera-cooling duct — near the hot camera ⚠ NEW
- Small shroud that channels the ACP2006 blower across the camera and out a cut vent.
- **PETG** (it sits against the hot camera; PLA would creep), 0.2 mm, 3 walls, 20% infill, hidden → any color.
- Parametric starter file provided separately (`camera_blower_duct.scad`) — set it from your measured parts.

### Hidden mounts / internals
- Any filament/color the shop has loaded.

---

## Print order & part selection (what to print, what to skip)

**Body — generic 2024, painted W17.** Print `NEW BODY 2024 FRONT 1` + `NEW BODY 2024 REAR` + `NEW BODY 2024 Mirror` + `new halo 2.1` + the **revised `camera top 1.1`** + the **`rearbacklightdiffuser`** (diffuser settings above). **Skip** the `Ferrari SF 24` and `Mclaren mcl38` subfolders — those are team-shaped / pre-colour-split bodies; you paint W17 on the generic shell.

**Front end — original oil-shock (NOT the revision).** Print the **original** front suspension + steering: `Suspension_Block_10`, `Arm4`, `Crossarm3_extended`, `GuideRod`, `Steering_Block4`, `servosaverv7`, the original upright `2023WheelHubsSuspension5` (+ mirror), plus the remaining base front parts from the original RC-01 set. **Skip the entire `New 1.1 Steering Upgrades` folder** (`New Left/Right Wheel Hub`, `New Steering Arm with Ball Joint L/R`, `New Steering Servo Holder`, `NewFrontFloorSuspensionUpgrade REVISION_1.1`) — that's the revision ball-joint front, incompatible with your oil-shock front.

**Rear end — oil-shock.** Print the rear-axle holders (`Leftrearaxle`, `Rightrearaxle`), `beltdrivemotorlock`, `newgearmotorlock`, the rear spacers, and the **rear mount for the 68 mm oil shock** — in **ASA**. The printed rear spacers ride on the metal axle **inside the 14 mm aluminium sleeves** (cut from the OD16 tube) so they don't melt onto the shaft.
- ⚠ **Open verification before you print the rear:** confirm in the slicer that `Spring_mount_2_REVISION_1`'s rocker actually **seats the 68 mm coilover**. **If yes** → you may run that hybrid rocker (+ the revised rear motor covers). **If no** → print the **original** rear mount and skip `RearSpringMountREV4` / `springblock` (those are the revision spring rear).

**Wheels — printed rim set.** Print `Front_Rim_F1_2022`, `Rear_Rim_F1_2022`, `Front_Right_Wheel_Hub_2022_F104` (+ its left/mirror), `Front_Locking_Nut_F1_2022`, `Rear_Locking_Nut_F1_2022`, and the **`New 1.1 Rear Upgrades`** adapters `F104 tyreslot1/2 no grubs tighter` (the tighter, less-slop versions). Materials per the wheel group above. Tamiya 54198 (front) + 51400 (rear) glue on.

**Prerequisites (download once, per the model's READ ME).** `new halo 2.1` is already in the 2024-body folder (use it). The **2022 Tyre Runs for Tamiya F104** download *is* your rim/hub/nut set above. Grab the mirror part from the body folder (`NEW BODY 2024 Mirror`) rather than an external mirror.

**Suggested sequence:** test coupons first (one ASA + one PETG, to dial the shop's machine) → **one rim + one hub** and **test-fit a tyre before printing the other three** → rear/drivetrain ASA parts → front PETG parts → floor → body shell (slow, fine layers) → diffuser + small cosmetics → cooling duct once the camera/blower are in hand and measured.

---

## Finishing
- **Prime everything visible** (kills layer lines + unifies filament colour). **Grey** primer default; **white** under the silver nose + Petronas turquoise for max brightness. **Mask the diffuser lens — do not prime/paint it.**
- Cure → **gloss clear** (decals need a glossy bed or they "silver") → decals → **final clear** (matte/satin to taste) → cure → assemble.

## Safety
- **ASA fumes:** ventilated space, not a closed room.
- **Heat-set inserts:** soldering-iron installed — handle with care (designer's own warning).

## Sort before build week
1. **Confirm the shop stocks ASA** (the one material you can't substitute for the rear/drivetrain parts; any high-temp filament works).
2. **Spring_mount rocker vs 68 mm shock** — the slicer check above; decides original vs hybrid rear.
3. **Test-fit a printed rim + Tamiya tyre** before printing all four.

---

**Summary:** PLA shell (black, fine) → **white/natural diffuser, lens unpainted** → PETG floor → **PETG/ASA wheels (not PLA)** → PETG front, **ASA rear/drivetrain (100%/rectilinear)** → PETG cooling duct. Original oil-shock front/rear, generic 2024 body, skip the revision-steering + team-livery folders. Orient stressed parts along the load path; prime everything visible except the diffuser lens.
