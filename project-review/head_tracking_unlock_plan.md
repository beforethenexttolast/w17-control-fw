# Head-tracking unlock plan — mapper architecture & sequencing

**Status: documentation only. Nothing here is implemented, authorized, or scheduled.**
Date: 2026-07-14. Claims tagged [C] confirmed (file cited) / [I] inferred / [A] assumption.

This is the **Claude-side source of truth for the head-tracking unlock sequence and the
mapper process boundary**. It turns the 7 blockers of
`iphone_pan_tilt_firmware_readiness.md §8` into ordered, ownership-tagged work. It is NOT
the canonical safety milestone — that remains Codex-owned
(`iPhone_rc/docs/FIRST_ACTIVE_PAN_TILT_MILESTONE.md`,
`iPhone_rc/docs/FUTURE_HEAD_TRACKING_TO_PAN_TILT_SAFETY.md`). Where the two disagree, the
Codex milestone gates movement; this doc sequences the work leading up to it.

Related Claude-side docs (cross-links, not copies):
- Placement source of truth: `w17-3d-codex/CAMERA_GIMBAL_PLACEMENT.md`
- Display semantics: `w17-ground-station/docs/camera_aim_display_semantics.md`
- Video baseline: `w17-ground-station/docs/video_topology_baseline.md`
- Codex handoff for iPhone-side items: `_handoff/2026-07-14_codex_handoff_vr_fpv_cross_review.md`

---

## 0. Invariants (unchanged, restated)

- Firmware stays iPhone-unaware: no iPhone JSON, no iPhone UDP, ever.
- No iPhone → CRSF / servo / gimbal / ESC.
- Windows is the sole control/integration authority; the arbitration into CRSF ch9/10
  happens on the PC, never in firmware, never on the phone.
- No servo movement from head tracking until every blocker in
  `iphone_pan_tilt_firmware_readiness.md §8` is green AND the Codex-owned
  FIRST_ACTIVE milestone checklist passes.
- The Electron ground station remains **viewer / configuration / visualization /
  log-only**. It never enters the control path
  (`w17-ground-station/test/noControlPath.test.js` is unbreakable by policy).

## 1. Ratified constants and vocabulary

### 1.1 Stale-timeout canon: 300 ms (receive-time authority)

The mapper's stale authority is **300 ms of receive-time silence**, matching the canonical
contract (`iPhone_rc/docs/windows_bridge_contract.md §3`) and the implemented W3 default
(`W17_HEADTRACK_STALE_MS = 300`, `w17-ground-station/main/main.js` /
`shared/headTracking.js DEFAULT_STALE_MS`) [C]. **Ratified canonically 2026-07-14 at
revision `84532ed` (contract §3 "Canonical stale boundary"), including the exact
299/300/301 boundary below; `timeout_ms` is pinned as a diagnostic hint that cannot
weaken the receiver threshold.**

Deterministic test boundary (for mapper tests):

| Age since last valid packet | Classification |
|---|---|
| 299 ms | fresh |
| 300 ms | fresh |
| 301 ms | stale |

The **400 ms** figure in `iphone_pan_tilt_firmware_readiness.md §3.9 / §8.3` and
`w17-ground-station/docs/iphone_bridge_readiness.md §4` is **superseded** (notes added in
place in both documents, 2026-07-14).

### 1.2 Three distinct timeout domains — do not conflate

| Domain | Value | Owner | Status |
|---|---|---|---|
| iPhone local motion-sample freshness | currently **500 ms** (`iPhone_rc/FPVHUDApp/Models/MotionState.swift:82`, `staleAfter = 0.5`) [C] | Codex | **RESOLVED canonically 2026-07-14 (rev `84532ed`, contract §3):** before any active mapping, the iPhone must stop packet generation when its Core Motion sample is older than **250 ms**; the current 500 ms behavior is acceptable for the log-only phase only; `timestamp_ms` stays send-time (diagnostics), receive time stays stale authority; **no sample-age field in v1** (adding one later = deliberate schema/example/mirror revision). The sender change itself is future Codex implementation work, gated as part of the active milestone |
| Packet `timeout_ms` advisory hint | app default **250 ms** | Codex (sender) | diagnostic hint only; never authority |
| Mapper receive-time stale authority | **300 ms, fixed** | mapper (PC side) | ratified; boundary table above |

### 1.3 Stale/disarm decay target: commanded center 992

On stale intent, disarm, or fault, the mapper decays commanded ch9/ch10 to **CRSF 992 —
the authoritative *commanded* center**. Any hybrid-mapping virtual center
(`virtualCameraCenter` in the Codex VR plan) is **discarded** by that transition; re-arming
requires an explicit recenter, which re-seeds the virtual center from the mapper's
authoritative final **commanded** value (never a claimed measured servo position).

992 is a commanded neutral, not a physically validated one: whether the mechanical
assembly is safe at (and on the way to) that pulse still requires bench validation
(blocker 1). Until physical feedback exists, no layer may present 992 — or any commanded
value — as a measured angle (§1.4).

### 1.4 Commanded vs measured — honesty rules (until physical feedback exists)

There is no gimbal position feedback anywhere in the system: not in firmware telemetry,
not on the wire (`iphone_pan_tilt_firmware_readiness.md §4.6` [C]). Therefore:

- Camera yaw/pitch values shown or exported anywhere are **commanded/requested mirrors**,
  never measured angles.
- "Near limit" means **command saturation** (the commanded value reached its configured
  cap), not confirmed mechanical contact.
- Recenter operations use the mapper's authoritative final **commanded** value.
- Display-side rules live in `w17-ground-station/docs/camera_aim_display_semantics.md`.

## 2. Process boundary — verified findings (read-only, 2026-07-14)

Owner decision on record: **the proposed active mapper/arbiter is elrs-joystick-control.**
The Electron app does not own and will not own active mapping.

### 2.1 Who does what today

| Question | Finding | Evidence |
|---|---|---|
| Which executable binds UDP 5602 now? | The **Electron ground-station main process**, and only when W3 is enabled (`W17_HEADTRACK=1` or the persisted settings toggle); default is **nothing bound**. The bind is a plain exclusive `dgram udp4` socket — **no `reuseAddr`** — so a second process cannot bind 5602 while it runs. | `w17-ground-station/main/HeadTrackingReceiver.js:33,73` [C] |
| Can elrs-joystick-control bind or receive the W3 input? | **VERIFIED (CB0, 2026-07-14): not as shipped — but a source fork can.** Upstream has **no UDP, no plugin/dynamic-load, and no external or virtual-axis ingest of any kind.** Its only inputs are SDL gamepads, and every mixer-graph leaf resolves to a physical gamepad axis (`pkg/config/input_axis.go:85-111`, `pkg/devices/controller.go:40,136`). Its only network sockets are gRPC/TCP:10000 and the HTTP/grpc-web Web-UI on :3000 (`pkg/server/controller.go:73`, `cmd/elrs-joystick-control/main.go:27,30`); there is no UDP listener and no 5602. So head-intent ingest requires a **code fork**, not configuration or a plugin. Details + fork shape in §2.3. | read of upstream HEAD `2b8031a`, cloned read-only into `_vendor/elrs-joystick-control` 2026-07-14 [C] |
| Electron ↔ elrs-joystick-control relationship | **Launch-only, by pinned safety design**: detached spawn, stdio ignored, unref'd, no kill/stop/IPC. Electron cannot talk to it at all today. | `main/elrsLauncher.js:1-40`, pinned by `test/noControlPath.test.js:125-137` [C] |
| Where do manual right-stick and head intent meet? | Inside **elrs-joystick-control's input/mixing stage**: it already reads the DualShock (including the right stick that becomes ch9/10) and is the only place both sources can be arbitrated before channel encode. Right-stick-wins, no-auto-restore arbitration therefore lives there. The Electron app is uninvolved. | `iphone_pan_tilt_firmware_readiness.md §2.3.1, §8.4` [C for the authority location; the ingest mechanism is the §2.3 blocker] |
| Which process generates final CRSF ch9/10? | **elrs-joystick-control** — sole producer of the RC channel stream. | readiness §2.3.1 [C] |
| Which process transmits them? | **elrs-joystick-control**, over the CRSF serial (FT232) it holds **exclusively for write**, to the ELRS TX module; radio carries it to the car. | `w17-ground-station/docs/TELEMETRY.md:64` [C] |
| How does firmware receive and convert? | RX → UART2 @ 420 k `RC_CHANNELS_PACKED` (0x16) → `crsf` parser → `ChannelDecoder` (indices 8/9 = ch9/ch10 → pan/tilt, anchors 172/992/1811 → ±1000; `ChannelDecoder.hpp:20-21`) → `ServoOutput::setPosition()` (µs, double-clamped; `ServoOutput.cpp:7-31`) → LEDC 50 Hz PWM, pan GPIO19 / tilt GPIO23 (`main.cpp:106,117-118`, `PinMap.hpp:28-31`) → MG90S. Source-agnostic by construction. | [C] |
| How does Electron get mapper diagnostics without entering the control path? | **Design note (not implemented):** the mapper republishes a one-way, display-only diagnostics stream (state, packet age, arbitration source, commanded values) that Electron only renders — inbound-only to Electron, mirroring how it already consumes telemetry. Electron sends nothing to the mapper; the launch-only property of `elrsLauncher.js` is untouched; a `noControlPath`-style guard would pin the new surface. | [I — design; whatever architecture §2.3 resolves to must preserve this property] |

### 2.2 Port 5602 single-bind constraint

The iPhone sends intent as **unicast UDP to one configured destination**
(`iPhone_rc/FPVHUDApp/Networking/HeadTrackingSender.swift` — single `NWConnection`) [C].
Two processes cannot independently bind the same UDP port (the current bind is exclusive).
So "mapper receives intent" and "Electron keeps its log-only 5602 receiver" cannot both be
true as currently built.

### 2.3 VERIFIED FINDINGS (CB0, 2026-07-14) — owner decision #1 RESOLVED 2026-07-15

**Provenance:** upstream `github.com/kaack/elrs-joystick-control` cloned read-only into
`_vendor/elrs-joystick-control` (HEAD `2b8031a`), read 2026-07-14. No W17 code changed, no
`_vendor/` contents edited. All file:line below are in `_vendor/elrs-joystick-control/`.
All findings tagged [C] were confirmed against that source; architecture-shape statements
tagged [I] follow from it. **This section reports evidence; it does not choose a topology.**

#### 2.3.1 How gamepad axes enter channel mixing [C]

The app is a **node-graph mixer**. `pkg/config/util.go:73-103` registers 28 node types;
input leaves are `InputGamepad` / `InputAxis` / `InputButton` / `InputHat`, math/logic
nodes are `InputIf` / `InputCase` / `InputGt…Lt` / `InputMin/Max` / `InputAdd/Subtract` /
`InputSeq` / `InputTrim` / …, and the sink is `OutputTransmitter` (holds a `[16]CRSFValue`
keyed by serial-port name). Pipeline:

1. **Input:** SDL only. `pkg/devices/controller.go:40` initialises `sdl.INIT_GAMECONTROLLER`;
   `:116-142` polls SDL and fires `DeviceEventChan` on every event (`:137`).
2. **Value read:** `InputAxis._Eval` reads a value **only** from a gamepad
   (`pkg/config/input_axis.go:85-99`, `case *InputGamepad: gamepad.Axis(n)`); any non-gamepad
   source returns NaN (`:110-111`). No leaf reads anything but a physical gamepad.
3. **Mix:** the eval loop recomputes the whole graph into per-transmitter `[16]CRSFValue`
   (`pkg/config/eval.go:60-113`; transmitter `Values` collected at `:88-92`). It recomputes
   **only** on a config change, a stream event, or a gamepad device event (`eval.go:78,95,104`).
4. **Send:** a ticker-driven loop reads the latest computed array for the port and packs it
   to CRSF over serial (`pkg/link/send.go:118-144`). Commanded center default is **992**
   (`pkg/config/controller.go:43`, `EvalCenter` all-992; no-data default 0 at `:42`).

#### 2.3.2 Is there any UDP / network / plugin / virtual-axis ingest? — **No.** [C]

Repo-wide search finds **no UDP, no `net.ListenUDP`/`DialUDP`, no `:5602`, no dynamic plugin
load (`dlopen`/`.so`), and no virtual/external axis.** The only sockets are gRPC/TCP:10000
(`pkg/server/controller.go:73`, default `main.go:30`) and the HTTP/grpc-web Web-UI on :3000
(`pkg/http/controller.go`, `main.go:27`). Because every graph leaf resolves to an SDL
gamepad (§2.3.1), there is no configuration- or plugin-level way to inject head intent:
**head-intent ingest requires a source-code fork.**

#### 2.3.3 Where right-stick-wins arbitration could live [C for primitives / I for design]

Inside the config node graph — the architecture already supports selection/relational logic
(`InputIf`, `InputCase`, `InputGt/Lt`, `EvalRelational`/`EvalOperation` in `util.go:212-305`).
"Right-stick-wins, no-auto-restore" can be a small new arbitration node (registered in
`_NewTypesMap`, `util.go:73-103`) or a composed sub-graph. Shaping and safety are also
expressible in-graph: nodes may hold state and read wall-clock time (`InputSeq` uses
`time.Now()`, `input_seq.go:242,262`; `InputTrim` keeps trim memory), so deadband, low-pass,
slew, stale-decay-to-commanded-992, and a `virtualCameraCenter` hybrid need **no engine
surgery** — only the ingest source does. Arbitration requires a head-intent value to exist
in the graph, which is exactly what §2.3.2 says is missing today.

#### 2.3.4 How a one-way diagnostics republish could work — **already exists** [C]

The app exposes read-only state over gRPC streaming, by construction (each RPC only reads
controller state and `server.Send`s — never writes into the control path):
`StreamRfDeviceChannels` = the 16 commanded CRSF values per port
(`pkg/server/stream.go:15-27` → `config/controller.go:103-134`); `StreamEvalStates` = every
node's value incl. any arbitration/state nodes (`stream.go:29-41` → `controller.go:167-184`);
`StreamLinkState` = packet counts + port/supervisor state (`stream.go:55-66` →
`link/controller.go:78-90`); `StreamDeviceState` = gamepad states. Electron can be a
**grpc-web client** of these (the same transport the Web-UI already uses via `pkg/http`),
sending nothing back — preserving the launch-only property of
`w17-ground-station/main/elrsLauncher.js` pinned by `test/noControlPath.test.js`. A dedicated
head-intent diagnostics stream (state, packet age, arbitration source, commanded 9/10) would
be one more RPC of the identical shape, or can piggyback on `StreamEvalStates`.

#### 2.3.5 What a minimal fork looks like [I, grounded in the above]

Smallest viable fork mirrors how gamepads already work — **one ingest source + one/few
nodes, no send-path change:**

1. A `pkg/headintent`-style package analogous to `pkg/devices`: a goroutine binds UDP 5602,
   validates each datagram against the bridge contract, keeps last-valid value + receive
   time, and pokes an eval trigger on accept (`configCtl.AlertStreamChan()` /
   `devicesCtl.AlertDeviceChan()` — the server already "fakes a device event to force
   evaluation" this way at `server_grpc.go:194,226,250`).
2. A new `InputHeadIntent` node (registered in `_NewTypesMap`) reading that shared state,
   applying deadband/low-pass/slew and stale-decay-to-992 at the 300 ms receive-time
   authority (§1.1) using `time.Now()` like `InputSeq`.
3. A right-stick-wins arbitration node (or `InputIf`/`InputCase` sub-graph) feeding ch9/ch10.
4. Optionally a `StreamHeadIntentState` RPC (else reuse `StreamEvalStates`).

Serial, CRSF packing, and the send loop are untouched. Build stays Go + SDL; UDP via
stdlib `net`. Effort: moderate (one package + 1–3 node types + native tests) — all three
topology options below need this same fork; they differ **only** in how the single 5602
datagram reaches it.

**Licensing (fork-ownership input, not legal advice) [C]:** upstream is **dual-licensed —
GPL-3.0-or-later OR Fair Source 0.9** (`LICENSE`, `LICENSE-GPL`, `LICENSE-FAIR-SOURCE`);
the recipient chooses one. The Fair Source license carries a **1-user Use Limitation** with
paid licensing beyond it; the GPL-3.0 option carries copyleft (a *distributed* fork's source
must be offered under GPL-3.0). A private, single-user bench fork is compatible with either.
Which license the fork is taken under, and who owns/maintains it, is part of owner decision #1.

#### 2.3.6 Topology options mapped onto the real codebase

All three require the §2.3.5 fork. The only difference is the 5602 delivery path:

- **(a) Mapper binds 5602 + diagnostics republish.** Fork's UDP source binds 5602; Electron's
  W3 log-only receiver is disabled while the mapper runs (the current 5602 bind is exclusive,
  no `reuseAddr` — `w17-ground-station/main/HeadTrackingReceiver.js:33,73`); Electron
  subscribes to the mapper's **existing** gRPC diagnostics (grpc-web:10000, §2.3.4) for its
  live view. **No iPhone change; no new process; republish surface already exists.** Cost:
  Electron gains a small grpc-web diagnostics client; its independent live 5602 log view is
  replaced by the mapper's stream.
- **(b) iPhone dual-destination send.** iPhone sends identical packets to the mapper's port
  **and** Electron's 5602. Mapper still needs the same §2.3.5 ingest (dual-send avoids port
  contention, it does not reduce fork work). Requires an iPhone sender change — today
  `HeadTrackingSender.swift` is a single `NWConnection` (§2.2) — Codex work, handoff **H11**,
  config-level, no schema change; accepts two intent consumers (only the mapper authoritative).
- **(c) Fan-out relay.** A tiny reviewed process binds 5602 and mirrors datagrams to both the
  mapper and Electron. No iPhone or Electron change, but **adds a new process inside the
  control-path perimeter** — a new review/`noControlPath`-style surface. Mapper still needs
  the §2.3.5 ingest.

**Evidence-based lean (input to owner decision #1, not a decision):** (a) has the smallest
new surface on the evidence — no iPhone change, no new process, and the one-way republish it
depends on already exists and is already read-only. (b) trades that for an iPhone sender
change plus two intent consumers; (c) trades it for a brand-new relay process to review.

#### 2.3.7 OWNER DECISION #1 — RESOLVED 2026-07-15 (topology (a))

The owner accepted CB0 and chose **topology (a)**:

- The **owned/forked elrs-joystick-control mapper is the production owner of UDP 5602.**
- Electron stays **viewer / configuration / logging only**; it does **not** get an
  Electron-mediated control relay (option (c) is rejected).
- The mapper exposes a **read-only head-intent diagnostic snapshot** to Electron through its
  existing gRPC/API architecture where practical (§2.3.4). If that would need a substantially
  new transport/architecture, stop and present the smallest alternatives first.
- **Electron and the mapper must never both bind UDP 5602.** If Electron's direct W3 receiver
  is retained for rollback, the two receiver modes are **explicitly mutually exclusive**: when
  mapper ingest is enabled, Electron closes / does not bind 5602.
- **Fork ownership:** an **owned fork** of elrs-joystick-control (production changes only in
  the fork; `_vendor/` stays an untracked read-only reference pinned to upstream `2b8031a`).
  The exact fork repo path/name/remote/branch is **not yet designated in durable docs** —
  to be approved by the owner before any source change (fork-hygiene rule #3). Fork license
  (GPL-3.0 vs Fair Source, §2.3.5) confirmed/deferred at the same approval point.

Implementation proceeds in slices, first slice = **mapper-owned log-only head-intent ingest
+ read-only diagnostics** (no value ever reaches the node graph / `[16]CRSFValue` / ch9-10 /
serial / firmware / servo). Active authority remains unauthorized.

#### 2.3.8 CB1 slice IMPLEMENTED — mapper log-only ingest (2026-07-15)

Repo/branch: **`w17-mapper` @ `w17-headtrack`** (owned fork of upstream `2b8031a`,
GPL-3.0-or-later). New self-contained Go package **`pkg/headintent`** (`doc.go`, `packet.go`,
`monitor.go`, `receiver.go` + `packet_test.go`, `monitor_test.go`, `receiver_test.go`). No
upstream file modified. Diagnostics are **in-process only** for this slice (owner deferred the
Electron transport).

- **Validation** (`packet.go`): ported 1:1 from the reviewed Windows reference
  (`w17-ground-station/shared/headTracking.js`) and canonical contract §3 (`iPhone_rc` `84532ed`):
  required `seq`/`timestamp_ms` (int ≥0), `yaw/pitch/roll` finite with |yaw|≤360, |pitch|≤180,
  |roll|≤180; `tracking_enabled` bool; optional `centered`/`calibrated` bool, `timeout_ms`
  1..5000; `protocol_version` optional (missing⇒v1, present must ==1). Booleans are not integers.
- **State machine** (`monitor.go`): `idle/invalid/stale/inactive/not_centered/active_log_only`
  (+ receiver `disabled/fault`); **never `active`**. Invalid packets bump counters only and never
  replace the last valid packet. **Receive-time** freshness authority 300 ms — boundary
  **299/300 fresh, 301 stale** (test-proven). `timeout_ms` diagnostic only. seq
  gaps/repeats/regressions, counts, rate, sender-clock delta all exposed read-only.
- **Receiver** (`receiver.go`): non-blocking (own goroutine), **disabled by default**, injectable
  socket/clock; bind failure ⇒ `fault`; plain **exclusive** UDP bind (no `SO_REUSEPORT`) — test
  proves a second binder on the same port faults, so mapper vs Electron 5602 ownership is
  **mutually exclusive by the OS**, not just policy.
- **Evidence (go1.26.5, 2026-07-15):** `go build` + `go vet` clean; `go test -count=1` all green
  (incl. 299/300/301 boundary, invalid-preserves-state, seq diagnostics, fault, real-UDP accept,
  port-exclusivity); `go test -race` clean; `go list -deps ./pkg/headintent/` reaches **no**
  `config/link/crossfire/serial/devices/server/http` package; grep confirms **no existing file
  imports** `headintent` (existing build/outputs unchanged).

**Boundary held:** no head-intent value reaches the node graph, `[16]CRSFValue`, ch9/10, serial,
firmware, servo, or gimbal; no hybrid/rate mapping, sign flips, endpoint conversion, arming,
arbitration, return-to-center, or active pan/tilt. This is **log-only ingest + in-process
diagnostics** — distinct from later **simulated mapper output** (U6) and **physical pan/tilt
validation** (U3/U7; gated A2 + Phase B + FIRST_ACTIVE).

**Deferred / next:** (i) wire the receiver into `cmd` behind a disabled-by-default flag —
**DONE in slice 2, §2.3.9**; (ii) Electron-facing diagnostics transport (owner picks gRPC RPC
vs localhost HTTP later — §2.3.4 / item 14) — **next; decision presented to owner 2026-07-15**;
(iii) shaping + right-stick-wins arbitration + stale-decay = U4, safety-gated. Remaining blockers
unchanged: real iPhone↔Windows validation (U1/CB6), real mounted-axis validation (U5), gimbal
endpoint measurements (U3/CB9), video-loss owner decision (#2/§4), simulated mapping (U6), active
milestone (U7/CB10).

#### 2.3.9 CB8 slice 2 IMPLEMENTED — cmd wiring behind a disabled-by-default flag (2026-07-15)

Repo/branch: **`w17-mapper` @ `w17-headtrack`** (uncommitted). One production file changed —
`cmd/elrs-joystick-control/main.go` — plus one new test file
`pkg/headintent/pack_deadend_test.go`. No dependency, `go.mod`/`go.sum`/`go.work`, or upstream
control-path file changed.

- **Flag/env (disabled by default):** `-headtrack-ingest` (bool, default from env
  `W17_HEADTRACK_INGEST`; explicit flag wins) and `-headtrack-port` (default
  `headintent.DefaultPort` = 5602). When off, **no `Receiver` is constructed and no socket is
  bound** — the gamepad→CRSF path is byte-for-byte identical to upstream. When on, `main`
  constructs a `headintent.Receiver`, `Start()`s it, and defers `Stop()` — **and nothing else**:
  the receiver is not passed to `grpcServer`, `devicesCtl`, `configCtl`, `serialCtl`, `linkCtl`,
  `serverCtl`, or `client.Init`. A bind failure is logged and ignored (never affects control).
- **Byte-for-byte dead-end proof** (`pack_deadend_test.go`, `package headintent`, imports
  `pkg/crossfire` + `pkg/util` **only in the `_test` file**): packs a faithful gamepad→CRSF
  snapshot sequence (stick sweep across all 16 channels via the same `util.MapRange` the axis
  path uses, plus all-min/all-center/all-max edges; 12 frames / 312 CRSF bytes) with
  `crsf.PackChannels`, then repeats the pack while a **real UDP receiver** is running and has
  demonstrably reached its observable state under **valid** (`active_log_only`), **stale**
  (aged past the 300 ms bound → `stale`), and **invalid** (malformed + oversized → `invalid`)
  traffic. Output is asserted **`bytes.Equal`** to the flag-off baseline in all three cases; the
  test fails the receiver did not actually process the traffic, so no arm is vacuous. Shell
  `diff` of the emitted hex dumps (`HEADINTENT_PACK_DUMP=<dir>`): `pack_off.hex` vs
  `pack_on_{valid,stale,invalid}.hex` all **empty (IDENTICAL)**.
- **Evidence (go1.26.5, 2026-07-15):** `go build ./pkg/headintent/` + `go vet ./pkg/headintent/`
  clean; `go test -count=1` and `go test -race` all green (incl. the new dead-end test);
  `go list -deps ./pkg/headintent/` still reaches **no** `config/link/crossfire/serial/devices/
  server/http/client` package (the `crossfire` import is test-only); the only production importer
  of `headintent` is now `cmd/elrs-joystick-control/main.go` (grep-confirmed; no `pkg/` production
  file imports it). `-help` on the built binary shows both flags with the receiver **off by
  default**.
- **Full-app build note (NEW finding, owner-facing):** `go build ./...` on this **macOS +
  go1.26.5** host now succeeds for SDL (`sdl2`+`pkg-config` installed) **and** the web-UI embed
  (`webapp/dist` built), but **fails only in the third-party dep `go.bug.st/serial/enumerator`
  v1.5.0** (`cannot define new methods on non-local type C.*` — go1.26 closed the cgo method
  loophole). This is **pre-existing and unrelated to this change** — it fails identically on a
  pristine `git checkout` of `main.go` and when the dep is built standalone. Verified that
  temporarily bumping `go.bug.st/serial` → `v1.7.1` makes `go build ./...` **fully green**
  (my `main.go` compiles and the app links); the bump was **reverted** because `pkg/serial`
  sits directly in the CRSF **send path**, so changing that dependency is an owner decision, not
  a silent slice-2 edit. Recommendation: approve the `v1.7.1` bump (or build on the Windows host)
  as a separate, reviewed step — it is required for **any** go1.26 build of the fork.

**Boundary held (unchanged from slice 1):** no head-intent value reaches the node graph,
`[16]CRSFValue`, ch9/10, serial, firmware, servo, or gimbal; no mapping, arming, arbitration,
return-to-center, or active pan/tilt. Still **log-only ingest + in-process diagnostics**.

#### 2.3.10 OWNER DECISION — Electron diagnostics transport RESOLVED 2026-07-15 (gRPC; item 14)

Owner picked **gRPC over the existing :10000 service** (not a second HTTP JSON API). This
resolves the item-14 deferral from §2.3.8. **Not yet built** — recorded as the spec for the next
slice (CB8 slice 3).

Spec (owner):
- New **read-only, server-streaming** RPC on the **existing** gRPC service:
  `rpc WatchHeadIntentDiagnostics(google.protobuf.Empty) returns (stream HeadIntentDiagnostics);`
  No acknowledgements, setters, arm/disarm, or any Electron→mapper method. Electron is a
  **subscriber only**, consumed in the Electron **main/preload** layer (never an unrestricted
  renderer). Electron renders the mapper's **authoritative** state — it must not recompute
  freshness or run a second head-intent state machine.
- **Snapshot-on-subscribe**, then **push state transitions immediately**; rate-limit ordinary
  value updates to **~10 Hz**. **Latest-value semantics, bounded 1-item buffer per subscriber**;
  drop superseded snapshots rather than block UDP receive / eval / mixing / CRSF TX. A slow,
  disconnected, or crashed client must have **zero** effect on mapper operation.
- Message: protobuf **enum** state with an `_UNSPECIFIED` zero value (not a raw string);
  server-computed **`receive_age_ms`** (Electron never derives freshness from the iPhone
  timestamp); counts (total/valid/invalid), last valid seq, seq gaps/repeats/regressions, packet
  rate, yaw/pitch/roll, tracking_enabled, centered, sender timeout hint, concise fault info;
  **preserve the last valid packet separately** when current state is invalid/stale. This proto
  is an **internal mapper↔Electron diagnostics API** — it does **not** modify the canonical
  iPhone UDP/JSON bridge contract.
- Tests required: initial-snapshot delivery, immediate state-transition delivery, ~10 Hz rate
  limiting, slow/disconnected subscriber isolation, stale-transition delivery, and **proof that
  diagnostics streaming cannot alter mixer or CRSF output**.
- Explicitly out of scope this slice: no second HTTP endpoint / new port; **do not change the
  existing gRPC bind or security policy**.

**Bind-policy fact (recorded per owner request):** the gRPC server binds
`net.Listen("tcp", ":%d")` → **`[::]:10000`, i.e. all interfaces / externally reachable — NOT
loopback-only** (`pkg/server/controller.go:73`; the grpc-web HTTP port :3000 binds the same way,
`pkg/http/controller.go:65`). Policy left unchanged this slice as instructed; the non-blocking /
bounded-buffer requirements above are what make an untrusted or slow subscriber safe. Tightening
:10000 to loopback (or gating it) is a **separate owner decision**, flagged here.

**Build prerequisites / where the work lands (blockers to surface before slice 3):**
- **Proto toolchain not installed here** — `protoc`, `protoc-gen-go`, `protoc-gen-go-grpc`, `buf`
  all absent. The new RPC needs `pkg/proto/server.proto` edited and **both** the Go stubs
  (`pkg/proto/generated/pb/`) and the grpc-web JS stubs (`webapp/src/generated/`) regenerated —
  needs the toolchain installed (owner approval), or generated on the Windows host.
- **Two repos:** the RPC + server + Go tests live in **`w17-mapper`**; the subscriber-only
  consumer lives in **`w17-ground-station`** (`elrsLauncher.js` / main-process layer) — a
  separate repo/session under the one-repo-at-a-time rule.

##### 2.3.10.1 CB8 slice 3A IMPLEMENTED — mapper-side gRPC diagnostics (2026-07-15)

Mapper side only (Electron consumer = slice 3B, `w17-ground-station`, later). **Uncommitted.**

**Pinned generation toolchain** (installed on this dev host; no tool binaries committed; the
reproducible driver is the new `pkg/proto/generate.sh`, which version-checks each tool):
`protoc v4.23.2` (libprotoc 23.2), `protoc-gen-go v1.30.0`, `protoc-gen-go-grpc v1.3.0`,
`protoc-gen-js v3.21.2` (commonjs+binary), `protoc-gen-grpc-web v1.4.2` (commonjs,
mode=grpcwebtext). These match the versions recorded in the pre-existing generated headers.
**Drift gate passed:** regenerating the *unchanged* proto produced **zero** git diff for all
four artifacts (the SPDX header is copied from `server.proto`'s leading comment by protoc-gen-go,
not prepended); after the proto change, re-running `generate.sh` is idempotent (identical output).

**Proto (`pkg/proto/server.proto`):** added enum `HeadIntentState` (explicit
`HEAD_INTENT_STATE_UNSPECIFIED = 0`, then disabled/fault/idle/invalid/stale/inactive/
not_centered/active_log_only — **no active-control state**), message `HeadIntentDiagnostics`
(state, total/valid/invalid counts, has_last_valid, last_valid_seq, seq gaps/repeats/regressions,
**server-computed** receive_age_ms, rate_per_sec, yaw/pitch/roll, tracking_enabled,
centered+has_centered, sender_timeout_ms+has_sender_timeout, sender_clock_delta_ms, stale_ms,
last_error), and `rpc WatchHeadIntentDiagnostics(Empty) returns (stream HeadIntentDiagnostics)`.
Uses the service's **repo-local `Empty`** (as every existing RPC does) instead of
`google.protobuf.Empty` — same empty request, no new import; noted as an intentional deviation
from the spec's literal wording.

**Regenerated (committed-artifact) files:** `pkg/proto/generated/pb/server.pb.go`,
`pkg/proto/generated/pb/server_grpc.pb.go`, `webapp/src/generated/server_pb.js`,
`webapp/src/generated/server_grpc_web_pb.js`.

**New/changed Go:** `pkg/headintent/broadcast.go` (transport-agnostic `Broadcaster`:
snapshot-on-subscribe, immediate state-transition push, ~10 Hz value-update rate limit,
per-subscriber bounded 1-item latest-value buffer, **4-subscriber cap** → `ErrTooManySubscribers`,
context/disconnect release; READ-ONLY consumer of the receiver snapshot); `monitor.go`/`receiver.go`
(+`LastError` on the diagnostics snapshot); `pkg/server/headintent_stream.go`
(`WatchHeadIntentDiagnostics` RPC + snapshot→pb conversion; nil source → `codes.Unavailable`;
cap exceeded → `codes.ResourceExhausted`); `pkg/server/server_grpc.go` + `controller.go`
(thread `*headintent.Broadcaster` in, nil when ingest off); `cmd/.../main.go` (build the
broadcaster over `receiver.Diagnostics` only when `-headtrack-ingest`, else pass nil).

**Evidence (2026-07-15):** `go build ./...` and `go test ./...` **green**, `go vet` clean (the
only warning is the pre-existing upstream `main.go` unbuffered-signal-channel nit); `pkg/headintent`
and `pkg/server` pass under `-race`; `webapp` webpack build **compiles** the regenerated grpc-web
stubs. gRPC tests (bufconn): disabled→Unavailable, initial-snapshot delivered, 5th stream→
ResourceExhausted, client-cancel releases the subscription (SubscriberCount→0), and the service
descriptor proves the method is **server-stream-only (ClientStreams=false)** = read-only.
Broadcaster tests: initial snapshot, transition bypasses the rate limit, value updates
rate-limited, slow subscriber keeps only latest & never blocks, cap-4/5th-refused, unsubscribe
releases, receive-age is receive-time (not iPhone timestamp), last-valid preserved across
invalid+stale. **CRSF invariance re-proven:** `crsf.PackChannels` output is byte-identical with
the flag off vs on under valid/stale/invalid traffic **and** with diagnostics subscribers
connected, slow, and disconnected (empty `diff` + `bytes.Equal`, `-race`).

**Build-host caveat (unchanged from slice 2):** `go build ./...`/`go test ./...` here required a
temporary `go.bug.st/serial v1.5.0 → v1.7.1` bump to clear the pre-existing go1.26.5 cgo failure in
`.../serial/enumerator` (`pkg/serial` → `pkg/server`, the CRSF send path). The bump was **reverted**;
`go.mod`/`go.sum`/`go.work` are pristine. Approving that bump (or building on Windows) remains a
separate owner decision.

**Bind + hardening (recorded, unchanged this slice):** gRPC still binds `[::]:10000`
(externally reachable). Protection against subscriber fan-out is the **4-stream cap** +
non-blocking bounded buffers, **not** the bind. Loopback/auth hardening of :10000 is a separate
owner decision (§2.3.10). No new port, no HTTP JSON endpoint, no auth scheme, no bind change.

**Safety boundary held:** the RPC is diagnostics-only (mapper→Electron). No Electron control call,
no virtual input node, no head-intent into the node graph / `[16]CRSFValue` / ch9/10 / serial /
firmware / servo / gimbal; no mapping/arming/arbitration/return-to-center. **No Electron
integration yet** — that is slice 3B in `w17-ground-station`.

#### 2.3.11 CB8 slice U4 — head-intent shaping/arbitration DESIGN (2026-07-15, SAFETY-GATED, no code)

**Status: DESIGN ONLY. No code was written, scaffolded, or wired for this slice.** U4 is the
first step in the whole program that could ever turn head motion into commanded gimbal
motion, so it is blocked behind the **FIRST_ACTIVE review** (§2.3.11.6). This section is the
artifact that review consumes; implementation code lands only *after* the review passes, as
the first **gated** slice. Nothing here is authorized, scheduled, or built.

**What this slice changed at runtime: nothing.** The `w17-mapper` receiver stays LOG-ONLY,
the Electron consumer stays display-only, `HeadIntentState` gains **no** active-control enum
value (the proto enum still ends at `HEAD_INTENT_STATE_ACTIVE_LOG_ONLY = 8`,
`pkg/proto/server.proto:527`), and nothing new touches the node graph, `crsf.PackChannels`,
ch9/ch10, servos, the gimbal, or the ESC. Firmware stays iPhone-unaware. Deliberately **no
scaffolding** was added to the mapper: shaping/arbitration code — even dead-ended — would
bake in constants and a control-path stage the FIRST_ACTIVE review has not yet approved, so
it waits for the review. Proof the mapper is unchanged is in §2.3.11.7.

##### 2.3.11.1 Arbitration authority — mapper-only, single post-node-graph choke point

Authority for turning head intent into commanded ch9/ch10 is the **mapper, and only the
mapper**: not firmware (stays iPhone-unaware — parses no iPhone JSON/UDP), not Electron
(viewer/config/log-only), not the iPhone (thin client). This is unchanged from §0/§2.1.

**Design refinement (supersedes the in-graph-node sketch of §2.3.3 / §2.3.5):** the
head-intent shaping + arbitration is a **single post-node-graph stage**, not a set of nodes
inside the user-editable mixer graph. The stage sits in the send path between the eval
loop's computed `[16]CRSFValue` for the CRSF port and `crsf.PackChannels`
(`_vendor/elrs-joystick-control/pkg/link/send.go:118-144`). Rationale:

- **One auditable point** for every head-derived motion, instead of safety logic spread
  across graph leaves a user could rewire in the config UI.
- **The flag-off = byte-identical invariant becomes trivially provable:** with the stage a
  pure identity passthrough (its default and only state until FIRST_ACTIVE), the array
  handed to `crsf.PackChannels` is bit-for-bit the eval output, so the whole CRSF stream is
  unchanged (§2.3.11.5, matrix Group A).
- The node graph and `InputAxis` right-stick path (§2.3.1) stay **exactly** as upstream; the
  arbiter only ever *reads* the graph's ch9/ch10 and the receiver's read-only `Diagnostics`
  snapshot, and *replaces* ch9/ch10 (only) on its way to the packer.

The arbiter is a pure function of (eval `[16]CRSFValue`, `headintent.Diagnostics` snapshot,
arbiter memory, monotonic now) → `[16]CRSFValue`. It touches **only** indices 8/9 (ch9/ch10
= pan/tilt; `ChannelDecoder.hpp:20-21`). It never writes back into the receiver, the monitor,
the broadcaster, the node graph, or firmware.

##### 2.3.11.2 The shaping/arbitration model

All of the following describe the **future gated** arbiter. Commanded center is CRSF **992**
(§1.3); "counts" are 11-bit CRSF units (172…1811, span 1639, center 992).

1. **Deadband.** Head angles within a small band around center map to exactly 992 (no
   commanded motion). Deadband is expressed in **degrees of head angle** (the natural unit of
   the input) and converted to counts via the same deg↔count table U3/CB9 measures on the
   real mount (§3 U3) — it is **not** a guessed count value. Purpose: kill sensor jitter and
   micro-drift so a still head holds a still camera. Exact width is a FIRST_ACTIVE-reviewed
   constant (§2.3.11.6), not chosen here.
2. **Rate limit (slew).** Commanded ch9/ch10 may change by at most `maxRate` counts per
   second. A step in head angle produces a **ramp**, never a jump. Applied to **every**
   output transition, including failsafe decay and override (U4 row, §3).
3. **Acceleration limit.** The *change in rate* is bounded by `maxAccel` counts/s² so the
   ramp itself eases in/out rather than starting/stopping instantly — protects the gearing
   from commanded snap. Secondary to the rate limit; a reviewed constant.
4. **Freshness gate — 250 ms for active, distinct from the 300 ms log-only boundary.**
   - The **300 ms receive-time** boundary (§1.1, `DefaultStaleMs = 300`,
     `pkg/headintent/packet.go:19`; 299/300 fresh, 301 stale) is the **log-only / diagnostic
     classification** authority — it decides `StateStale` vs `StateActiveLogOnly` and is what
     the receiver and broadcaster already report.
   - The **active** path uses a **stricter ≤ 250 ms** freshness gate (§1.2 active-motion row,
     ratified contract §3): head-derived motion is permitted only while the mapper's
     receive-time age is ≤ 250 ms. Age 251 ms → the arbiter treats intent as not-fresh and
     runs the failsafe decay (item 6), even though the diagnostic state could still read
     `active_log_only` up to 300 ms. **The active gate is always at least as strict as the
     log-only boundary; it must never be relaxed past 300 ms.**
   - Independently, the **iPhone sender** must stop emitting packets when its Core Motion
     sample is older than 250 ms before any active use (§1.2, Codex-side, contract-ratified).
     These are two independent 250 ms guards — sender-side sample age and mapper-side receive
     age — and the mapper never trusts the iPhone timestamp for freshness (receive time only).
5. **Center / enable / arm preconditions (all required, every tick).** Active head-derived
   ch9/ch10 is permitted **only** while *all* of these hold simultaneously:
   - **enabled** — `tracking_enabled == true` in the last valid packet (drives
     `StateInactive` today, `monitor.go:137`);
   - **centered** — `centered == true` **and** not `calibrated == false` (the exact
     `StateNotCentered` guard, `monitor.go:141-142`);
   - **fresh** — receive-time age ≤ 250 ms (item 4), i.e. a stricter subset of
     `StateActiveLogOnly`;
   - **armed** — an **explicit operator arm action** (a physical/gamepad control on the
     mapper host or a reviewed operator affordance). Arming is never automatic, never derived
     from the iPhone, and never inferred from "conditions look good."
   - **FIRST_ACTIVE flag** — the compile-time **and** runtime flag of §2.3.11.4 is on.
   Losing **any** precondition drops out of active immediately into failsafe decay (item 6).
6. **Failsafe behavior — decay to commanded center, reconciled with the hold-vs-center owner
   decision.** On loss of freshness (> 250 ms), disarm, loss of center/enable, or receiver
   `fault`, the arbiter **rate-limited-decays commanded ch9/ch10 to exactly 992** (§1.3) and
   **discards** any `virtualCameraCenter` hybrid offset. It does **not** hold the last
   off-center command. This is the *mapper/intent* failsafe layer and is **distinct from the
   firmware/radio failsafe layer**:
   - *Mapper layer (this design):* intent goes stale but the **radio link is still up**, so
     the firmware simply follows the mapper's decay to 992 — an authoritative *commanded*
     center whose physical safety still needs blocker-1 bench validation (§1.3).
   - *Firmware layer (unchanged, still an owner decision):* on **radio** loss the firmware
     applies its own gimbal failsafe — today hold-last. Whether that becomes hold-vs-center is
     the open **failsafe hold-vs-center** decision — **CURRENT_STATUS.md owner decision #3 /
     this doc §5 item 2 / step U8** (same decision, reconciled here across the two numbering
     schemes). The mapper's active decay-to-992 on intent-stale directly *reduces* the
     concern U8 raises (that the hybrid mapping makes sustained off-center positions routine,
     raising the stakes of firmware hold-last): under normal intent loss the camera is already
     commanded home before any radio-layer failsafe would apply. U8 remains an owner decision;
     this design does not resolve it, it de-risks it.
7. **Right-stick-wins arbitration, no auto-restore.** If the manual right stick (the existing
   `InputAxis` ch9/ch10 path, §2.3.1) is deflected beyond a reviewed threshold, the manual
   value **wins immediately** and head intent is suppressed. Returning the stick to center
   does **not** auto-restore head control: override **latches** until the operator explicitly
   re-arms (item 5) — mirroring the no-auto-restore rule already in the U4 row (§3) and the
   readiness "manual override" blocker (readiness §8 item 4). Re-arming re-seeds the virtual
   center from the mapper's authoritative final **commanded** value, never a claimed measured
   servo angle (§1.3/§1.4).
8. **Every authority transition is rate-limited.** Arm, disarm, override-engage,
   override-release-then-rearm, stale-entry, and fault-entry all route through the item-2/3
   limiter. No transition may step the output; there is exactly one path to a new commanded
   value and it is the rate/accel-limited ramp.

##### 2.3.11.3 Safety invariants U4 code must hold

Restating §0 in U4-specific, testable terms. Every one of these is an assertion the future
gated code must prove (test matrix, §2.3.11.5):

- **I1 — Inactive ⇒ byte-identical CRSF.** With the FIRST_ACTIVE flag off (its default and
  the only state until review), `crsf.PackChannels` output is bit-for-bit identical to the
  no-arbiter build, under all head-intent traffic (none/valid/stale/invalid) and all
  diagnostics-subscriber states. The arbiter is a pure identity passthrough.
- **I2 — Active is multi-gated.** A non-992 head-derived ch9/ch10 is reachable **only** when
  *all* of: FIRST_ACTIVE compile flag on **AND** FIRST_ACTIVE runtime flag on **AND** armed
  **AND** `tracking_enabled` **AND** centered **AND** fresh (≤ 250 ms). Dropping any single
  one forces failsafe decay to 992.
- **I3 — Default build cannot ever be active.** In the default build (no FIRST_ACTIVE compile
  tag) there is **no reachable code path** to a non-passthrough arbiter — proven at
  compile/link level, not just by runtime default.
- **I4 — Head intent touches ch9/ch10 only.** No arbiter path writes any channel other than
  indices 8/9; throttle/steer/DRS/arm/gear are never a function of head intent.
- **I5 — Failsafe is decay-to-992, never hold-off-center, never step.** Every exit from
  active reaches 992 through the rate limiter and discards `virtualCameraCenter`.
- **I6 — No auto-arm, no auto-restore.** Arming and post-override/-stale re-entry require an
  explicit operator action; nothing about "good conditions" arms or restores.
- **I7 — Receive-time freshness only.** The active gate uses monotonic receive-time age; the
  iPhone `timestamp_ms` is never a freshness authority (it stays a diagnostic delta,
  `monitor.go:170`).
- **I8 — One-way, read-only inputs.** The arbiter only *reads* the node-graph output and the
  receiver's `Diagnostics` snapshot; it never writes back into the receiver/monitor/
  broadcaster/graph, and the firmware and Electron remain unaware of it.
- **I9 — Firmware/Electron boundaries intact.** Firmware still parses no iPhone data;
  Electron's `noControlPath` property is untouched (the arbiter lives in the mapper, not the
  Electron process).

##### 2.3.11.4 The FIRST_ACTIVE flag (compile-time AND runtime, both default off)

Two independent gates, **both** required for any active output; neither alone is sufficient:

- **Compile-time** — a build tag / const (working name `W17_FIRST_ACTIVE`), **absent by
  default**. When absent, the active arbiter branch is **not built** (or compiles to a
  `const firstActive = false` dead branch the linker drops), so the shipped default binary
  physically cannot produce head-derived motion (invariant I3). Any shared/CI build stays
  default (flag off) until the review explicitly authorizes flipping it.
- **Runtime** — even in a FIRST_ACTIVE build, an explicit runtime enable (flag/env, working
  name `W17_FIRST_ACTIVE_ARM` or equivalent, **default off**) plus the per-tick **armed**
  precondition (§2.3.11.2 item 5) are required. Runtime-on alone in a non-FIRST_ACTIVE build
  does nothing (there is no active branch to reach).

Analogy to the existing ingest flag: `-headtrack-ingest` is already default-off and gates the
*receiver*; FIRST_ACTIVE is a **second, stricter** gate that additionally gates the *arbiter*,
and it is off even when ingest is on. Ingest-on + FIRST_ACTIVE-off = today's log-only behavior.

##### 2.3.11.5 Exact test matrix U4 code must prove (before the flag is ever flipped)

The future gated implementation is not accepted until every row below is green. Grouped by
invariant. (This slice writes none of these — it specifies them.)

**Group A — Inactive byte-identity (proves I1, I3, I4). Flag OFF.**

| # | Test | Pass condition |
|---|---|---|
| A1 | Full 16-channel gamepad→CRSF sweep (reuse the `pack_deadend_test.go` vectors: stick sweep + all-min/center/max, 12 frames/312 bytes) packed with the arbiter compiled-in but flag OFF, under no/valid/stale/invalid head-intent traffic | `bytes.Equal` to the no-arbiter baseline **and** empty `diff` of hex dumps, all cases |
| A2 | Same as A1 with diagnostics subscribers connected / slow / disconnected | byte-identical in every case |
| A3 | Default build (no FIRST_ACTIVE tag): an exported `CanEverBeActive()`-style predicate | returns **false**; a build-tag test proves the active branch is absent from the default binary |
| A4 | Arbiter fuzz: random ch9/ch10 eval inputs, flag OFF | output ch9/ch10 == input ch9/ch10 exactly (identity) |
| A5 | `go list -deps` / import audit | firmware and Electron never import the arbiter; the arbiter never imports the receiver's writers, only its read-only snapshot |

**Group B — Active gating (proves I2, I5, I6, I7). FIRST_ACTIVE build, bench/sim output only.**

| # | Test | Pass condition |
|---|---|---|
| B1 | Precondition drop-out, table-driven: start fully-armed+fresh+centered+enabled+flag-on, then negate exactly one of {compile flag, runtime flag, armed, tracking_enabled, centered, fresh} | in every single-negation case commanded ch9/ch10 decays to **992** |
| B2 | Deadband | head angle within the reviewed band ⇒ commanded stays 992 |
| B3 | Rate limit | a step head input ⇒ per-tick |Δcount| ≤ `maxRate·dt`; never a jump |
| B4 | Acceleration limit | rate change per tick ≤ `maxAccel·dt` |
| B5 | Freshness boundary | receive age 249/250 ms ⇒ active permitted; 251 ms ⇒ decay to 992 (distinct from the 299/300/301 log-only boundary, both proven in the same test) |
| B6 | Stale/disarm/fault ⇒ decay | reaches exactly 992 via the limiter (no step) and `virtualCameraCenter` is discarded |
| B7 | Freshness authority | ingest with a skewed iPhone `timestamp_ms` never changes the active/decay decision (receive-time only) |
| B8 | ch9/ch10-only | throttle/steer/DRS/arm/gear outputs are byte-identical to the no-head-intent baseline for every Group-B case |

**Group C — Arbitration & no-auto-restore (proves I6, I8, I9).**

| # | Test | Pass condition |
|---|---|---|
| C1 | Right-stick-wins | stick deflection > threshold ⇒ manual value wins immediately, head intent suppressed |
| C2 | No auto-restore | returning the stick to center does **not** restore head control; output stays manual/decayed until an explicit re-arm |
| C3 | No auto-arm | no combination of fresh+centered+enabled arms the arbiter without the explicit operator arm action |
| C4 | Re-arm re-seeds virtual center | re-arm seeds from the authoritative final **commanded** value, never a claimed measured angle |
| C5 | Transition rate-limiting | arm/disarm/override/stale/fault transitions each ramp — no step at any transition |
| C6 | Read-only inputs | the arbiter never mutates receiver/monitor/broadcaster/graph state (verified by a state-diff harness) |

##### 2.3.11.6 FIRST_ACTIVE review checklist — must ALL pass before any U4 code is wired

This is the Claude-side gate that must be satisfied **before the first line of arbiter code is
committed or the FIRST_ACTIVE flag is ever added to a build**. It does not replace the
Codex-owned `iPhone_rc/docs/FIRST_ACTIVE_PAN_TILT_MILESTONE.md` — where they disagree, the
Codex milestone gates movement (§ top-of-doc). Both must pass.

- [ ] **R1** Codex-owned FIRST_ACTIVE milestone checklist passed and its go/no-go table filled
      with evidence.
- [ ] **R2** All 7 blockers of `iphone_pan_tilt_firmware_readiness.md §8` green, or explicitly
      deferred with recorded owner sign-off.
- [ ] **R3** Owner decision **#2 video-loss reaction** (§4) resolved — the arbiter cannot
      safely go active while "blind driver, camera keeps following" is unresolved.
- [ ] **R4** Owner decision **#3 / U8 failsafe hold-vs-center** (radio-loss firmware behavior)
      recorded either way, reconciled with the mapper decay-to-992 design (§2.3.11.2 item 6).
- [ ] **R5** Owner decision **#5 first head-tracked driving protocol / spotter** recorded;
      first active is bench-only, wheels off, tiny limits, slow rate, observer present.
- [ ] **R6** **A2 closed + Phase B approved** for any *powered* validation. (U4 *code* runs
      with physical output disconnected/simulated — U6 — and does not itself need Phase B; the
      powered servo sweep is U7/CB10 and does.)
- [ ] **R7** Real gimbal mechanical endpoints measured (U3/CB9) and the **deg↔CRSF-count
      table** recorded, so deadband/rate/accel/limits are real, not guessed.
- [ ] **R8** iPhone axis/mount validation (U5, Codex Batch 5) done — real signs, ranges, roll
      isolation — so the mapping isn't calibrated against assumptions.
- [ ] **R9** Real iPhone↔Windows **log-only** bridge validation (U1/CB6) done end-to-end.
- [ ] **R10** iPhone sender-side 250 ms sample-age suppression (§1.2, Codex) implemented and
      verified — the sender half of the freshness contract.
- [ ] **R11** Fork repo path/name/remote/branch **and** license (GPL-3.0 vs Fair Source,
      §2.3.5) approved and recorded (fork-hygiene rule #3).
- [ ] **R12** The exact shaping constants (deadband width, `maxRate`, `maxAccel`, override
      threshold, arm affordance) reviewed and signed off — no placeholder constants ship.
- [ ] **R13** The full §2.3.11.5 test matrix (Groups A/B/C) implemented and **green**, with the
      byte-identical-while-inactive proof (Group A) demonstrated on the default build, before
      the flag is flipped in any shared build.
- [ ] **R14** A written rollback: how to return to log-only (flag off ⇒ identity passthrough,
      already the default) and how Electron/mapper 5602 mutual exclusivity is preserved.

##### 2.3.11.7 This slice's proof — mapper unchanged, CRSF byte-identical

Because U4 added **no** mapper code, the byte-identity and isolation invariants hold trivially
and were re-confirmed (go1.26.5, 2026-07-15):

- `git status` in `w17-mapper` is clean at `59d1739` — **zero** files changed by this slice.
- `go test -count=1 ./pkg/headintent/` **green**, including `pack_deadend_test.go`, which is
  the standing proof that `crsf.PackChannels` is byte-identical (`bytes.Equal` + empty hex
  `diff`) with the head-intent receiver off vs on under valid/stale/invalid traffic. With no
  code change, that proof is unchanged.
- The proto is untouched: `HeadIntentState` still ends at `ACTIVE_LOG_ONLY = 8`
  (`pkg/proto/server.proto:527`) — **no active-control enum value added.** The GS
  proto-drift guard therefore stays green with no `npm run proto:check` regeneration needed.
- Nothing new references any arbiter (there is none) from `cmd` / `pkg` / `pkg/server`.

**Boundary held:** no head-intent value reaches the node graph, `[16]CRSFValue`, ch9/ch10,
serial, firmware, servo, gimbal, or ESC; no shaping, arming, arbitration, or active pan/tilt
exists in code. This remains **log-only ingest + read-only diagnostics** (slices 1–3C); U4 is
**design only**, gated behind §2.3.11.6.

## 3. Ordered unlock sequence

Blocker numbers refer to `iphone_pan_tilt_firmware_readiness.md §8`.

| Step | Work | Blocker | Owner | Gate |
|---|---|---|---|---|
| U1 | Validate the existing log-only Windows bridge end-to-end with a **real iPhone** (fake-sender validation exists; real-device pending) | 5 | Claude (GS) + real device | non-isolated bench network (`CURRENT_STATUS.md` pending validations) |
| U2 | Read-only investigation of elrs-joystick-control ingest capability; then **owner decision** on §2.3 (a)/(b)/(c) and fork ownership | — (prereq for 2,3,4) | Claude investigates; owner decides | none (read-only) |
| U3 | Bench-measure real gimbal mechanical endpoints on the assembled mount → per-axis `gimbalConfig` `ServoConfig` values; record the **deg ↔ CRSF-count conversion table** at the same bench session (the Codex milestone expresses limits in degrees; the wire carries counts; firmware owns µs — the conversion is bench evidence and lives here in `project-review/`) | 1 | Claude (fw) + bench | **HARD GATE: A2 closed + Phase B approved. This document does not authorize powered-bench work.** Requires `CAMERA_GIMBAL_PLACEMENT.md` mount decision + printed mount |
| U4 | Implement the mapper in the chosen host (per U2): deadband, low-pass, slew limit; stale-decay-to-992 per §1.1/§1.3; right-stick-wins arbitration with **no auto-restore** (re-arm = explicit operator action); rate limiting applied across **every** authority transition (arm, disarm, override, stale) so no transition can step the output. **Design recorded in §2.3.11 (2026-07-15); code is GATED behind the §2.3.11.6 FIRST_ACTIVE review — not yet authorized.** | 2, 3, 4 | Claude (mapper host per U2) | U2 decision **+ FIRST_ACTIVE review (§2.3.11.6)** |
| U5 | iPhone axis/mount validation in the EMV400 (Codex Batch 5) — signs, ranges, roll isolation | 6 | Codex | real device |
| U6 | Simulated-output integration: mapper computes hybrid output with physical output disconnected; every safety transition proven in logs (Codex Batch 7 equivalent, run on the U2-chosen host) | 2, 3, 4 verification | Claude + Codex test vectors | U4, U5 |
| U7 | Bench-only scripted ch9/10 servo sweep, car immobilized/wheels off, tiny limits, slow rate | 7 | Claude (fw bench) + one observer | **Phase B + FIRST_ACTIVE milestone checklist** |
| U8 | Record the **failsafe hold-vs-center re-decision** (firmware behavior on *radio* loss; today: hold-last, `main.cpp` gimbal tick comment / readiness §2.3.5). The hybrid mapping makes sustained off-center positions routine, which raises the stakes of hold-last. Owner decision; document either way. | re-decision | owner | before any head-tracked driving |

Firmware-side work items that fall out of U3/U4 (all identified in readiness §4, none
authorized yet): wire `gimbalConfig` into the tuning console (§4.9), gimbal endpoint/clamp
unit tests against real values (§4.10), optional defense-in-depth slew limiter (§4.2).
Deferred cosmetic item: the stale comment at `lib/channels/include/channels/ChannelDecoder.hpp:57-58`
("decoded but unwired until the gimbal deliverable") — a code-file edit, out of scope for
this documentation pass.

## 4. Video-loss behavior — unresolved active-safety decision (do not resolve silently)

The W3 intent packet has **no iPhone-local decoder-health / video-health field** — its
fields are exactly `seq`, `timestamp_ms`, `yaw_deg`, `pitch_deg`, `roll_deg`,
`tracking_enabled`, `centered`, `timeout_ms`
(`iPhone_rc/FPVHUDApp/Models/HeadTrackingPacket.swift:3-23`; contract §3) [C]. The Codex
VR plan deliberately keeps head tracking independent of the video receiver ("a temporary
video failure does not itself stop motion packet generation").

Consequence: if the driver's iPhone video dies while head tracking is active, the mapper
**cannot know** and will keep following a blind driver's head. Any mapper-enforced freeze
or return-to-center on iPhone-local video loss would require one of:

1. a reviewed W3 schema field (video/decoder health) — mirrored schema + examples change;
2. a reviewed side channel;
3. deliberate packet suppression by the iPhone on video loss (an iPhone behavior change);
4. operator action only (accept the risk; document it in the milestone runbook).

**This is an open owner decision.** Flagged to Codex as handoff item H9. Nothing in this
plan selects an option.

## 5. Open owner decisions (consolidated)

1. ~~Mapper port/ingest architecture — §2.3 (a)/(b)/(c) + fork ownership.~~
   **RESOLVED 2026-07-15: topology (a), owned fork owns 5602, Electron viewer-only,
   receiver modes mutually exclusive, no relay (§2.3.7).** Remaining sub-item: exact
   owned-fork repo path/name/remote/branch + fork license — owner to approve before any
   source change (fork-hygiene rule #3).
2. Firmware failsafe on radio loss: hold-last vs return-to-center (U8).
3. Video-loss reaction path (§4, options 1–4).
4. Camera placement: driver-seat vs halo-height
   (`w17-3d-codex/CAMERA_GIMBAL_PLACEMENT.md`).
5. First head-tracked driving protocol (Codex Batch 9 proposes no spotter; every safety
   doc so far authorizes bench only — recommend a separate reviewed gate).
