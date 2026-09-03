# W17 — Coordinated Two-Board Flash (link2 v2 lockstep)

> **Gate.** Flashing is Phase B work. Do not run any step below unless A2 is CLOSED and
> Phase B is APPROVED (`../../CURRENT_STATUS.md`) — see `D8_BENCH_BRINGUP.md` Phase -1 and
> `PHASE_B_FIRST_POWER.md`. This document is the detail that `D8_BENCH_BRINGUP.md` Phase 9
> and Phase 11a point to; it does not open the gate on its own.

## Why this needs its own procedure

Board #1 (`w17-control-fw`) and board #2 (`w17-soundlight-fw`) talk over the one-way `link2`
UART: board #1 owns the protocol (`lib/link2/`, `docs/link2_protocol.md`), board #2 carries a
**verbatim copy** of the shared subset (permanent by decision, guarded by
`tools/link2_copy_check.sh` and the `link2-drift` CI job on each side — not shared by a
submodule). The two boards are flashed independently, from two separate PlatformIO projects,
so nothing at flash time enforces that they carry matching protocol code. **The wire format
itself does the enforcing, and it is stricter than "ignore fields you don't know":**

- `lib/link2/include/link2/Link2Frame.hpp:60-63` — `kPayloadLen = 14`, `kFrameLen = 17`
  (start + length + 14-byte payload + CRC-8, poly `0xD5`).
- Board #2's decoder checks the **length byte before the version byte**
  (`w17-soundlight-fw/lib/link2/src/Link2Codec.cpp:56-58` in the one-shot decode path, and the
  streaming assembler's `ReadingLength` state at `:107-114` rejects immediately, before CRC or
  version, if the length byte isn't exactly `kPayloadLen`). **A version bump alone is
  forward-compatible-ish (`BadVersion`, a recognized "well-formed, just newer" outcome); a
  length mismatch is not** — every frame from a board whose payload length differs is
  `FrameInvalid`/`BadLength`, full stop, indistinguishable on the wire from a broken UART.
- Board #2's own local failsafe (`docs/link2_protocol.md` "Timing" section; `D8_BENCH_BRINGUP.md`
  Phase 9) engages within **500 ms of no valid frame** — engine to idle/off, hazard blink.

So the hazard this procedure guards against is specific: **flash a payload-length change to
only one board, and the other board doesn't show a wrong reading — it shows a permanent
"link lost" within half a second**, every time, with no partial/garbled state in between. That
is a safe failure direction (board #2 defaults to its own failsafe, never to inventing state),
but it is still the wrong car to hand over, and it is exactly the failure a coordinated flash
prevents from ever being observed on the gift unit.

**When this actually matters:** only when the two repos' `lib/link2` trees have drifted —
i.e., a protocol change landed on one side and not (yet) the other. If both repos are at their
current `main` with the copy check green, board #1 and board #2 already agree, and a normal
one-at-a-time re-flash (e.g., re-flashing board #1 alone after a `esp32dev`↔`esp32dev_tuning`
round-trip) is not a coordinated-flash situation — Phase 9's plain steps cover it. Run
`tools/link2_copy_check.sh` (control-fw side; add `--sibling <path>` if the soundlight
checkout isn't at the default `../w17-soundlight-fw`) before a coordinated flash to confirm
which situation you're in.

> **A known false alarm in this same family, so you don't chase it:** `tools/link2_copy_check.sh`
> deliberately treats `docs/link2_protocol.md` as **reported, not fatal** — each repo's copy
> legitimately carries its own local prose (an "Ownership" section, for one), so a plain diff
> cannot tell normative drift from local commentary. Confirmed at this revision: the two
> `docs/link2_protocol.md` files *do* differ, entirely in that kind of prose (ownership/tooling
> narration) — no normative content (frame layout, payload table, CRC, timing rule, state
> matrix) differs. **This repo's own `.github/workflows/ci.yml` `link2-drift` job does not
> call `tools/link2_copy_check.sh` at all** — it re-implements its own diff loop over all four
> shared files, including the doc, and fails on any difference (job `:45-71`, diff loop
> `:51-71`). That job can go red on a legitimate doc-only difference with zero protocol drift.
> Treat a red `link2-drift` job
> as inconclusive until you've separately run `tools/link2_copy_check.sh` (which does draw the
> fatal/reported line correctly) — do not read CI red alone as proof of a coordinated-flash
> hazard, and do not read CI green as proof of its absence, since the job as written can also
> pass while a *code* file has drifted if GitHub Actions network access to the sibling clone
> fails silently in a way this session did not exercise. This CI-job gap is a known finding
> (tracked for a separate fix wave, not resolved by this document) — it does not change what
> you should actually do here, which is run the code-file check yourself before flashing.

## Order

**Flash board #2 (soundlight) before board #1 (control), or flash both before connecting the
link2 wire.** Reasoning: board #2's failsafe is passive and safe in every state (never
connected, connected-then-lost, mid-frame power-on all resolve to "quiet" or "hazard-blink,"
never to inventing motion or sound) — so board #2 running slightly-newer or slightly-older
protocol code with the UART physically disconnected is inert. Board #1 has no way to observe
board #2 at all (link2 is one-way, TX only, GPIO25 → GPIO16), so there is no "wrong order"
that damages anything; the rule above exists only to avoid a window where the wire is live
between two boards you haven't yet confirmed agree.

1. Confirm `lib/link2/` + `docs/link2_protocol.md` are what you intend on **both** checkouts:
   `w17-control-fw` at its target commit, `w17-soundlight-fw` at its target commit. Run
   `tools/link2_copy_check.sh` (control-fw). Exit `0` with "identical across both repos" for
   the four code files (`Link2Frame.hpp`, `Link2Codec.hpp`, `Link2Codec.cpp`, `library.json`)
   is the pass condition; a reported (non-fatal) doc difference is expected and fine per the
   note above.
2. **Board #2 first, UART disconnected or board #1 powered off:** `pio run -e esp32dev -t
   upload` in `w17-soundlight-fw` (its only delivery env — no tuning/console variant exists
   on this board by design).
3. **Board #1 second:** `pio run -e esp32dev_tuning -t upload` in `w17-control-fw` for bench
   work (Phase 11a below covers the eventual `esp32dev` delivery re-flash). This is text here,
   not something this session runs — flashing is Phase B, gated as above.
4. Reconnect / power up the link2 wire (GPIO25 board #1 → GPIO16 board #2, common ground,
   115200 8N1) only after both boards are running the confirmed-matching firmware.

## NVS migration (board #1 only — board #2 carries no persisted tuning)

Board #1's settings blob is versioned (`lib/settings/include/settings/Settings.hpp:50-60`):
`[version][struct bytes][crc8]`, loaded through `settings::loadOrDefault` with a strict guard
chain — length → CRC → **version** → `Settings::valid()` — and *any* failure at any link falls
back to **complete compiled defaults**, never a partial or mixed config. There is currently one
history step, v1 → v2, already merged and shipped in this codebase (not something a coordinated
flash performs going forward, but the behavior it establishes governs every future bump):

- **v1** carried steering + gearbox + battery.
- **v2** added, in one bump, `failsafe::GimbalDecayConfig` (`gimbal.decay`), `link2::SoundConfig`
  (`sound.profile`, `sound.volume`), and `btpad::BtPadConfig` (BT show-off tunables) —
  `Settings.hpp:54-58`.
- **What flashing a v2-blob-format firmware onto a board whose NVS still holds a v1 blob does:**
  the version byte fails the check, the whole blob is discarded, and the board boots on
  **compiled defaults for every field, including steering trim/endpoints and `batt.ppt`** — not
  only the new v2 fields. This is safe (never a silent partial mix) but it is not free: **any
  bench calibration recorded before the version bump is gone from NVS after the flash** and
  must be redone and re-saved (`D8_BENCH_BRINGUP.md` Phase 11a steps 1–6).
- The practical consequence for a coordinated flash: if you are moving a board from an
  old build predating v2 straight to today's `main`, budget time to **recalibrate and
  `save`** after the flash, not just to confirm connectivity. If the board already carries a
  v2 blob (this codebase's steady state since the 2026-08-17 merge), a same-version re-flash
  (e.g. `_tuning` ↔ `esp32dev` in Phase 11a) round-trips the existing blob with no data loss —
  that path is exercised and confirmed by Phase 11a step 6.
- Keys that must survive the round-trip, read back with `get <key>` or `status`: `steer.min`,
  `steer.max`, `steer.center`, `steer.trim`, `batt.ppt`, `gear.<N>.max`/`gear.<N>.expo` for each
  gear, `gimbal.decay`, `sound.profile`, `sound.volume`. This is the same list
  `D8_BENCH_BRINGUP.md` Phase 11a steps 4–5 record as "the shipped tune" — the authoritative
  evidence of what the car left the bench with.
- **Separately, and not an NVS setting:** `w17-soundlight-fw` has its own compile-time
  idle/max-RPM engine-sound constants (an optional owner-requested tuning knob on that repo,
  per `../../CURRENT_STATUS.md`'s "shipped-tune compile pin" note). That is a source change on
  board #2 requiring its own re-flash — it is not carried by board #1's NVS blob and not moved
  by anything in this document.

## Post-flash verification

1. `tools/link2_copy_check.sh` still exit-`0` on the two checkouts you actually flashed (belt
   and suspenders — confirms you flashed what you intended to check in step 1 above).
2. `D8_BENCH_BRINGUP.md` Phase 9's on-the-bench checks: link2 frame decodes as `FrameReady` on
   board #2 (not `BadVersion`/`FrameInvalid`), engine sound + WS2812 respond to board #1 state,
   cutting the UART mid-run drops board #2 to its own failsafe within 500 ms.
3. If board #1 was moved across the v1→v2 boundary (or any future version bump): re-run
   `D8_BENCH_BRINGUP.md` Phase 11a steps 1–6 in full — do not assume the old calibration
   survived; confirm it with `get`/`status` readback, per the NVS migration note above.
4. Re-run Phase 5's safe-state checks on whichever board #1 build you actually ship next —
   flashing is exactly the kind of change the re-arm invariant and boot-safe check exist to
   catch a regression in.

## Rollback

- **Board #1** rolls back like any other re-flash: `pio run -e esp32dev_tuning -t upload` an
  older firmware image, then either accept whatever NVS blob is already stored (if its version
  byte matches what the older firmware expects) or `reset` + `save` to force compiled defaults
  and recalibrate. There is no separate "undo" for a bad flash beyond flashing a known-good
  image — the NVS guard chain's job is only to keep a *version mismatch* from becoming a
  *silent partial* mismatch, not to remember old calibrations across an intentional downgrade.
- **Board #2** has no persisted state to roll back — re-flashing an older `esp32dev` image is
  the entire rollback.
- **If a coordinated flash leaves the two boards mismatched** (interrupted mid-procedure,
  wrong image on one side): the wire being live is harmless per the design above — board #2
  either decodes normally (versions/lengths agree) or sits in its own failsafe (they don't).
  Reconnect nothing until `tools/link2_copy_check.sh` confirms the two checkouts you intend to
  flash from actually agree, then redo the two flashes in order.
