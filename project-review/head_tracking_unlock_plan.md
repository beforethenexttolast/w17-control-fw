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
  a silent slice-2 edit. Recommendation at the time: approve the `v1.7.1` bump (or build on the
  Windows host) as a separate, reviewed step — it is required for **any** go1.26 build of the
  fork. **Owner decision 2026-07-15: the v1.7.1 bump was APPROVED as a future isolated slice —
  constraints in §2.3.12.9 item 2.**
- **RESOLVED 2026-07-25 — shipped as `v1.5.0 → v1.6.0`, not v1.7.1** (`w17-mapper` `f0a18f3`,
  2 files / +3 −1). `go build ./...` is now **fully green**; the go1.26 × cgo blocker is
  cleared (was 11 cgo errors across 6 packages). The isolated slice found that **v1.7.1 pulls
  collateral the approval did not cover** — it drags `golang.org/x/sys` v0.8.0 → v0.43.0 and a
  `go` directive bump **1.20 → 1.25.0 in both `go.mod` and `go.work`**, raising the module's
  language version — **including go1.22 per-iteration loop-variable semantics** — for the whole
  module, CRSF send path included. That is a behavioural change to every range loop as a side
  effect of a serial-driver bump. v1.6.0 avoids it entirely. Full reasoning, evidence, and the
  preserved v1.7.1 reachability analysis: **§2.3.12.9 item 2**.

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

###### Input-provenance rule (added 2026-07-30; **premise corrected 2026-08-04**) — arm/deadman/override must NOT come from the channel array

**The arbiter must not read arm, deadman, or manual-override state from the eval
`[16]CRSFValue` array.** That array carries **no validity channel**: a value read out of it
cannot say whether it came from a live device on this tick. Sourcing a safety precondition
from it silently defeats I2.

The rule is unchanged. Its **original justification is not.** The specific defect it was
written against on 2026-07-30 — hold-last in the fork's channel assembler — was fixed the same
day and hardened twice since. The trace below is kept in the **past tense** as the record of
why the rule exists; what replaced it, and why the rule still binds anyway, follow it.

**The mechanism as it stood on 2026-07-30** (line numbers as read then, at `w17-mapper`
`0e11d6b`; **history, not current behaviour**):

1. `pkg/config/input_button.go:77` — when the gamepad was absent from the device registry,
   `GetInputGamepad` returned `!ok` and the node returned **`nan=true`**. Same shape at
   `input_axis.go:87` and `input_hat.go:52`.
2. `pkg/config/output_tx.go:43` — the channel assembler:
   ```go
   _, out, ch, nan = ic.Eval(c)
   if nan || ch < 1 || ch > 16 { continue }
   (*i.Values)[ch-1] = util.CRSFValue(out)
   ```
   `i.Values` was a **persistent `*[16]util.CRSFValue` struct field**, allocated once and
   mutated in place across eval ticks, never reset to a neutral value at the top of a tick.
   So `continue` did not mean "neutral" or "invalid" — the channel **retained its value from
   the previous tick, indefinitely, for as long as NaN persisted.**

The failure it produced, absent the rule: operator in `ACTIVE` with the D-pad DOWN deadman
held; the USB gamepad drops (cable, hub, driver hiccup); the deadman channel **latches at its
held value**; head intent is still fresh/centered/enabled with flags on, so the arbiter reads
`armed == true` and **stays ACTIVE**, head motion still commanding the gimbal. The C1
right-stick override is dead too, because it reads the same frozen array. The only remaining
remedy is killing the mapper process or the radio.

**What the fork does now** — three commits on `w17-headtrack`, source read at HEAD `432a809`,
not commit messages:

- **`2dc7c5a`** (2026-07-30, the same day this rule was written) — a `nan` channel is **driven
  to its configured failsafe value** (`ChannelT.Failsafe`, defaulting to `util.CRSFCenterValue`
  = 992) instead of being skipped by the assembler; and `Config.GetInputGamepad` now gates on
  device presence (`InputGamepad.Attached()`), so a **detached gamepad no longer resolves by id
  at all** (`pkg/config/config.go:26-38`). Transmitter arrays also start centered rather than
  zeroed (`centeredValues`, `output_tx.go:31-36`) — a zero array is not neutral on the wire.
- **`e452d55`** (2026-08-03) — the neutral is resolved from the **owning `channel` node**
  rather than from the top-level holder, and frames are **suppressed across a config swap**.
- **`c60843e`** (2026-08-04) — neutralization is **per owner**, so a channel that stops
  resolving is railed even when its holder still reports healthy; and a **truncated**
  channel-owner walk fails safe.

Verified by reading `pkg/config/output_tx.go` at `432a809`: `centeredValues()` exists (`:31`),
the assembler's neutralizing paths sit at `:278-296` and `:308-326`, and
`OutputTransmitter.Unresolved` (`:59`, atomic) gates transmission — the send loop suppresses a
port's frames when the channel-owner walk cannot account for every channel
(`pkg/link/send.go:107`, used at `:319-323`). **Hold-last in the assembler is gone.**

**Why the rule still binds — it was never only about hold-last:**

1. **The fix changes what a stale slot *contains*, not whether the array carries provenance.**
   A stale channel now reads as its configured failsafe — by default **992**, which is exactly
   what a live, centred control reads. "Neutral" is not "valid". And `ChannelT.Failsafe` is a
   **per-channel, user-configurable** field, so a config can give a channel a non-neutral
   failsafe that reads as an active command.
2. **The original end-to-end failure is still reachable for switch channels**, now through the
   firmware decoder rather than the mapper assembler: 992 normalizes to **0**, which sits
   inside this firmware's ±250 hysteresis dead band (`switchOnAbove = 250` /
   `switchOffBelow = -250`, `ChannelDecoder.hpp:38-39`), and `decodeSwitch` **holds the
   previous state** in between (`ChannelDecoder.cpp:78-84`). A deadman carried on a switch
   channel, with the shipped default failsafe, therefore still latches across a dropout. That
   is RESIDUAL A in `CURRENT_STATUS.md` — a **config** obligation (`failsafe: 172` on the six
   `decodeSwitch` channels), open, with no mapper config on this Mac yet carrying it.
3. **R15 is open and no U4 code exists**, so nothing has been demonstrated against the real
   SDL/OS device-removal path — the fix is verified by unit tests and source reading, not by a
   physical unplug.
4. **Provenance is an architectural requirement, not a workaround for one upstream bug.** The
   assembler's behaviour is a mapper-internal decision a future rebase onto upstream could undo
   without anyone noticing; the arbiter's safety must not depend on it.

**Required instead:** the arbiter derives arm/deadman/override from a source carrying
**explicit validity** — query the device layer directly and treat *device absent* as disarm,
with a positive liveness signal (a monotonic per-device sequence, or the SDL removal event via
`Controller.AlertDeviceChan`, `pkg/devices/controller.go:106`) rather than inferring liveness
from a value that cannot distinguish held, neutral-by-failsafe, and genuinely centred.

Stated as a rule because it is exactly the constraint that gets quietly violated during
implementation, when the channel array is right there and convenient.

> **Scope note (rewritten 2026-08-04 — the defect it described is closed; two residuals are
> not).** As written on 2026-07-30 this note said the same hold-last behaviour affected
> **every** gamepad-driven channel including throttle and steering, that a dropout froze the
> last command, and that it was a pre-existing defect tracked separately. **The freeze is
> closed** by the three commits above: a dropped gamepad now drives every affected channel to
> its configured neutral instead of holding. Two things that closure does **not** cover, which
> is why this note stays:
>
> - **Switch channels still latch downstream** on the shipped default failsafe (992 ⇒
>   normalized 0 ⇒ inside the firmware's ±250 dead band ⇒ `decodeSwitch` holds). Arm, DRS,
>   gear up/down, boost and overtake are affected: the car goes neutral but stays *armed*.
>   RESIDUAL A in `CURRENT_STATUS.md` — open, config-class.
> - **The mapper still transmits at full rate on input loss** — fail-to-neutral, not
>   fail-silent — so the firmware's radio-loss failsafe still does **not** fire on a gamepad
>   dropout. That is by design. Frame suppression exists only for no-config (`630ea96`), a
>   config swap (`e452d55`), and a truncated owner walk (`c60843e`).
>
> Neither is closed by I10/R15, which bind only the future arbiter. Status and residuals for
> the stick path are tracked in `CURRENT_STATUS.md`, not here.

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
     this design does not resolve it, it de-risks it. **[Updated 2026-08-16: the firmware
     layer now decays to center — vision decision 11, implemented on
     `feat/gimbal-decay-center`; see the §2.3.12.2 amendment. The driving-readiness
     re-review is still owed, and the two-layer separation described here is unchanged —
     the mechanisms now also match in character (both rate-limited decays, never a step).]**
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
- **I10 — Loss of the arm/deadman input *device* is a disarm, identical to release.** (Added
  2026-07-30.) Gamepad disappearance and gamepad button-release must reach the same arbiter
  state. Arm/deadman/override state is sourced per the input-provenance rule (§2.3.11.1), never
  from the `[16]CRSFValue` array, which carries no validity of its own; "device absent" is an
  affirmative disarm, not an absence of evidence. Device *disappearance* and value *release*
  are distinct events and both are tested (D15 covers release; D19–D22 cover disappearance).

##### 2.3.11.4 The FIRST_ACTIVE flag (compile-time AND runtime, both default off)

Two independent gates, **both** required for any active output; neither alone is sufficient:

- **Compile-time** — a **Go build tag, and only a build tag**: `//go:build w17_first_active`,
  **absent by default**. When absent the active arbiter file is **not compiled at all** and the
  default build links a passthrough stub, so the shipped default binary physically cannot
  produce head-derived motion (invariant I3). Any shared/CI build stays default (flag off)
  until the review explicitly authorizes flipping it.

  > **The `const` alternative is DELETED (2026-07-30), not demoted to a fallback.** Earlier
  > revisions allowed *"or compiles to a `const firstActive = false` dead branch the linker
  > drops."* That branch of the fork **silently disarms the shipped accident guard.**
  > `w17-mapper/.githooks/pre-push` (enabled in this clone, `core.hooksPath = .githooks`)
  > scans code with exactly two patterns: lowercase **`w17_first_active`** across
  > `*.go *.sh *.yml *.yaml *.mk Makefile Dockerfile*`, and the uppercase word
  > **`FIRST_ACTIVE`** across `*.go *.proto`. The doc's own suggested identifier
  > `const firstActive = false` matches **neither** — not the lowercase tag, not the uppercase
  > word — so arbiter code gated that way would pass both checks and reach a **public** remote.
  > The two mechanisms also differ in what they can prove: a build tag makes the branch
  > *absent* (assertable by symbol absence); a const makes it *eliminated*, compiled and
  > type-checked, flippable by a one-character edit. I3's "physically cannot" is true of the
  > first and rhetorical about the second.
  >
  > **Naming contract with the guard:** the tag **must** be lowercase `w17_first_active`
  > exactly, because that is the literal the hook greps. Renaming the tag without updating the
  > hook silently voids check 1.
  >
  > **Owed in `w17-mapper` (separate pass, not done here):** extend the hook's own verification
  > to confirm it *misses* the `const firstActive` form, and record that miss as a documented
  > limit rather than assuming coverage. The existing "verified to bite on all three
  > injections" evidence does not cover it. Not actioned in this pass because `w17-mapper` is
  > a different repo with a live session in it.
  >
  > **DONE — closed 2026-08-04 in `w17-mapper`, recorded here 2026-09-03.** The pass did better
  > than what was owed: rather than only documenting the miss, it closed the specific name.
  > `.githooks/pre-push` gained **check 4** — `first_?active` matched **case-insensitively**
  > over the code/build globs (`.githooks/pre-push:18-22`, grep at `:94-101`) — so the
  > `const firstActive = false` form that once passed clean is now REFUSED. The hook's own
  > VERIFICATION MATRIX (`:40-49`) records all five injections *including that reversal*
  > (`I4 const firstActive = false -> REFUSED by check 4 (was ALLOWED before check 4)`), and
  > it keeps the residual **class** limit explicit rather than implying coverage:
  > `I5 const enableShaping = false -> ALLOWED. Not a bug; the documented class limit`
  > (`:27-32`). Check 4's false-positive surface over the in-scope globs was **measured** at
  > zero, with the measurement's own scope stated (`:50-55`) — the zero-hit result stands, though the
  > same line miscounts the `.sh` files in scope (2, not 3; `[fix-wave: MAP-16]`), which is worth
  > correcting precisely because it is a standing claim about a measured safety check. **Check 3 has a
  > substantive weakness of its own** (`[fix-wave: MAP-13]`): it refuses only the bare
  > `HEAD_INTENT_STATE_ACTIVE` name shape, so it cannot enforce "the enum ends at 8" the way the header
  > above describes; a positive assertion on the enum block, backed by a Go test, is the fix and it is
  > trunk work. Re-verified 2026-09-03 against
  > the `u4-arbiter` tip: checks 1, 2 and 4 all match and the hook exits 1.
  >
  > This does **not** upgrade the guard. It is still a name-matching speed bump against
  > accident, `--no-verify` still bypasses it, it still does nothing where `core.hooksPath`
  > was never set, and it still scans only the pushed tip tree. The class limit above is
  > exactly why §2.3.11.4 resolves to the build tag EXCLUSIVELY.
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
| A3 | Default build (no `w17_first_active` tag): an exported `CanEverBeActive()`-style predicate, **plus symbol-absence in the linked binary** (`go tool nm` or equivalent — meaningful only on the build-tag mechanism, §2.3.11.4) | predicate returns **false**, and no arbiter symbol is present in the default binary — *absent*, not merely branch-eliminated |
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

> **Owner amendment 2026-08-16 — branch-only implementation approved** (recorded at vision
> lock, `W17_PRODUCT_VISION.md` reality check "Head tracking (9)"; propagated here per that
> note's "owed when `w17-control-fw` is next touched"). The paragraph above stands as
> written history; its blanket "before the first line of arbiter code is committed" is
> superseded in exactly one respect: the U4 arbiter and its Groups A/B/C test matrix
> (§2.3.11.5) may now be **written on a `w17-mapper` feature branch that is never merged
> and never pushed** before this R1–R16 checklist passes, with **both FIRST_ACTIVE flags
> default-off** (§2.3.11.4) and **every shaping constant fail-closed** — no invented
> calibration values; absent a signed §2.3.12.8 record the arbiter refuses non-passthrough
> (the R12/D18 semantics). Nothing else moves: **merge, push, and activation remain gated
> on R1–R16 plus bench evidence**; the `w17-mapper` pre-push guard (§2.3.11.4) and the
> fork's push-disabled provenance (§2.3.12.9 item 1) stay in force; activation semantics
> are unchanged (two flags + R-review + bench evidence).

- [ ] **R1** Codex-owned FIRST_ACTIVE milestone checklist passed and its go/no-go table filled
      with evidence.
- [ ] **R2** All 7 blockers of `iphone_pan_tilt_firmware_readiness.md §8` accounted for.
      **Rewritten 2026-07-30 — the blanket "or explicitly deferred with recorded owner sign-off"
      is DELETED.** That clause was a waiver route around four items that carry no waiver of
      their own: six of the seven blockers are already independently mandated elsewhere, so
      deferring one under R2 could be read as overriding the R-item that requires it. R2 is now
      cross-references plus one promotion:

      | readiness §8 blocker | Discharged by |
      |---|---|
      | 1. Physical servo endpoint validation | **R7** (endpoints + deg↔count table) |
      | 2. Smoothing / rate-limiting ownership confirmed **and built** | **R12** (constants signed) **and R13** (matrix green — R12 covers the values, R13 proves the mechanism exists) |
      | 3. Stale-decay-to-center policy | design §2.3.11.2 item 6 + **R4** |
      | 4. Manual override | C1/C2 + design §2.3.11.2 item 7 |
      | 5. Windows log-only bridge validated | **R9** (real-device log-only bridge, U1/CB6) |
      | 6. Real iPhone axis/mount validation | **R8** (U5 / Codex Batch 5) |
      | 7. Bench-only servo sweep, wheels off | **R16** — promoted, see below; R2 was its only home |

      R2 therefore carries **no independent evidence obligation and no waiver**. It passes when
      the referenced items pass.
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
      **Scope widened 2026-09-03 (OD-17, §2.3.12.13(b)):** this enumeration is illustrative,
      not exhaustive — the record R12 signs is `pkg/headarbiter/calib.go`'s full schema
      (6 metadata + 14 per-axis + 10 policy + the bindings block, enumerated in full at
      §2.3.12.13(b)), so nothing can be signed by omission.
- [ ] **R13** The full §2.3.11.5 test matrix (Groups A/B/C) implemented and **green**, with the
      byte-identical-while-inactive proof (Group A) demonstrated on the default build, before
      the flag is flipped in any shared build.
- [ ] **R14** A written rollback: how to return to log-only (flag off ⇒ identity passthrough,
      already the default) and how Electron/mapper 5602 mutual exclusivity is preserved.
- [ ] **R15** **Input-device-loss disarm demonstrated by physically unplugging the gamepad**
      (added 2026-07-30), not simulated — from `ARMING`, from `ACTIVE`, and from `OVERRIDDEN`,
      with reconnect proven not to restore authority. Proves I10 and the §2.3.11.1
      input-provenance rule against the real SDL/OS device-removal path — the one path no unit
      test exercises, and the one the mapper-side hold-last fix (§2.3.11.1) does **not**
      establish. Group D rows D19–D22.
- [ ] **R16** **Bench-only servo sweep before any driving use** ⚠ **(name collision: a
      *different* R16 exists in `10_risk_register.md` — `main.cpp` orchestration + Wokwi sim not
      asserted in CI. The two R-series are independent. Risk-register R16 was partially advanced by
      the first Wokwi run on 2026-08-04 and its commit subject says "R16 closed"; that says
      NOTHING about this one, which remains NO-GO. Always qualify: "FIRST_ACTIVE R16".)**
      (promoted out of R2's blocker 7,
      2026-07-30, because R2 was its only home and R2 carried a blanket waiver clause). A
      scripted ch9/ch10 sweep — Wokwi Stage-2, then real servos on the bench, car elevated /
      wheels off the ground — exercising the full commanded range with **no stall, no rail
      brown-out, and correct direction**, with an observer present. **No waiver clause**: this
      is the last check before anything moves under power, and nothing else in R1–R15 covers
      it. Requires Phase B (powered), so it is gated behind R6.

**Precedence rule (added 2026-07-30).** A deferral or sign-off recorded under any one R-item
**cannot** waive an obligation that another R-item states independently. Where two items cover
the same ground, the stricter governs. This is the general fix; R2's blocker-1-vs-R7 collision
is simply where it surfaced.

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

#### 2.3.12 FIRST_ACTIVE owner decisions + U4 design addendum (2026-07-15, documentation only)

**Status: DECISION/DESIGN RECORD. No code was written or authorized.** This section records
the owner decisions taken 2026-07-15 that close the *decision-class* FIRST_ACTIVE blockers
(R3, R4 bench scope, R5, R11, R14, and the open §2.3.11.2 behaviors) and completes the U4
design addendum the §2.3.11.6 review consumes. The overall FIRST_ACTIVE verdict remains
**NO-GO / BLOCKED** (§2.3.12.11) — the remaining blockers are hardware-evidence class and
cannot be closed by documentation. Nothing here authorizes implementation, hardware power,
or active head control.

##### 2.3.12.1 Owner decision #2 — video-loss policy (RESOLVED; active behavior NOT implemented)

For the future active head-control path the owner selects **§4 option 3 (sender-side
suppression), composed with the existing mapper stale machinery**:

- On **iPhone-local decoder/video loss**, the iPhone **suppresses W3 head-intent
  transmission** (a future Codex-side behavior change, delivered via the two-stage handoff —
  never implemented from this side).
- The mapper consequently reaches its **stale timeout** and runs the **controlled stale
  decay** to commanded 992 (§1.3, §2.3.11.2 items 4/6). No new mapper mechanism is invented
  for video loss — suppression deliberately reuses the one already-specified stale path.
- **Video loss must never directly command a servo or CRSF value.** No layer may translate a
  video-health signal into an output; the only permitted consequence is packet silence →
  staleness → the standard decay.
- The **current W3 log-only operation stays independent of video state** — nothing changes in
  the log-only phase.
- An **operator-facing degraded/lost-video state is REQUIRED** before the active milestone:
  the operator must be able to see "video degraded/lost" distinctly from "head tracking
  stale". Surface and wording are implementation-slice work, gated with U4.

The active behavior itself remains unimplemented and gated; this decision resolves review
item **R3**.

##### 2.3.12.2 Owner decision #3 / U8 — radio-loss policy (RESOLVED FOR BENCH ONLY)

For the **bench-only FIRST_ACTIVE milestone only**: on radio/handset loss the firmware
**holds the last valid gimbal command** (today's shipped hold-last behavior stands,
unchanged). Scope limits, recorded verbatim as owner constraints:

- Applies **only** to the bench-only FIRST_ACTIVE milestone (wheels off, §2.3.12.3).
- **Must be re-reviewed before any vehicle driving** — the U8 concern (the hybrid mapping
  makes sustained off-center positions routine, raising the stakes of hold-last) is deferred,
  not dismissed. The driving-scope hold-vs-center decision **remains open**.
- Reconciled with the mapper layer exactly as §2.3.11.2 item 6 records: mapper intent-loss
  decays to commanded 992 while the radio is up; the firmware radio-loss layer is separate.

This resolves review item **R4** for the bench scope only.

> **Amended 2026-08-16 — implemented as decay-to-center; the driving re-review is still
> owed.** Vision decision 11 (`W17_PRODUCT_VISION.md`) set the vision-level end-state: on
> radio loss the gimbal **decays to center**. Now implemented in `w17-control-fw` branch
> `feat/gimbal-decay-center` (`lib/failsafe/GimbalDecay`, wired into the 50 Hz control
> tick; rate = NVS tunable `gimbal.decay`, default 2 s full-deflection-to-center; recovery
> slews back to the live stick command, no snap). The hold-last wording above stands as
> the record of the 2026-07-15 bench-scope resolution, and is superseded in code for all
> scopes. Two things do NOT move: (1) the **formal driving-readiness re-review this
> section requires remains owed** — implementing the intended end-state does not
> discharge the review (the U8 concern is now mitigated, not retired; the re-review says
> so formally or revises); (2) the mapper-side U4 design's decay to commanded 992 on
> intent-loss (§2.3.11.2 item 6) is a **separate, compatible mechanism** — the mapper
> decays while the radio is up, the firmware decays when the radio itself is lost.

##### 2.3.12.3 Owner decision #5 — driving policy (RESOLVED: FIRST_ACTIVE is bench-only)

FIRST_ACTIVE remains **bench-only**: wheels off the ground; **no vehicle driving**; operator
present at the controls; **emergency power removal available** within reach; conservative
physical limits; **first powered movement limited to ±5 mechanical degrees around the
measured center** (per-axis, from the §2.3.12.8 measurements).

The **first-driving milestone is separate** and additionally requires: another reviewed
safety gate; a **spotter**; completed bench evidence; and explicit owner approval of
**driving-mode authority and failure behavior** (including the §2.3.12.2 re-review). This
resolves review item **R5**; Codex Batch-9-style no-spotter driving is rejected.

##### 2.3.12.4 Invalid-packet policy (active path; owner decision)

Stricter than the log-only monitor's diagnostic classification (which preserves last-valid
for display, `monitor.go`):

- **One invalid packet immediately removes iPhone head-input eligibility.** Stale handling
  and the controlled decay to 992 begin at once — the arbiter does not wait for the 250 ms
  freshness gate to expire.
- **Repeated invalid packets latch a fault** (threshold/window is an R12-reviewed constant).
  A latched fault is never cleared by traffic alone.
- **Fault recovery requires all of:** operator **disarm**; a **valid-data recovery interval**
  (continuous valid traffic for a reviewed duration); and an **explicit recenter** before
  rearming.
- **Invalid input can never acquire or preserve head-control authority** — an invalid packet
  is never "last valid held", never freshness-extending, never center-defining.

##### 2.3.12.5 Manual-override policy (owner decision: GLOBAL takeover)

Refines §2.3.11.2 item 7:

- **Global takeover:** intentional deflection beyond the reviewed threshold on **either**
  manual pan or tilt axis gives manual control of **both** axes simultaneously. There is no
  per-axis split authority — one authority owns the pair at all times.
- Manual input wins **during active head control AND during stale decay** — a decay in
  progress yields to the stick immediately.
- **No auto-restore:** the stick returning to center does not restore head control. Override
  latches; restoration requires **explicit recenter and rearm** (§2.3.12.6 affordances).
- **Every authority transition remains rate- and acceleration-limited** (§2.3.11.2 item 8) —
  including override-engage: the output moves to the manual value through the limiter, never
  as a step.

##### 2.3.12.6 FIRST_ACTIVE controller affordances — **CONFLICT FOUND; Alternative C adopted (bench-only); live mapper-binding validation still required**

The proposed bench-only default was: hold **L1+R1** together 1 s and keep holding = deadman
arm; releasing either = immediate disarm; short-press **R3** = recenter — *subject to an
explicit controller-map conflict audit*. The audit was performed 2026-07-15 and **fails the
proposal**:

- **Exact conflict:** in every documented DualShock/Xbox/Generic layout of the ground
  station's SEAT-FIT presets (`w17-ground-station/shared/inputPresets.mjs:15-53`,
  `STANDARD_MAP`), **R1 (button 5) = gear up and L1 (button 4) = gear down** — vehicle
  transmission controls. A held L1+R1 deadman would continuously assert gear-down + gear-up
  during every second of head-tracked operation. The GS preset is the display mirror; the
  **binding authority is the mapper's node-graph config**, which must be audited on the bench
  before any affordance ships — but the documented layout conflict is already disqualifying.
- **Secondary caution (R3):** R3 is the **right-stick click** — the physical stick whose axes
  (2/3) are the manual pan/tilt input. Pressing R3 mechanically perturbs the very axes the
  manual-takeover threshold watches, so a recenter press could trip (or mask) an override
  transition. R3 (and L3, which perturbs the steering axis 0) should not be used for any
  head-control affordance.

Per the owner's standing rule, the bindings were **not silently remapped**. Three candidates
were considered; all use only controls unbound in every documented preset (SHARE = button 8,
OPTIONS = button 9, D-pad DOWN = button 13) and none touch the gear (L1/R1) or stick-click
(L3/R3) controls above.

- **Alternative A (REJECTED):** hold SHARE + OPTIONS together 1 s and keep holding = deadman;
  short-press D-pad DOWN = recenter.
- **Alternative B (REJECTED):** hold OPTIONS + D-pad UP together 1 s and keep holding =
  deadman; short-press ✕ / A = recenter.
- **Reason A and B were rejected:** both require a *continuously held two-thumb chord* for the
  whole active window. On a standard pad SHARE/OPTIONS/D-pad are all left-hand controls, but a
  held two-button chord ties up the hand and, more importantly, competes with the operator's
  need to keep the **right thumb free on the right stick** — the manual pan/tilt takeover the
  entire safety model leans on (§2.3.12.5, I6). A deadman that makes the safety escape hatch
  awkward is the wrong deadman.

**Alternative C — ADOPTED for FIRST_ACTIVE bench testing (owner, 2026-07-15):**

- **short-press SHARE (button 8) = recenter** (defines the current head pose as neutral,
  §2.3.12.7);
- **hold D-pad DOWN (button 13) + OPTIONS (button 9) together for one continuous second = the
  deliberate arm gesture** (a two-control intent confirmation, so a stray press cannot arm);
- **after arming, OPTIONS may be released**;
- **D-pad DOWN must remain held as the continuous deadman** — it is the single held control for
  the rest of the active window;
- **releasing D-pad DOWN immediately disarms** (→ `DECAYING` then `MANUAL`, §2.3.12.10);
- the **right thumb stays free on the right stick** for manual pan/tilt takeover throughout;
- **any manual deflection above the reviewed threshold still triggers global manual takeover**
  (§2.3.12.5) regardless of deadman state — Alternative C changes the arm gesture, never the
  override authority;
- **before any use, the live mapper node graph must prove SHARE, OPTIONS, and D-pad DOWN are
  unbound** (and not intercepted) — the GS SEAT-FIT preset is only the display mirror; the
  binding authority is the mapper node-graph config;
- **if any chosen control is bound or intercepted, active testing remains blocked** — no
  silent remap, no proceeding.

Scope limits, recorded as owner constraints:

- **Bench-only.** This mapping applies **only** to FIRST_ACTIVE bench testing.
- **Not approved for driving.** Because D-pad DOWN occupies the **left thumb**, it is unsuitable
  as a driving-time deadman; the later driving-readiness milestone (§2.3.12.3 / O) **must choose
  another deadman/authority UX** and is not bound by this bench choice.

This closes **only the owner-choice portion** of decision #6. The bench session must still
perform the **live mapper node-graph binding validation** above before the first active use;
until that validation passes, active testing stays blocked (and R12's "arm affordance signed
off" stays open — the affordance is *chosen*, not yet *validated on the live binding*).

> **Amendment 2026-09-03 — the button NUMBERS above are the DirectInput reading; the physical
> controls are unchanged.** `w17-mapper` `4e27b0e` (2026-08-17, review blocker F1) corrected the
> shipped W17 profile to the **SDL HIDAPI GameController** layout, which is what the PS4 driver
> reports by default on Windows. In that layout **SHARE = button 4, OPTIONS = button 6, and the
> D-pad is a HAT (hat 0)** — "D-pad DOWN" is a hat direction, **not "button 13"**. The legacy
> DirectInput fallback moves both axes *and* buttons, and is where 8/9/13 came from; both layouts
> are documented side by side in `w17-mapper` `configs/README.md`.
>
> **Nothing about the adopted affordance changes.** Alternative C is a choice of *physical*
> controls — SHARE, OPTIONS, D-pad DOWN — the reservation in the shipped profile is by physical
> button, and the arbiter's bindings are **external, validated and fail-closed with no shipped
> default**, so no numeric value is owed anywhere in code. Only the numbering attributed to those
> controls in the prose above was wrong.
>
> What the correction *does* change is the live-binding validation this section already demands:
> it must now also establish **which numbering the bench pad actually reports** and **whether its
> D-pad enumerates as a hat or as buttons**, by pressing each bound control on the machine that
> will run it. A binding record written against the wrong assumption fails closed rather than
> arming — the failure is safe, but it is a failure. R12's "arm affordance signed off" stays
> open, unchanged, and FIRST_ACTIVE stays NO-GO / BLOCKED.

##### 2.3.12.7 Hybrid controller mapping (owner decision: preserved/ratified)

- Head **yaw → gimbal pan**; head **pitch → gimbal tilt**; head **roll is ignored**.
- **Near neutral: position mapping** (head angle → commanded offset). **Near the comfortable
  head-turn boundary: transition smoothly into rate mapping** (sustained head deflection →
  commanded velocity), so the operator can look further than the neck turns.
- **No command discontinuity at the transition** — the position→rate blend is continuous in
  value and bounded in derivative (test D6).
- Output remains **clamped** (measured per-axis safe min/max), **rate-limited**, and
  **acceleration-limited** at all times — the blend never bypasses the §2.3.11.2 limiters.
- **Recenter defines the current head pose as neutral** (re-seeds `virtualCameraCenter` per
  §1.3 — from the authoritative final commanded value, never a claimed measured angle).

##### 2.3.12.8 Numeric shaping constants — derivation policy (NO production values signed)

**No production constant is invented or signed before hardware measurement.** Required
derivation record (per axis, pan and tilt measured independently), to be captured at the
U3/CB9 bench session and stored in `project-review/`:

center command · safe minimum and maximum commands · mechanical direction/sign · counts per
mechanical degree · stationary real-iPhone jitter (deg) · real controller centered noise
(counts) · hardware identity, date, operator, and evidence source for every row.

Initial FIRST_ACTIVE derivation policy (formulas, not values):

- **Head deadband** derives from measured stationary jitter; **floor 1°**, **cap 3°** —
  exceeding 3° requires another review, not a bigger number.
- **Initial max rate = 10°/s**, converted through the measured counts/degree.
- **Initial max acceleration = 20°/s²**, converted through the measured counts/degree.
- **Manual-takeover threshold = max(measured controller-noise margin, ≈10 % of the usable
  CRSF half-range)** on the relevant axis.
- **First powered travel limited to ±5 mechanical degrees around the measured center.**
- **Missing calibration ⇒ active control unavailable** (fail closed — the arbiter refuses to
  leave passthrough without a complete signed calibration record). **No silent production
  fallback values, ever.**

U4 remains blocked until the exact per-axis values are measured, recorded, signed, and
reviewed (**R7 + R12**).

##### 2.3.12.9 Related owner decisions recorded at the same sitting

1. **Fork license + provenance (R11 RESOLVED).** The owned mapper fork is licensed
   **GPL-3.0-or-later** (of upstream's GPL-3.0 OR Fair Source 0.9 dual offer), **provided the
   W17 mapper remains open source**. Record: upstream `github.com/kaack/elrs-joystick-control`;
   fork base = upstream commit `2b8031a`; fork created **2026-07-15** as local repo
   `w17-mapper`, branch `w17-headtrack`; upstream copyright and license files preserved
   unmodified in the fork; provenance = local clone of the read-only `_vendor/` reference;
   modification policy = production changes only in `w17-mapper` (never `_vendor/`), reviewed
   slices, one repo per session; **no remote publication or push without explicit owner
   approval**. **If proprietary or source-closed distribution is ever intended, STOP and obtain
   legal review** — GPL-3.0 must not be treated as sufficient for that case.

   **UPDATED 2026-07-25 — "push remains disabled" is no longer true; do not rely on it.**
   The fork now has a remote: `origin` = `github.com/beforethenexttolast/w17-mapper`, created
   2026-07-25T04:11Z, **PUBLIC**, with `origin/w17-headtrack` carrying the current work.
   (`upstream`'s push URL remains disabled.) The accidental "no remote, so push is impossible"
   protection is **gone**, and it was never a control in the first place — it was an accident of
   setup. What replaced it, deliberately:
   - a tracked **`.githooks/pre-push`** (enable per clone with
     `git config core.hooksPath .githooks`) that refuses a `w17_first_active` build tag, a
     `FIRST_ACTIVE` identifier in Go or proto source, or an active head-intent enum — verified
     to pass a clean HEAD and to bite on all three injections; **plus**
   - the **push-review rule in `FORK-NOTICE.md`**.

   The hook is the *accident* guard; the rule is the *control*. What is published distributes
   **no control path**: proto still ends at `ACTIVE_LOG_ONLY = 8`, there is no `FIRST_ACTIVE`
   in tracked Go or proto source, and upstream licence files are unmodified (all re-verified
   read-only 2026-07-27).
2. **Mapper serial dependency — SHIPPED 2026-07-25 as `v1.5.0 → v1.6.0`.**

   **Superseded approval (2026-07-15, preserved):** the owner approved
   `go.bug.st/serial` v1.5.0 → **v1.7.1** as a future isolated slice (the §2.3.9 go1.26 cgo
   incompatibility). Constraints: `w17-mapper` only; exact `go.mod`+`go.sum` diff; no unrelated
   dependency churn; `gofmt` where applicable; `go test ./...` + `go build ./...` green; CRSF
   byte-invariance tests re-run; **no arbiter or control-path changes ride along**; no commit
   until owner review. **Not to be mixed** with any ground-station or U4 work.

   **What shipped instead: v1.6.0** (`w17-mapper` `f0a18f3`, 2 files / +3 −1). **Accepted.**
   The reason is not "smaller delta" — it is that the approved version would have changed the
   language the module compiles under:

   - **v1.7.1 would raise the module's language version.** It drags `golang.org/x/sys`
     v0.8.0 → v0.43.0 and a `go` directive bump **1.20 → 1.25.0** in **both** `go.mod` and
     `go.work` — **including go1.22 per-iteration loop-variable semantics**. That is a
     behavioural change to every range loop in the mapper, arriving as a side effect of a
     serial-driver bump, on a module whose `pkg/serial` sits in the CRSF send path. The
     approval did not cover that collateral. By contrast v1.6.0's own `go.mod` is
     **byte-identical to v1.5.0's**; `go list -m all` differs by **exactly one line out of
     ~400 modules**; `go.work` / `go.work.sum` are byte-identical to `59d1739`.
     *(Verified in-tree 2026-07-27: `go.mod` and `go.work` both still read `go 1.20`,
     `golang.org/x/sys` still v0.8.0.)*
   - **v1.6.0's delta cannot touch CRSF timing.** `Write`/`Read` are **byte-identical**
     v1.5.0 → v1.6.0 in both `serial_unix.go` and `serial_windows.go`. The delta is confined
     to enumeration, `Open` error wrapping/cleanup, an added `Drain()` **that is never
     called**, and cgo type wrappers.
   - **v1.7.1 reachability analysis — preserved deliberately, because it makes a future bump
     cheap.** v1.7.1 carries two real read-path deltas: the CH340 `0xFFFFFFFE` → `0x7FFFFFFE`
     timeout constant, and a `Read` that loops forever when `hasTimeout == false`. Both are
     **provably unreachable** in this fork because `supervisor.go:52` always passes a positive
     `refreshRate*4`. v1.6.0 predates both. So if v1.7.1 is ever needed, the read-path risk is
     already cleared and only the language-version question remains.

   **Evidence:** `go build ./...` green (was 11 cgo errors / 6 packages failed);
   `go test -count=1 ./...` green; `-race` on `./pkg/headintent/` + `./pkg/server/` green
   (39 tests / 29 subtests, 0 races); `go mod verify` all modules OK; proto untouched (still
   ends at `ACTIVE_LOG_ONLY = 8`, no active enum — re-verified 2026-07-27); and
   `crsf.PackChannels` **byte-identical** — 12 frames / 312 bytes, all four dumps
   (off / on-valid / on-stale / on-invalid) sharing one SHA with subscribers connected, slow,
   and disconnected, and identical to the dumps generated at v1.5.0.

   **Residual unknown:** real Windows enumeration of the ELRS TX. Unverifiable without
   hardware on a macOS host — tracked with the other Windows-hardware items in
   `../../CURRENT_STATUS.md` (Pending validations).

   **`go vet ./...` is NOT green in the fork — this is not a regression.** The bump did not
   break vet, it **revealed** it: before v1.6.0, `cmd` never compiled, so vet never reached it.
   What surfaces is one pre-existing **upstream** diagnostic
   (`cmd/elrs-joystick-control/main.go:130`, unbuffered `os.Signal` channel, upstream
   `db01a677`, 2023) plus two gofmt-dirty upstream files (`pkg/client/grpc_client.go` and one
   other). All left as-is per "no unrelated dependency churn", and noted in the commit message
   — fixing upstream files would add rebase friction to a fork we intend to keep tracking.
   **Consequence to carry forward: if mapper CI is ever added, scope vet to the owned packages**
   (`go vet ./pkg/headintent/ ./pkg/server/`) rather than `./...`, or it will fail on upstream
   code we deliberately did not touch.
3. **Diagnostics boundary (ratified as a hard invariant).** Mapper→Electron diagnostics stay
   **read-only gRPC** (§2.3.10). The diagnostics interface must **never**: arm; disarm;
   recenter; select authority; change controller configuration; alter the node graph; alter
   mixer values; alter CRSF output. Bounded subscriber count (4-stream cap) and bounded
   per-subscriber buffers stand; slow subscribers must never block mixer evaluation or serial
   output; subscriber failure must never affect command output. Hardening the external
   `[::]:10000` bind remains a **separate reviewed decision** (§2.3.10 bind-policy fact).
4. **iPhone R10 status (recorded WITHOUT modifying or mirroring iPhone files).** The
   canonical iPhone repo currently carries **automated evidence for the 250 ms send-time
   sample-age gate**: `MotionState.swift` pins `maximumMotionSampleAge = 0.250` and its tests
   prove **249 ms eligible / 250 ms eligible / 251 ms stale**, and that **cached active state
   cannot bypass the send-time check** (verified read-only 2026-07-15; the work is uncommitted
   in the `iPhone_rc` working tree). Treated as **PASS (automated) only**. Still pending: real
   iPhone lifecycle testing; real device motion-axis evidence; mount-orientation evidence;
   the canonical iPhone commit approval; and the bridge-contract mirror **only after** that
   canonical commit hash is received. Uncommitted iPhone contract text is **not** mirrored.

##### 2.3.12.10 U4 detailed design addendum — states, behaviors, and the required test per behavior

Completes §2.3.11.2 into the reviewable behavior spec. All of it describes the **future
gated** arbiter; none of it exists in code.

**Arbiter states** (distinct from the log-only diagnostic states, which are unchanged and
gain no active value until the gated slice):

| State | Meaning | Output on ch9/ch10 |
|---|---|---|
| `IDENTITY` | Compile flag absent OR runtime flag off — the only reachable state in default builds (I3) | bit-for-bit passthrough of the eval array (I1) |
| `MANUAL` | Flags on, not armed (incl. never-armed, after-disarm, no-data-ever) | passthrough (manual stick) |
| `ARMING` | Deadman affordance held, 1 s accumulation running | passthrough until arm completes |
| `ACTIVE` | armed ∧ enabled ∧ centered ∧ fresh(≤250 ms) ∧ eligibility intact ∧ recentered | shaped head-derived command (hybrid §2.3.12.7, shaped §2.3.11.2) |
| `DECAYING` | any ACTIVE precondition dropped (stale, invalid, disable, un-center, disarm, video-suppression silence) | rate/accel-limited ramp to 992; `virtualCameraCenter` discarded |
| `OVERRIDDEN` | manual deflection past threshold (from ACTIVE **or** DECAYING); latched | manual stick (reached through the limiter) |
| `FAULT` | repeated-invalid latch or receiver fault | ramp to 992, then passthrough; head ineligible |

**Transition guards:**

- `MANUAL→ARMING`: approved deadman affordance held (§2.3.12.6) — never automatic (I6).
- `ARMING→ACTIVE`: 1 s continuous hold ∧ explicit recenter performed ∧ all §2.3.11.2 item-5
  preconditions true. Any interruption returns to `MANUAL`.
- `ACTIVE→DECAYING`: any precondition lost, or **one** invalid packet (§2.3.12.4).
- `ACTIVE/DECAYING→OVERRIDDEN`: manual deflection > threshold on either axis (global takeover,
  §2.3.12.5).
- `DECAYING→ACTIVE`: **never automatic.** Once decay begins, resumption requires an explicit
  recenter (conservative FIRST_ACTIVE default; the deadman may remain held — a full disarm is
  required only from FAULT). Data merely returning fresh does not re-acquire authority.
- `OVERRIDDEN→ACTIVE`: explicit recenter + rearm only.
- `ANY→FAULT`: repeated invalid packets (reviewed threshold/window) or receiver `fault`.
- `FAULT→MANUAL`: disarm ∧ valid-data recovery interval ∧ explicit recenter (§2.3.12.4);
  rearm then proceeds through `ARMING` normally.
- Release of either deadman control in any state ⇒ immediate disarm ⇒ `DECAYING` (if head
  authority was engaged) then `MANUAL`.

**Behavior spec + the required test for each** (Groups A/B/C = §2.3.11.5; new Group D rows
below are additional gated-implementation tests):

| Behavior | Spec | Required test |
|---|---|---|
| Authority ownership | mapper-only, single post-graph choke point (§2.3.11.1); one authority owns both axes at all times | A5, C6 |
| Arm/disarm | deadman per §2.3.12.6 (post-approval); release-either = disarm; no auto-arm | C3, D15 (deadman release from every state) |
| Recenter | defines current pose as neutral; re-seeds from final commanded value | C4, D7 |
| Manual takeover | global, both axes, wins in ACTIVE and DECAYING, latched | C1, C2, D4, D5 |
| Head-input eligibility | all item-5 preconditions, every tick | B1 |
| Invalid packet | one ⇒ eligibility removed + decay; repeated ⇒ latched FAULT | D1, D2 |
| Fault recovery | disarm + recovery interval + recenter, in that order | D3 |
| Stale timeout | active gate 250 ms receive-time (249/250 fresh, 251 stale), distinct from 300 ms log-only | B5 |
| Stale decay | rate-limited ramp to exactly 992; `virtualCameraCenter` discarded; no re-acquisition without recenter | B6, D14 |
| Disconnect/reconnect (**iPhone / UDP 5602 stream**) | silence ⇒ decay; reconnect alone never restores ACTIVE | D14 |
| Disconnect (**gamepad / arm-input device**) | device loss ⇒ **disarm**, identical to deadman release (I10); arm/deadman/override never sourced from the channel array, which carries no validity (§2.3.11.1 input-provenance rule); reconnect alone never restores authority | D19–D22, R15 |
| Video loss | sender suppression ⇒ ordinary stale path; no direct video→output coupling | D16 (suppressed stream indistinguishable from stale at the arbiter) |
| Radio loss | firmware layer, hold-last, bench scope only — out of arbiter scope. *(2026-08-16: firmware layer is now decay-to-center, vision decision 11 — §2.3.12.2 amendment; still out of arbiter scope)* | firmware bench evidence (Phase B), not a U4 unit test; native decay tests exist in `w17-control-fw` (`test_failsafe`) |
| Deadband | degrees, via measured deg↔count; inside band ⇒ exactly 992 offset | B2 |
| Smoothing | single-pole low-pass on head angles before deadband; reviewed time constant | D17 (step response matches constant; no overshoot) |
| Rate limiting | ≤ maxRate counts/s on every transition path | B3, C5 |
| Acceleration limiting | ≤ maxAccel counts/s² | B4 |
| Fractional accumulation | sub-count remainders accumulate; no truncation bias at low rates | D8 |
| Endpoint clamping | never outside measured per-axis safe min/max, even transiently | D9 |
| Monotonic time | all freshness/ramp math on the monotonic clock; wall-clock jumps have no effect | D10 |
| No-data identity | never-received ⇒ ch9/ch10 passthrough, byte-identical | A1, D11 |
| Copy-on-write evaluation | arbiter copies the eval array; never mutates shared state in place | D12, C6 |
| Diagnostic-state semantics | arbiter state exported read-only via the existing diagnostics stream; no active enum value exists before the gated slice adds it under review | D13 |
| Hybrid blend continuity | position→rate transition continuous, no command discontinuity | D6 |
| Missing calibration | no signed calibration record ⇒ arbiter refuses non-passthrough | D18 (fail-closed) |
| **Arm-input device loss** (I10, added 2026-07-30) | gamepad disappearance ⇒ disarm, identical to release; never inferred from the channel array | **D19** unplug in `ARMING` ⇒ `MANUAL`; **D20** unplug in `ACTIVE` ⇒ `DECAYING`→`MANUAL`, ramped to 992; **D21** unplug in `OVERRIDDEN` ⇒ stays non-active, no head re-acquisition; **D22** reconnect alone (no recenter, no rearm) ⇒ authority **not** restored. All four physically unplugged per R15, plus a unit-level seam test proving `nan`/device-absent maps to disarm and *not* to a retained previous value |

##### 2.3.12.11 FIRST_ACTIVE go/no-go table (honest, 2026-07-15) — **NO-GO / BLOCKED**

| Item | Status | Evidence / blocker |
|---|---|---|
| R1 Codex milestone checklist | **NO-GO** | not run; milestone doc has uncommitted edits in `iPhone_rc` |
| R2 readiness §8 blockers accounted for | **NO-GO** | rewritten 2026-07-30 as cross-references with **no independent waiver**; passes only when R7/R12/R13/R4/R8/R9/R16 do, and most of those are hardware-evidence class |
| R3 video-loss decision | **PASS (decision)** | §2.3.12.1, 2026-07-15 |
| R4 hold-vs-center recorded | **PASS (bench scope only)** | §2.3.12.2; driving scope re-review still required |
| R5 driving protocol/spotter | **PASS (decision)** | §2.3.12.3; bench-only, separate driving milestone |
| R6 A2 + Phase B | **NO-GO** | A2 unexecuted; Phase B blocked (`CURRENT_STATUS.md`) |
| R7 endpoints + deg↔count table | **NO-GO** | hardware (U3/CB9; gated by R6) |
| R8 iPhone axis/mount validation | **NO-GO** | hardware (U5, Codex Batch 5) |
| R9 real-device log-only bridge | **NO-GO** | hardware/network (U1/CB6) |
| R10 sender 250 ms suppression | **PARTIAL — PASS (automated)** | §2.3.12.9 item 4; real-device lifecycle/axes/mount + canonical commit + mirror pending |
| R11 fork path/name/license | **PASS** | §2.3.12.9 item 1 |
| R12 shaping constants signed | **NO-GO** | derivation policy recorded (§2.3.12.8); values need R7 measurement. **Amended 2026-09-03 (OD-17, §2.3.12.13(b)):** scope widened to `calib.go`'s full schema — the checklist's five named items are illustrative, not exhaustive. Verdict unchanged. |
| R13 test matrix green | **NO-GO** | **Amended 2026-09-03 — the evidence line was stale; the verdict is not.** "No U4 code exists" stopped being true on 2026-08-16: the gated U4 arbiter exists on `w17-mapper` branch **`u4-arbiter`** (tip `8007603`, 2026-09-03), under the owner's 2026-08-16 **branch-only** approval — **never merged, never pushed**, and `.githooks/pre-push` refuses that tree by construction (re-verified 2026-09-03 on the tip: checks 1/2/4 all match, hook exits 1). The proto still ends at `HEAD_INTENT_STATE_ACTIVE_LOG_ONLY = 8` — verified by **reading** `pkg/proto/server.proto:519-527`, and because the branch changes no `.proto` file at all (I9), **not** inferred from check 3's zero hits: check 3 is a negative regex on one name shape and a differently named active value would pass it (`[fix-wave: MAP-13]`). On that branch the §2.3.11.5 matrix is green in **both build modes** on go1.26.5 — `go build`, `go vet ./pkg/...`, the full `./...` suite and `-race` over the full `./...`, default and `-tags w17_first_active` — and the ten identity dumps still share **one SHA-256** after the 2026-09-02 rebase onto `w17-headtrack` `9cb501e`. R13 stays **NO-GO** anyway, for two reasons that automated green cannot touch: (a) the branch has had **no adversarial desk R-review by anyone other than its author**, which §3 roadmap step **L** requires (a fresh adversarial §2.3.11.6 R1–R16 re-check), and (b) the matrix rows that need hardware (R15's physical unplug, FIRST_ACTIVE R16's servo sweep) are not runnable at a desk at all. Review packet: `w17-mapper` `docs/u4-branch-README.md` + `docs/u4-evidence/` |
| R14 written rollback | **PASS** | §2.3.12.12 |
| R15 device-loss disarm (I10) | **NO-GO** | added 2026-07-30; no U4 code exists to test. **Evidence line corrected 2026-08-04:** it previously read "the §2.3.11.1 hold-last defect it guards against is confirmed present in the fork's channel assembler (`output_tx.go:43`)" — both the claim and the line reference are stale. The assembler defect was fixed at `w17-mapper` `2dc7c5a`/`e452d55`/`c60843e` (§2.3.11.1). R15 is unaffected: it demonstrates **device-loss disarm** against the real SDL/OS removal path, which no unit test covers and which the fix was never claimed to establish. Needs a physical unplug demonstration, so it is hardware-*procedure* class, not bench-power class — it does **not** require Phase B. **Amended 2026-09-03 (OD-18, §2.3.12.13(c)):** transport ruled — gated build reaches the Windows VM as a `git bundle`/archive copied in by hand, never a push; hook stays intact; bundle deleted after the session; no serial port, no TX. Verdict unchanged: NO-GO until run. |
| R16 bench-only servo sweep | **NO-GO** | promoted out of R2 blocker 7, 2026-07-30; no waiver clause; requires Phase B, so gated behind R6 |
| **Overall** | **NO-GO / BLOCKED** | hardware-evidence items R1/R2/R6–R9, R12, R13, R15, R16 open |

Missing hardware evidence is **not** converted into PASS anywhere in this table.

##### 2.3.12.12 Rollback to log-only (R14)

- **Flag off ⇒ identity passthrough is the default and the rollback.** The runtime flag off
  (or any build without the compile tag) makes the arbiter a pure identity stage (I1/I3);
  reverting to log-only requires no code removal — it is the resting state.
- The receiver rollback below U4 is the existing `-headtrack-ingest` default-off flag
  (§2.3.9): ingest off ⇒ no socket, byte-identical output.
- **5602 mutual exclusivity is preserved in both directions** (topology (a), §2.3.7): mapper
  ingest on ⇒ Electron does not bind 5602 (`W17_MAPPER_HEADINTENT=1` topology, proven live in
  the slice-3C evidence); mapper ingest off ⇒ Electron's W3 log-only receiver may bind again.
  Rollback never leaves two binders or zero owners by construction — the modes are driven by
  one switch on each side and were validated mutually exclusive in
  `w17-ground-station/docs/2026-07-15_cb8_slice3c_integration_evidence.md`.

##### 2.3.12.13 R-review ratifications (owner, 2026-09-03) — OD-17 / OD-18

Owner rulings on the desk R-review of `u4-arbiter` (review packet: `w17-mapper`
`docs/u4-branch-README.md` + `docs/u4-evidence/`), answering items 2 and 8 of that review's
own "exactly what stands between `u4-arbiter` and an R-review pass" list. **This subsection
changes no gate: FIRST_ACTIVE stays NO-GO / BLOCKED (§2.3.12.11), and R12/R13/R15 stay
NO-GO.**

**(a) `dtClampMs = 50` ms — RATIFIED as a policy constant (D10 spirit).** The branch's
original comment cited "(plan §2.3.12.10 row D10)" for this value. D10 (`:1354`,
"Monotonic time | all freshness/ramp math on the monotonic clock; wall-clock jumps have no
effect") governs the **clock**, not a per-tick integration clamp — no plan text anywhere
ratifies 50 ms or any other clamp value, and the supporting "~2 tick periods at the slowest
standard CRSF refresh rate" rationale was unsourced against a refresh rate that is in fact
**dynamic**, reset from telemetry at runtime (`pkg/link/send.go:291-292`,
`ticker.Reset(nextRefreshRate)`). The branch has already corrected the citation
(`u4-arbiter` `pkg/headarbiter/gate.go:35-53`, tip `e4f6ae8`): it now says the value is the
implementer's unratified choice, not plan-derived, tagged `[bench-TBD / owner ratification
pending]`. **The owner now ratifies the value itself, 2026-09-03, as a policy constant in the
D10 spirit** — the clock stays monotonic (D10's actual requirement, independently satisfied:
`send.go:299`'s `time.Now()` is a monotonic reading) and the clamp exists only to bound what a
scheduling gap or clock anomaly can integrate into motion. **The clamp only ever reduces
commanded motion — smaller is always at least as safe, never less** — so ratifying an
implementer-chosen value here carries no motion-increasing risk. Value unchanged at **50 ms**.
`w17-mapper` records this ratification at `gate.go`'s `dtClampMs` comment, in
`docs/u4-branch-README.md` deviation 7, and in
`docs/u4-evidence/calibration_record_TEMPLATE.md:153-159`.

**(b) R12 scope WIDENED to the full `calib.go` schema.** R12's checklist text (§2.3.11.6,
`:908-909`) enumerates five things: "deadband width, `maxRate`, `maxAccel`, override
threshold, arm affordance". The signed record the branch actually requires before the
arbiter will leave passthrough is **`pkg/headarbiter/calib.go`'s complete schema**
(`u4-arbiter` tip `e4f6ae8`) — every field below is fail-closed: absence alone, not just an
out-of-policy value, refuses the record. **R12's signature now covers all of them, not only
the five named in the checklist text, so nothing can be signed by omission.**

Enumerated from `calib.go` (field — where the record is refused without it):

*Metadata, 6 fields required non-empty (`calib.go:98-107` schema, `:189-212` validation):*
`schema_version` (present and exactly `1`, `:98`, `:189-192`) · `hardware_id` (`:99`, `:201`,
`:208-211`) · `measured_date` (`:100`, `:202`, `:208-211`) · `operator` (`:101`, `:203`,
`:208-211`) · `evidence_source` (`:102`, `:204`, `:208-211`) · `signed_off_by` (`:103`,
`:205`, `:208-211`).

*`pan` and `tilt` axis blocks, 7 fields each = 14 (`calib.go:110-118` schema; block itself
required at `:104-105`/`:215`/`:218`/`:330-332`; each field required at `:338-354`):*
`center_count` · `safe_min_count` · `safe_max_count` · `sign` · `counts_per_degree` ·
`stationary_jitter_deg` · `controller_noise_counts` — once per axis (`pan.*` and `tilt.*`).

*`policy` block, 10 fields (`calib.go:120-131` schema; block itself required at `:222-224`):*
`deadband_deg` (`:121`, `:237`) — named in R12's text · `max_rate_deg_s` (`:122`, `:246`) —
named (as `maxRate`) · `max_accel_deg_s2` (`:123`, `:253`) — named (as `maxAccel`) ·
`takeover_threshold_counts` (`:124`, `:260`) — named (override threshold) ·
**`lowpass_tau_ms`** (`:125`, `:273`) — **not named in R12's text**; §2.3.12.10's "Smoothing"
row (`:1349`) calls the time constant "reviewed" but R12's own enumeration omits it ·
**`blend_start_deg`** (`:126`, `:280`) — **not named in R12's text or anywhere upstream**;
§2.3.12.7 (`:1153-1163`) defines the position→rate blend qualitatively only and names no
field for its geometry (deviation 2) · **`blend_full_deg`** (`:127`, `:283`) — same gap as
`blend_start_deg` · **`invalid_fault_threshold`** (`:128`, `:290-293`) — **not named**;
§2.3.12.4 (`:1041`) calls the threshold/window "an R12-reviewed constant" but R12's
enumeration omits it · **`invalid_fault_window_ms`** (`:129`, `:295-298`) — same gap as the
threshold · **`fault_recovery_valid_ms`** (`:130`, `:300-303`) — **not named anywhere
upstream**.

*`bindings` block, 7 top-level fields + sub-fields (`calib.go:139-147` schema; block itself
required at `:415`/`:417-418`):* `device_id` (`:140`, `:420-422`) · `target_port` (`:141`,
`:424-426`) · `deadman`, `arm_confirm`, `recenter` — three binding blocks (`:142-144`,
`:430`/`:433`/`:436`), each requiring its own `kind` (`:387-389`), `index` (`:391-394`), and
`hat_mask` (required iff `kind=="hat"`, forbidden iff `kind=="button"`, `:395-411`); the three
bindings must also be three physically distinct controls (`:445-447`) · `override_pan_axis`
(`:145`, `:449-451`) and `override_tilt_axis` (`:146`, `:453-455`) — the two must differ
(`:457-459`).

**Total: 6 metadata + 14 per-axis (7 × 2) + 10 policy + 7-plus bindings fields, all
fail-closed.** Of the 10 policy fields, only 4 were named in R12's original checklist text;
`lowpass_tau_ms`, `blend_start_deg`, `blend_full_deg`, `invalid_fault_threshold`,
`invalid_fault_window_ms`, and `fault_recovery_valid_ms` were not. **R12 is amended: its
checklist enumeration (§2.3.11.6) is illustrative, not exhaustive — the record R12 signs is
`calib.go`'s schema in full, at whatever commit is under review.** R12 itself stays **NO-GO**
(§2.3.12.11): this widens what a future signature must cover; it does not supply one.

**(c) R15 transport — RATIFIED (OD-18).** The gated `u4-arbiter` build reaches the Windows VM
as a **`git bundle` / archive copied in by hand — never a `git push`**. `.githooks/pre-push`
stays enabled and intact on every clone that can reach a remote; the bundle/archive transport
never touches a remote at all, so the hook is never in that loop and is not weakened by this
ruling. The bundle/archive is **deleted after the session** — it does not persist as a second
copy of gated code outside the two worktrees the branch already lives in. R15 itself runs
with **no serial port opened and no CRSF TX**: it is the arbiter's device-loss disarm demo
(unplug the gamepad from `ARMING`/`ACTIVE`/`OVERRIDDEN` per D19–D22, reconnect proven not to
restore authority), observed through the existing read-only diagnostics stream, not through
powered output. This discharges the transport question the desk review raised ("nobody has
asked this, and it silently blocks the one open R item that needs neither A2 nor Phase B") —
see the amended R15 row, §2.3.12.11. **R15 stays NO-GO until the procedure is actually run.**

## 3. Ordered unlock sequence

Blocker numbers refer to `iphone_pan_tilt_firmware_readiness.md §8`.

| Step | Work | Blocker | Owner | Gate |
|---|---|---|---|---|
| U1 | Validate the existing log-only Windows bridge end-to-end with a **real iPhone** (fake-sender validation exists; real-device pending) | 5 | Claude (GS) + real device | non-isolated bench network (`CURRENT_STATUS.md` pending validations) |
| U2 | Read-only investigation of elrs-joystick-control ingest capability; then **owner decision** on §2.3 (a)/(b)/(c) and fork ownership | — (prereq for 2,3,4) | Claude investigates; owner decides | none (read-only) |
| U3 | Bench-measure real gimbal mechanical endpoints on the assembled mount → per-axis `gimbalConfig` `ServoConfig` values; record the **deg ↔ CRSF-count conversion table** at the same bench session (the Codex milestone expresses limits in degrees; the wire carries counts; firmware owns µs — the conversion is bench evidence and lives here in `project-review/`) | 1 | Claude (fw) + bench | **HARD GATE: A2 closed + Phase B approved. This document does not authorize powered-bench work.** Requires `CAMERA_GIMBAL_PLACEMENT.md` mount decision + printed mount |
| U4 | Implement the mapper in the chosen host (per U2): deadband, low-pass, slew limit; stale-decay-to-992 per §1.1/§1.3; right-stick-wins arbitration with **no auto-restore** (re-arm = explicit operator action); rate limiting applied across **every** authority transition (arm, disarm, override, stale) so no transition can step the output. **Design recorded in §2.3.11 (2026-07-15); code is GATED behind the §2.3.11.6 FIRST_ACTIVE review — not yet authorized.** **Amended 2026-08-16:** writing the arbiter + its test matrix on a never-merged/never-pushed `w17-mapper` branch is now owner-approved (§2.3.11.6 amendment: flags default-off, constants fail-closed); merge/push/activation remain gated on R1–R16 + bench evidence. | 2, 3, 4 | Claude (mapper host per U2) | U2 decision **+ FIRST_ACTIVE review (§2.3.11.6)** |
| U5 | iPhone axis/mount validation in the EMV400 (Codex Batch 5) — signs, ranges, roll isolation | 6 | Codex | real device |
| U6 | Simulated-output integration: mapper computes hybrid output with physical output disconnected; every safety transition proven in logs (Codex Batch 7 equivalent, run on the U2-chosen host) | 2, 3, 4 verification | Claude + Codex test vectors | U4, U5 |
| U7 | Bench-only scripted ch9/10 servo sweep, car immobilized/wheels off, tiny limits, slow rate | 7 | Claude (fw bench) + one observer | **Phase B + FIRST_ACTIVE milestone checklist** |
| U8 | Record the **failsafe hold-vs-center re-decision** (firmware behavior on *radio* loss; today: hold-last, `main.cpp` gimbal tick comment / readiness §2.3.5). The hybrid mapping makes sustained off-center positions routine, which raises the stakes of hold-last. Owner decision; document either way. **Amended 2026-08-16:** the firmware now implements **decay-to-center** (vision decision 11, `feat/gimbal-decay-center`, §2.3.12.2 amendment) — "today: hold-last" is history; the driving-scope re-review this row requires **remains owed** and is unchanged by the implementation. | re-decision | owner | before any head-tracked driving |

Firmware-side work items that fall out of U3/U4 (all identified in readiness §4, none
authorized yet): wire `gimbalConfig` into the tuning console (§4.9), gimbal endpoint/clamp
unit tests against real values (§4.10), optional defense-in-depth slew limiter (§4.2).
Deferred cosmetic item: the stale comment at `lib/channels/include/channels/ChannelDecoder.hpp:57-58`
("decoded but unwired until the gimbal deliverable") — a code-file edit, out of scope for
this documentation pass.

### 3.1 Canonical execution order (owner, 2026-07-15)

The concrete cross-repo order from here to a driving milestone. Status annotations are as of
2026-07-15; live status stays in `CURRENT_STATUS.md`.

| # | Step | Owner | Status 2026-07-15 |
|---|---|---|---|
| A | Review and commit the existing iPhone VR Batch 1 work **separately** | Codex/owner | open (uncommitted in `iPhone_rc`) |
| B | Review and commit iPhone R10 sender-safety/canonical-doc work **separately** | Codex/owner | open (uncommitted; automated tests pass, §2.3.12.9 item 4) |
| C | Mirror the canonical bridge contract **only after** receiving the R10 commit hash | Claude (GS) | blocked on B |
| D | Complete the owner-decision / U4 design addendum | Claude (fw docs) | **DONE this pass** (§2.3.12) |
| E | Mapper serial dependency upgrade (`go.bug.st/serial`) as a **separate slice** | Claude (mapper) | **DONE 2026-07-25** — shipped as **v1.6.0**, not the approved v1.7.1 (`f0a18f3`); accepted, reasoning in §2.3.12.9 item 2 |
| F | Mapper diagnostics + Electron subscriber, separate repo sessions | Claude | **already DONE** (CB8 slices 3A–3C, committed) |
| G | A2 no-power mechanical inspection | owner + bench | open (A2 unexecuted) |
| H | Phase B approval | owner | blocked on G |
| I | Measure center, endpoints, signs, usable counts, counts/degree (per axis) | Claude (fw) + bench | blocked on H (U3/CB9) |
| J | Validate real iPhone axes, mount orientation, lifecycle, CB6 bridge behavior | Codex + Claude | needs device + bench network (U1/U5) |
| K | Derive and **sign** the exact shaping constants (§2.3.12.8 policy) | owner | blocked on I, J |
| L | Fresh adversarial review (full §2.3.11.6 R1–R16 re-check) | owner + review | blocked on A–K |
| M | Only after approval: implement U4 in small reviewed slices | Claude (mapper) | **GATED** |
| N | Bench-only physical validation (U6/U7) | Claude + observer | gated (Phase B + milestone) |
| O | Separate driving-readiness milestone (incl. §2.3.12.2 re-review, spotter) | owner | gated |

## 4. Video-loss behavior — RESOLVED 2026-07-15 (owner decision #2; see §2.3.12.1)

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

**RESOLVED 2026-07-15 — the owner selected option 3** (deliberate packet suppression by the
iPhone on local decoder/video loss), composed with the mapper's existing stale
timeout → controlled stale decay; video loss must never directly command a servo or CRSF
value; current W3 log-only operation stays independent of video state; an operator-facing
degraded/lost-video state is required before the active milestone. Full record + constraints:
**§2.3.12.1**. The iPhone-side suppression is future Codex work via the two-stage handoff;
the active behavior is NOT implemented. (Historical options context above retained.)

## 5. Open owner decisions (consolidated)

1. ~~Mapper port/ingest architecture — §2.3 (a)/(b)/(c) + fork ownership.~~
   **RESOLVED 2026-07-15: topology (a), owned fork owns 5602, Electron viewer-only,
   receiver modes mutually exclusive, no relay (§2.3.7).** ~~Remaining sub-item: exact
   owned-fork repo path/name/remote/branch + fork license.~~ **RESOLVED 2026-07-15:
   `w17-mapper` @ `w17-headtrack`, GPL-3.0-or-later (conditional on staying open source),
   push disabled — full provenance record §2.3.12.9 item 1.**
2. ~~Firmware failsafe on radio loss: hold-last vs return-to-center (U8).~~ **RESOLVED FOR
   BENCH ONLY 2026-07-15: hold-last stands for the bench-only FIRST_ACTIVE milestone;
   MUST be re-reviewed before any vehicle driving (§2.3.12.2). Driving scope remains open.**
   **Updated 2026-08-16: vision decision 11 chose decay-to-center and it is implemented
   (`feat/gimbal-decay-center`; §2.3.12.2 amendment). The driving-scope re-review REMAINS
   OPEN — implementing the intended end-state does not discharge it.**
3. ~~Video-loss reaction path (§4, options 1–4).~~ **RESOLVED 2026-07-15: option 3,
   sender-side suppression → ordinary stale decay; operator-facing degraded-video state
   required (§2.3.12.1 / §4).**
4. Camera placement: driver-seat vs halo-height
   (`w17-3d-codex/CAMERA_GIMBAL_PLACEMENT.md`). **Still open.**
5. ~~First head-tracked driving protocol.~~ **RESOLVED 2026-07-15: FIRST_ACTIVE is
   bench-only; a separate driving-readiness milestone requires a reviewed gate, spotter,
   bench evidence, and explicit driving-mode authority approval (§2.3.12.3).**
6. ~~FIRST_ACTIVE controller affordances — L1+R1/R3 failed the conflict audit; pick A or B.~~
   **OWNER-CHOICE RESOLVED 2026-07-15: Alternative C (bench-only), §2.3.12.6** — short-press
   SHARE = recenter; hold D-pad DOWN + OPTIONS 1 s = arm; OPTIONS may then release; D-pad DOWN
   is the continuous held deadman (release = disarm); the right thumb stays free for right-stick
   manual takeover. A and B rejected (held two-thumb chords impede that takeover). **Still
   required before active use:** live mapper node-graph validation that SHARE/OPTIONS/D-pad DOWN
   are unbound. **Bench-only — not approved for driving** (D-pad DOWN occupies the left thumb;
   the driving milestone must choose another deadman/authority UX).
