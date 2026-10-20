# Recomp roadmap

Forward strategy for the recon → build → hw → reconcile loop. Where
[`PLAN.md`](PLAN.md) is the live **queue** (one line per task, move the status as you
go), this file is the **plan**: the critical path, what is blocked and on what, the
risks, and the objective of the next round.

Owner: the `openscanner/planner` agent. The dispatcher consumes it and applies the row
moves it proposes. Statuses, claims and evidence live on the board — this file points at
them and never restates them.

_Last planned: 2026-10-20 (rev 25 — **the single-session round, tightened**. Rev 24 aimed the round at one
room session and made the record its gate; the hours since did not change the aim, they sharpened it.
Four things landed (`#366`, `#375`, `#387`, `#93`), so rev 24's "four PRs at `NEEDS CHANGE`" is one
(`#392`) and four of its board claims are already stale. The fact that re-ranks the round is in the
records already on `main`: both of the 2026-10-20 live runs died at the **first RPC**
(`tests/results/2026-10-20-plan127-live-reply-dump.md`), and `#157`'s fix landed **after** them and has
never met the daemon. So the next session is not another attempt — it is the first test of a landed fix,
and it costs thirty seconds to learn the answer. This revision keeps the objective, replaces the four-run
framing with a **floor + stretch** and a stop rule, turns the record factory into a row (`P12`), and
records the verdict on the operator's candidate skills. **Provenance:** written against `origin/main`
`e1c5e2b` (board through `#159`; open PRs #400 and #392; roster: 7 agents, 0 live model drift, 1 dispatch
session over the context threshold)._

## Objective for the next round

**Prove the live path once — a suffixed `start_stream` accepted and one
`get_frame` reply's contents named, on a record the gate can cite — in one room session whose floor
decides whether the rest of the session runs.**

The **floor** is three things, in this order:

1. **Replay** (`--replay … --dump-replies --capture DIR`): the per-port data/empty tally is recorded. No
   dependency on the live path (the daemon still answers); banked in minutes.
2. **Live IR** (`--live 127.0.0.1 10013 30 --capture DIR-IR --frames 200`): the **first RPC is accepted**
   on the suffixed method, and every `get_frame` reply is `band(N)` or `empty` — never an error, never no
   reply.
3. **Both records are citable**: the raw-log citation lines are generated rather than typed and their
   hashes resolve, and each record's binary sha is the binary that ran.

Both outcomes are results, and both name the next row. A consistent `band(N)` is the pivot's first live
frame → `#132`'s checklist. A consistent `empty` is `#127`'s answer → `#143`. A named rejection names the
next row, and the binary already carries `--dump-replies`, so a rejection produces its own evidence.
**The only failure is a session that produces neither a result nor a named rejection — or a record the
gate cannot cite.**

The **stretch** runs only after the floor: live depth (`#132`'s corpus), then `#71`'s A/B on a still scene
(`#153`'s `exposure n=`, `#76`'s coverage line, `#141`'s UI half, `#151`'s fused-rate half, `#93`'s
markerless stretch). One record
per run. **Stop rule:** if the IR run fails at the first RPC, cancel the depth run — same path, same
defect — but keep the A/B: it exercises a different binary (`scan_server` → the vendor SDK) and is
unaffected by `raptor_client`'s RPC.

**Why this leads.** The room is not scarce because it is unavailable — it ran on 10-19 and on 10-20 — it is
scarce because both of the last session's live runs died at the **first call**, and the fix landed after
them and has never met the daemon. A 30-second run converts a landed fix into a known state. Nothing else
on the board has that ratio: every remaining pivot row (`#105`, `#127`, `#98`'s acceptance, `#132`, `#143`,
`#71`, `#159`'s capture) hangs off this one session, and no device-free row frees more than one.

**Why the record is half the objective.** The last session's output cannot be cited: `#392` fails
`archives` and `record-provenance`, so the evidence exists and is unusable. The room is the loop's scarcest
resource; spending it twice on the same answer is the one failure this round is shaped to prevent. That is
why the record factory (`P12`) is a row and why it lands *before* the session, not beside it.

**What this round is not.** Not `#126`/`#159`'s real curve, `#145`/`#146`/`#150`'s captures, or `#115` —
each needs a *different physical setup* (risk 4). Not `#99`/`#100` (the destination). Not `#113` and the
seams behind it (`C1`/`C2`/`S3`): the census in **Blocked** supplies the fact, the operator still owns the
choice. Not a build landing mid-session (risk 4).

## The two artifacts, and the third layer

A distinction the row list keeps assuming, stated once so the dispatcher can place a row on the right
side of the wall. It is also the answer to "what is the difference between the OBScan API and the
vendor dylib".

- **The OBScan API is the contract.** The `obscan_*` C exports, the `ObScan*` POD structs, enums,
  status codes and opaque handles declared in
  [`obscan_api.h`](../../engine/include/openscanner/obscan_api.h). It has no behaviour of its own. It
  is recovered by recon, reviewed by a human, and it is the only thing `engine/src` may be written
  against. It is **ours**, so a wrong claim in it is a *spec bug* — rows `#5`, `#6`, `#12`, `#13`,
  `#14`, `#15` are all that shape: a header claim the binary contradicted, fixed in the header.
- **The vendor dylib is the implementation.** The shipped binary the contract was recovered from and
  that our loader binds at runtime (the `liborbbec_scan…` dylib and the `libOrbbecSDK…` it depends
  on). It is **evidence**, read only by recon, and never copied into the repository or the app
  bundle. Its internal C++ symbols are not part of the contract; the header states the calling
  convention, not the vendor's structure.
- **Neither one is the daemon.** `#96`'s protocol spec describes a *third* layer: the RPC the dylib
  speaks to Creality's service. The pivot's whole point is that this layer — not the dylib, and not
  the header — is what a replacement must speak (`#98` → `#99` → `#100`).

Why it matters to sequencing: every row is one of **spec** (`docs/recon/**`, the header), **bind**
(`engine/**`, calling the dylib through the header), **own** (`tools/**`, no vendor linkage), or
**protocol** (the daemon, `tools/raptor_client`). They have different walls, different acceptance and
different definitions of done, and a round that mixes them inside one session mixes evidence with
code.

## Critical path

```
THE ROOM — the pivot's only gate
  #157 ✅ (suffix) · #140 ✅ (capture) · #158 ✅ (toggle)
        |
        ├─ P12 (record factory, no device) ──► every record below is citable
        └─ ONE session ─┬─ FLOOR   replay ──────────────► #127 (replay half) · #140's tally
                        ├─ FLOOR   live IR ─┬──────────► #157's daemon check
                        │                   ├──────────► #105 live half ─► #98 acceptance ─► #99 ─► #100
                        │                   ├──────────► #127 (live half)
                        │                   └──────────► #151 (raw-rate half: replies/s)
                        ├─ STRETCH live depth ──────────► #132 ─┬─► #143
                        │                                       ├─► #124's remaining clauses
                        │                                       └─► #159's capture
                        └─ STRETCH A/B (scan_server) ───┬─► #71
                                                        ├─► #153 (`exposure n=`)
                                                        ├─► #76 (coverage line)
                                                        ├─► #141 (UI-open half)
                                                        ├─► #151 (fused-rate half: FRAME_READY/s)
                                                        └─► #93 (markerless stretch in the HUD)

DEVICE-FREE — beside the room, one row freed each
  #144 (existing capture)  ──► OQ-124-5 · #114's mode names · #99's ingest fields
  P11 (S1, the encoder)    ──► #152's budgets and the lower-end target
  P10 (broadcast counters) ──► retires #141's proxy · #152's waste inventory
  #149 (reader half)       ──► the archive (its format is the operator's)
  #154 (probe + presets)   ──► the tier descriptor (the rate axis waits on C2)
  #103 (README)            ──► trust

SECOND ROOM — different setups, scheduled, not this round
  #145 + #150's three captures ──► ONE operator session, one checklist ──► OQ-150-1…6
  #126 (known scene) ──► #159's real curve        [precondition: orphan 3]
```

**Edges, stated so they can be checked.** `#157` → the two live runs (one interface per run, per its own
note). `#140` → every bundle. `#158` → `#71`'s mid-session toggle. The **IR** run → `#105`, `#127`'s live
half, `#157`'s check, `#151`'s raw half; the **depth** run → `#132` only. `#132` → `#143`, `#124`'s
clauses, `#159`'s capture. `#126` → `#159`. `#97` → `#100` (decided). `#144` → `#99`'s *ingest fields*,
not its start; `#146` → `#99`'s calibration correctness, not its start. **`P12` → the citability of every
record**, so it is the only thing that must precede the session.

**Ranked by what frees, not board order:**

1. **The room's floor** — frees `#105`'s live half and `#98`'s acceptance (the pivot's next link), `#127`,
   `#157`'s check; hands `#132`/`#143`/`#71` their inputs. Nothing else is close.
2. **`P12` + `#392`** — frees no row; it is the condition on order 1 being worth anything.
3. **`#144`** — the only device-free row that hands `#99` a field and closes an OQ from evidence already
   on disk, and it does not compete for the room.
4. **The A/B's riders** (`#153`, `#76`, `#141`, `#151`'s fused half) — four rows' observations for zero
   extra sessions.
5. **The capacity set** (`P11`, `P10`, `#149`'s reader, `#154`) — one row each, all feeding the perf
   contract; fills the time the room does not use.
6. **Board and trust** (`#93`'s move, `#103`, `#118`, `#116`, the close pass) — frees no row, but the
   board's own claims stop being wrong.

## Ready now

| Item | Role | What it needs |
| --- | --- | --- |
| **The session's floor** — replay + live IR | hardware + operator | The current binary from `main` (`#157`, `#140` landed). No code change. Pre-test: `strings` the built and bundled binary for the suffixed method name — a stale copy burned a run once (`#107`). One record each, generated hash lines. |
| **The session's stretch** — live depth + `#71`'s A/B | hardware + operator | The same session, after the floor. A still scene for the A/B; `set_plane_removal` (`#158`) toggles mid-session; UI open for the riders. |
| **`P12` — the record factory** | builder | No device. `scripts/hw-postflight.sh` + `tests/results/_TEMPLATE.md`. Acceptance: a wrong hand-typed hash is the only way left to reproduce `#392`'s `archives` failure. |
| **`#392` — repair or close** | record owner + dispatch | Its two provenance defects and the archive hash. It carries `#71`'s fifth attempt; if it cannot be repaired, close it — the batch supersedes it. |
| `#144` — op-31 structures + the 208-B segments | recon | The capture already on disk. No device. Also OQ-124-5's cheapest answer. |
| **`P9`** — the flash-write falsifier | recon | Static, no device. It gates `#100`'s "no required writes" claim. |
| **`P11`** (`S1`, the encoder) · **`P10`** (broadcast counters) | builder | No device. Work orders: `SCALE-ARCHITECTURE.md` §5 and `#141`'s own wall note. |
| `#149` — the reader half | builder | No device (the vendor's own `model.ply`); the write path's format is the operator's. |
| `#154` — probe + presets | builder | Its inputs landed (`#152`, `#156`). |
| `#103` · `#118` · `#116` | dispatch / builder | Small, live, and owned by no dependency. |
| **The board close pass** | dispatch | `#93` is already `HW` (landed, PR #399; its HUD observation rides the A/B run) — no move needed; close or re-point `#134`, `#155`, `#138` (orphans 1–2). |
| `#145` + `#150`'s captures | operator + hardware | **One** session, one checklist (the proposal in **Blocked**). Separate from the room batch. |
| `#146` | operator + hardware | `#140`'s recorder; rides the same operator session if convenient. |

## Blocked, and on what

| Row | Blocked on |
| --- | --- |
| `#105` (live half), `#127` (live half), `#98`'s acceptance | **The room.** The fix is landed; the run is the proof. No code prerequisite remains. |
| `#132` | The room, **depth run**, one interface per run (its analysis checklist is already in the row). |
| `#71` | The room **and a still scene**; `#158` removed the two-process confound, so one session suffices. |
| `#76`, `#141`'s UI half | The **A/B** run with the UI open — and only if the record states the client count and the UI-open state beside every number (risk 2). |
| `#153` | The A/B run's `exposure n=` windows — one push per window, the picture unchanged. |
| `#93`'s observation | The A/B run with the UI open and a markerless stretch — the HUD reads `LOST, LOST, MARKERLESS` with no `markerless window k/3`. |
| `#151` (device half) | The two live runs' **rate census**: replies/s from `raptor_client` is the raw-profile rate, `FRAME_READY`/s from `scan_server` is the fused rate. Its browser render half rides the A/B run's recording. |
| `#126` | The device **and a known scene** (a flat wall at 300/400/500 mm; a checkerboard). The setup is the blocker. |
| `#159` | `#126`, **and** its ground-truth frame settled first (orphan 3). |
| `#143` | `#132`'s bundle. |
| `#124`'s remaining clauses | `#132`'s evidence: the container is named, the band rule is not, the height is not on the wire. |
| `#99` | `#98`'s acceptance (→ the room), `#132`/`#126` for a correct decode, `#146` for calibration correctness. |
| `#100` | `#98` and `#97` (decided; the helper is `#100`'s build). |
| `#113` | **The operator's target rate**, now narrowly framed: the census gives the fact (what the fused cloud and the raw profile actually deliver); the choice (hold the source rate, or chase the raw rate and pay for `C1`/`C2`/`S3`) stays the operator's. Do not dispatch it before both. |
| `#115` | A Windows host **with the scanner attached**; the M1/M2 halves are ready. |
| `#150` | **Three controlled captures**: a known-axis rotation, a known-temperature soak, a fast rotation — one session, one checklist (below). |
| `#149` (write path) | The archive's **format and location** — operator. |
| OQ-150-1…6 | `#150`'s captures (`imu-stream.md` §10.7 names each). |
| G4, G5 | Subsumed by the pivot — Phase C replaces fusion. |
| The Windows lane | Nothing. Handed off by operator rule — see **Windows fork**. |

**One proposal that buys a whole session: consolidate `#145` and `#150`'s three captures into one operator
sitting.** They share every cost — operator, device, a capture directory, `#140`'s bundle convention — and
differ only in the checklist: switch the three modes with `lo0` traffic, pause/resume, the exposure slider,
power-on (`#145`); then ~2 minutes still (temperature drift), a 90° rotation about the table normal (axis
order), and a fast rotation (the gyro's inertness) (`#150`). None needs a different rig. One record, one
bundle, one manifest — five OQs and a mode mapping in a sitting that would otherwise be two. **It does not
merge with `#126`**: a known scene is a different physical setup with its own acceptance (risk 4).

## Orphaned open questions

An OQ with no task row is unowned work — and so is a row whose trigger has fired with nobody watching.
This sweep reads `PLAN.md`'s OQ list against the findings' own open lists and the board as it stands on
`origin/main` `e1c5e2b`. It corrects four stale items from rev 24 and lists nine orphans — seven new, two carried
from rev 24; **one of them is the register itself** (orphan 6).

**Resolved since rev 24 — record, then delete:**

- **`#93` landed** (PR #399, `e1c5e2b`) and the board has since moved it to `HW` (PR #403). The standing
  contradiction with `#81` is closed by the merge; what remains is `#93`'s own owed observation — a
  markerless stretch in the HUD — which the A/B run can supply (it is a `scan_server` session with the UI
  open). Before the merge, no tracking-display claim should have been treated as evidence; after it, the
  claim is the code's own.
- **`#112` is `DONE`** — its HUD capture landed. Rev 24's orphan 1 is closed; do not re-point it.
- **`#109` is `DONE`** — rev 24's hygiene order named a closed row. Drop it from the list.
- **`#366`, `#375`, `#387` landed** — rev 24's risk 5 (a round premised on unmerged measurements) is
  discharged except `#392`.

**Still unapplied from rev 24 — the board edits:** the answered OQs (OQ-1 → `#50`; OQ-4 → `#52`; OQ-5 →
`#47`; OQ-P1…P4 → `#26`–`#29`; OQ-124-1 → `#124`; OQ-124-3 → `#131`); OQ-124-5 re-pointed at `#144`; and
four proposals with no row (`P9`, `P10`, `P11`, and now `P12`).

**New this sweep:**

1. **`#138` needs a close.** Its question is answered in `findings/imu-stream.md`; its residuals are
   `#150`'s. `SPEC-READY` reads as pending work when the work is done. Cheapest: close it citing the
   finding, or move it behind `#150`.
2. **`#134` and `#155` are still `SPEC-READY` with their work done** (rev 24's orphans 2–3, unapplied).
   Each needs an acceptance line or a close, not more work.
3. **`#159`'s ground-truth frame is unowned, and it is a precondition.** The truth (`model.ply`) is written
   after the rotate pose while the recorded frames are the un-rotated cloud (PR #387's verifier names it),
   so a real-data curve would score the wrong points. Cheapest: one recon row reading where the rotate pose
   is applied in the vendor's write chain (static, no device) plus one sentence in `#159`'s note naming the
   authoritative frame. **It must land before `#126` runs**, or the known-scene session produces a curve
   that has to be thrown away.
4. **`#151`'s browser half (receipt→paint) has no owner.** Its device half is in the census above; the
   render half needs a browser. Cheapest: read it off the A/B run's screen recording, the way `#112`'s HUD fields
   were read three independent ways — folded into that record, no new row.
5. **`#127` may already be answered.** Its own summary of `#131` says all 11 elements are named; if that
   holds, "what is inside the array" is a recon answer and the live dump is a confirmation. Cheapest:
   dispatch re-reads `#127` against `frame-codec.md` §10 and narrows it to "the live array has the same
   shape" — so the room is not spent confirming a spec.
6. **The OQ register is no longer the register.** `PLAN.md`'s OQ list stops at OQ-124-5; `#131`'s,
   `#150`'s and `#159`'s OQs live only in their rows and findings. Rev 24's sweep read that list as if it
   were complete, and the next sweep will too — the *method* is unsound until this is fixed. Cheapest:
   state the rule (an OQ with a row is owned by that row; the register holds only the row-less ones) and
   append the row-less ones. A register that is a subset of the truth is worse than a pointer to the rows.
7. **`P12` — the record factory.** `hw-postflight.sh` generates one citation line (the crash report) and
   the raw-log line is typed; `#392`'s `archives` failure is what that costs. Builder;
   `scripts/hw-postflight.sh` + `tests/results/_TEMPLATE.md`. It is the round's only must-precede.
8. **The parked pair** (`OQ-89a–d` + `OQ-S4`) still has no row (rev 24's orphan 8). One adopting row,
   beside `#83`–`#88`.
9. **`ARCHITECTURE.md`'s Roles table is stale in two ways.** It lists six agents; the live roster has seven
   (`intake`), and every model in its column is superseded by the agent files (`builder.md`,
   `verifier.md`, `recon.md` carry the current ones). It is the wall document, so a wrong row is read as
   policy. Dispatch owns the file: one pass, no re-design.

**Fired triggers nobody moved:** `#103` — its gate (`#96`) is `DONE`. `#113` stays gated on the operator
(see **Blocked**).

**Still parked, deliberately:** `OQ-89a–d` and `OQ-S4` (the adopting row above), and
`OQ-S2`/`S7`/`S9`/`S11`/`OQ-7c` (rows `#83`, `#84`, `#85`, `#87`, `#88` — trigger-gated, unchanged).

---

## Scope input — telemetry and capture (rev 21, absorbed into rows)

The operator's two asks are adopted here as a **principle and a list**, not a subsystem:
instrument the surfaces that have to be fast; record every byte the hardware session sees; decode
offline.

**Status (rev 22).** The proposals below are now rows, and the labels in the tables are stale:
**P1 → `#140` (landed)** · **P2 → `#141` (instrument landed, device distribution owed)** ·
**P3 → `#142`** · **P4 → `#143`** · **P5 → `#144`** · **P6 → `#145`** · **P7 → `#146`**.
Do not re-file them. The capture convention survived as `#140`'s `--capture DIR` + `manifest.json`,
which is why the bundle section below is a description of what landed rather than a proposal.

### Telemetry — name the surface, name the schema, stop

| Surface | What exists | What is owed | Row |
| --- | --- | --- | --- |
| `scan_server` live tick | `#111`'s four segments, p50/p95/max, 5 s windows | the **pacing wait** (#139); the exposure re-assert and cloud release as segments (#134); a total-vs-nominal and untimed-residual line | **P2**, `#139` |
| WebSocket broadcast | one timed segment | per-client send time, queue depth, drops and timeouts (the 100 ms send timeout is currently invisible) | **P2** |
| `raptor_client` frame path | per-call status logs (#263), counters | per-call latency, data-vs-empty share, bytes/s, time-to-first-frame; **full-reply capture** | **P1** |
| `ui/` | `dataFps`, `render` fps (different clocks; both labelled) | frame age: device capture time → server tick → render, with a p50/p95 and a rolling log; this is the number `#113` should hold itself to, not either fps | **P3** |
| Phase C math | not written | define the acceptance now: each stage timed against `SPEC-TARGETS.md`, with the same schema | `#99`'s row, when it starts |

**The schema rule.** Every perf record states: clock (`steady_clock`), unit (µs), segment name,
window, and the instrument that emitted it. A record that says "fps" without saying *which* instrument
is not evidence (`#134`). The existing header stays where it is until P1 gives it a second consumer;
then it moves to one shared location — **do not** copy it.

### Capture — the bundle is the artifact

| Surface | Already on disk | Decoder exists? | Row |
| --- | --- | --- | --- |
| `get_frame` replies, 10010/10013 | `session4` — but a sample (`#128`) | partial (`#125` band renderer) | **P1 + #132** |
| IMU, 10011 | 8,643 records in `session4` | no | **`#138`** |
| Property GET/SET, discovery, vendor commands | `session4` + the capture set | partial (`#101`) | **P6** |
| Flash reads (op-25) | startup window | partial (`#95`) | **P7** |
| The 208-B segments; op-31 structures | yes | no | **P5** |
| The project-folder output | the vendor app's own sessions | no map | **`#136`** |
| The tick's latency trace | run logs from `#112` | instrumented, incomplete | **P2/`#139`** |

**The bundle convention (proposed).** Every hardware run that captures writes, outside the repo:

```
<capture-root>/<date>-<slug>/
  manifest.json      # run, branch, commit, binary sha256, start/end, every file's sha256+size
  replies/           # one file per port, length-prefixed records: ts, port, method, msgid, bytes
  perf.csv           # the tick windows, if the run measured them
  stdout.log         # the run's own log (also committed beside the record)
```

The **record** in `tests/results/` cites the manifest's path and hash chain; the bundle itself is
never committed (the same rule as `#123`'s extractor and `.recon-cache/`). The record template gains
an **Artifacts** section so a run with no bundle says so explicitly.

**Not proposed.** A general telemetry framework; always-on raw capture in a hot loop (opt-in, bounded
by seconds/bytes); committing captures; a sampling profiler before a CPU-bound loop exists; decoding
anything before its bytes have provenance.

## Orphaned work — the corpus's `G6`, **closed**

The sweep found one corpus claim that recon has since **refuted in part**, and it changes a
goal rather than adding a row:

- **`G6`'s "drain the data listener" half is no longer soundly motivated.** The findings
  correct §1.5's causal story ("we never drain → the queues fill"): the queues overflow in
  the vendor app too, and the story is in tension with the same corpus's §1.1. So this is not
  a defect to fix on the corpus's reasoning — **do not row it on that basis**.
- **`G6`'s voxel-size half is answered — `#64`, `DONE`: the alignment claim does not survive.**
  (The app's values are per-mode *and per-session*; our tools pass the operator's laser values, at
  the minimum of the app's range.) This section previously said the half "stands, unverified"; that
  was true when it was written and is not now. Nothing to row.

## Goals carried forward from the research corpus (captured once)

_States re-read against the board on 2026-10-19; the board owns any later move._

`docs/research/**` is **frozen** — a historical artifact: no in-place maintenance, no adoption
into findings. Its vendor-fact claims are partly superseded by later recon, so any goal picked
up re-derives the facts it needs through recon like any other target. Captured so they are
tracked and not lost.

| # | Goal | Needs first | State |
| --- | --- | --- | --- |
| **G1** | Live plane removal for **resource management** (cut data per frame during capture), not a post-scan cosmetic cut | — | **landed** — bind `#71` (HW), reimplement the decision `#77` (`DONE`) |
| **G2** | Coverage heatmap: replace the one-scalar "insufficient data" verdict with a spatial under-covered set | nothing — `#40` landed | **landed** — `#76` (`HW`, the observation is owed) |
| **G3** | Owned tracking (marker detection + bundle adjustment) with per-pose rejection reasons | `G2`'s feedback layer | **landed** — `#91` (`DONE`) |
| **G4** | Own denoising + fusion pipeline, Open3D as the MIT anchor | a fusion feature being planned | parked |
| **G5** | Fusion-accuracy evaluation harness | `G4` | parked with `G4` |
| **G6** | Fix our own SDK usage: *drain the listener* + align voxel sizes | — | **closed, both halves** — the drain half was corrected out, and `#64` found the voxel claim does not survive (our values are the operator's own, at the minimum of the app's ranges) |
| **G7** | Keep laser extraction + calibration behind the SDK until the "drop OBScan" decision | that decision (`README.md`) | standing |

Two corpus design constraints bind any row above: keep capture decoupled from fusion via a
bounded queue, and keep the frame-acceptance gate and the bundle adjuster as **two separate
systems**.

**`G1`'s verdict, for its build row** — the adoption row answered more than was asked, and the
answer matters: the re-homing question **dissolves** (same object, two layers), and the call is
**bind the toggles; reimplement only the decision**. It also names the trap plainly: the toggles
are live-callable and gate-free, but **binding them changes nothing about quality**, and the
operator's "it does not work at all" *is* a quality complaint. So the reimplement half is the
half that answers the complaint — the build row should not be scoped as bind-only.

**Historical note, superseded by `#59`.** Rev 12 carried a "re-homing question" here — whether the
plane toggles belong to `OBScanDataManagement` or to the data-management subobject on
`OBScanBase`. The adoption row answered it: **same object, two layers**, and the question
dissolves. It is kept only so the resolution reads as a resolution.

## App workstream (A1–A4) — a basic macOS app, captured 2026-10-18

The client is already built; what was missing is **a shell and a transport**. There is no
packaging anywhere (no `MACOSX_BUNDLE`, no `Info.plist`), and the UI is not served by the app —
`README.md` requires a separate `python3 -m http.server`, and `file://` is explicitly unreliable
there. `docs/architecture/scan-viewer.md` §2 named the destination (a web client plus a **thin
native shell**), so this is a decided direction, not a new one. The A-labels are the planner's;
the dispatcher files the rows.

| # | Foundation | Board row | State |
| --- | --- | --- | --- |
| **A1** | Serve `ui/` from the capture process — one origin, no external static server | **`#61`** | **`DONE`** |
| **A2** | The `.app` bundle + a WKWebView window (`MACOSX_BUNDLE`, `Info.plist`, icon) | **`#63`** | **`DONE`** — gate answered (see **The `A2` decision**) |
| **A3** | Lifecycle: spawn and supervise the worker, surface a crash, and make **quit = graceful `pause`/`stop`/`destroy`** | **`#65`** | **`DONE`** — the post-#73/#74/#106 rerun passed |
| **A4** | Settings and failure paths — missing-SDK dialog, port, session length | **`#66`** | **`DONE`** — steps 5–6 passed in the same launch run |

**Running the app found the rest, and they landed.** `#72` / `#73` / `#74` were not design gaps:
they were what a real bundle discovers when it resolves its own paths and answers the OS. All three
are `DONE`; their hardware verification is the launch rerun, and that is what `A3` was waiting on.

### The `A2` decision — no SDK in the bundle

**Operator decision, 2026-10-18 — confirmed twice, and to be applied: the app does not package
the vendor SDK. It requires the user to have CrealityScan installed, and resolves the dylib from
that install.**

**Provenance.** The operator stated this directly to the planner session, and re-confirmed it
explicitly when the dispatcher's note of the same date recorded the gate as *unconfirmed*. The
dispatcher's note is therefore stale — **no confirmation is pending and `A2` is past its gate.**
The reversal marker below exists so the one line that would change is *named*, not because a
reversal is expected.

**⚠ The one line that changes if the operator reverses it.** *Resolution source* — ours instead
of theirs. On reversal the bundle ships the dylib, requirements 1 and 2 below **collapse** (we
know where our own copy is), the pin check drops to a build-time assertion, and **licensing
becomes the blocker rather than the resolver**.

Rationale, recorded with the decision: redistributing OEM code inside an MIT project is a
licensing problem, and `.recon-cache/` is kept out of git precisely to avoid it. So the bundle
ships our binaries and the web client, and depends on the user's own licensed copy for the SDK.

**Three consequences that turn "resolve from there" into work** — the loader does the first
naively today and the other two not at all:

1. **Make the search a list, not one path.** `FindDefaultLibraryPath()` has a single macOS
   candidate (the `/Applications` app bundle). A user who installed elsewhere — `~/Applications`,
   another volume, a localized name — fails silently. The Windows branch already carries two
   candidates; the macOS one should carry the equivalents.
2. **Fail with a dialog, not a crash.** A missing app must reach A4's actionable message:
   what is missing, where it looked, and what to install. `A2` must not depend on a path
   existing.
3. **Verify the dylib against the pin.** Every ABI claim in the header — struct offsets, vtable
   slots — was recovered from one pinned slice (the sha256 recorded at the top of the
   [`device-message-listener`](../recon/findings/device-message-listener.md) findings). The
   loader checks only that the path *exists*. A user on a newer CrealityScan would get a
   **silently reassigned ABI**, which is the one failure the whole recomp exists to prevent.
   The app should verify the loaded dylib's hash and refuse, or warn loudly, on mismatch.

**Toolchain: nothing new is required.** This machine has CommandLineTools with no Xcode.app —
Apple clang, `codesign`, `notarytool`, `iconutil` and CMake are all present. The two absent
tools, `actool` and `ibtool`, compile asset catalogs and storyboards; a **programmatic ObjC++
shell** (no storyboard, `.icns` via `iconutil`) avoids both. Full Xcode becomes necessary only
for storyboards, asset catalogs, Instruments, or App Store submission.

**Rejected shapes**, for the record: a native SwiftUI + Metal rewrite (forks the UI away from
the portable client and from `V5`); Tauri (a new toolchain — revisit only if the Windows lane
wants to share a shell); and "package the binary and open a browser tab" (not an app).

**Why this is a macOS row and not a Windows one:** it is a macOS-only deliverable, so it sits
inside the machines-split-by-fork rule. The Windows lane wraps the same client in its own shell.

## Loop hygiene (H1–H4)

Housekeeping on the loop's own machinery, not on the product. Both items came from measuring
the dispatcher session itself on 2026-10-18 — the same reason the old inline `## Log` section
was split into `docs/recomp/log/`.

### H1 — Board compaction — **landed** (`930d70b`, PR #178)

**Result: `PLAN.md` 79,660 → 17,758 bytes.** The acceptance written into rev 11 is met — under
the 20 KB budget, with the before/after byte count recorded in the PR. The rule below is kept
because it is the standing discipline at every `→ DONE` transition, not a one-off.

**The measurement.** The dispatcher is one **21-hour session that has never reset**: 303
messages, 6.37M fresh input, 484M cached read. The biggest fixed cost it pays every round is
`PLAN.md`, and **91% of that file is the history of `DONE` rows** — 53 rows, 61,241 of 67,131
bytes, against 4 live rows and ~6 KB. It re-reads the archive to reach the queue.

**The rule.** A row's Notes cell carries **current state plus a pointer to its evidence**.
When a row reaches `DONE`, its full prior Notes moves to a log entry and the row keeps a
one-line outcome and a link to that entry. The board stays the queue; the log is the archive.

**The mechanism**, extending the split that already worked:

1. On a `→ DONE` move, append the row's full prior Notes to
   `docs/recomp/log/<date>-<row>-<slug>.md` in the existing format.
2. Replace the row's Notes with the landing commit, the evidence pointer, and a link to that
   log entry.
3. **Never compact an open row.** The rule applies at the `DONE` transition only — the
   dispatcher must not lose the live board to compaction.

**Budget and acceptance.** `PLAN.md` under **20 KB**, with every open row's detail intact, and
the before/after byte count recorded in the same PR so the effect is checkable rather than
asserted.

**Wall.** `PLAN.md` and `docs/recomp/log/**` are dispatch's; no other role edits them. **No
new agent is added for this** — it is a discipline applied at a transition the dispatcher
already performs, and it is deliberately not a "reviewing layer": a layer that reads the
dispatcher's transcript inherits the same bloat, so layers relocate context rather than
remove it.

### H2 — Model drift: the doc, and the mechanism underneath it

`ARCHITECTURE.md` documented the dispatcher's model as `opencode/nemotron-3-ultra-free` while the live
session ran `deepseek-v4.1-flash#max`. That was the *symptom*; the mechanism is what matters, and
`#104` has now landed the tooling and the explanation:

**A session captures its model at creation and never re-reads the agent file.** Editing an agent's
`model:` changes what *new* sessions get and nothing about a running one — so a corrected agent file
and a stale session can coexist indefinitely, which is exactly how the doc drifted from reality.

**A *disabled* model surfaces as a drain error, not a fallback.** The failure text is
*"Model access is disabled"* — so a session on a withdrawn model does not quietly degrade, it fails in
a way that reads as a transport problem. Worth knowing before diagnosing one.

**The tooling.** `scripts/check-agents.py` validates the agent files (and `#104`'s extension now knows
that a malformed permission list makes an agent vanish silently — see `B1`). `--sessions` reports
session/agent model drift; `--fix` switches a drifting session to its agent's configured model.

**The rule this implies:** a model change is not done when the file is edited. It is done when the
sessions that should carry it have been switched — and the check belongs in the round, not in a
correction afterwards.

### H3 — Check the board before claiming a row is unactioned

**Why it exists.** Rev 12 asserted three times that `G1`'s adoption row had "never been filed"
and was "a standing debt". It had been filed as **`#59`**, dispatched to recon, answered
(`e842e71`) and landed as PR #169 — **`DONE`**. The roadmap read a row that *was* actioned as
unactioned.

This file already warns about the orphan failure — an OQ with no row. This is its mirror image,
and it is worse in one way: an orphan merely goes quietly unowned, whereas a phantom debt invites
someone to **re-do work that is already finished**. The dispatcher caught it, not the planner.

**The rule.** Before this file calls any proposed row unfiled, unactioned or overdue, **grep the
board for the row's own words first** — `grep -i "<distinctive phrase>" PLAN.md`. The roadmap
proposes rows; the board records what became of them; the two drift independently. **A claim
about a row's state is a claim about the board, and must be read from the board.**

**And prefer families to row numbers.** Rev 14 needed three passes to write, because the board moved
67 commits mid-write and each pass invalidated the previous numbering. Where an objective can name a
*family* — "G1's bind half", "the app's run-discovered defects" — the board owns which numbers those
are, and the roadmap stops going stale on every landing.

### H4 — Session compaction (the dispatcher's memory)

**The measurement.** The dispatcher is one long-lived session on a **1,000,000-token** window, and
it has compacted **twice**: cycle 1 = **10.07 h** (session start → first compaction), cycle 2 =
**14.18 h**. Each is a full-history rewrite — the session went **427 → 121 messages** and restarted
at the summary. Its verbatim memory of its own past is therefore **about half a day**, shorter than
some single investigations here. Not a dispatcher quirk: recon, both verifier sessions, a build
session and a recon-sweep each carry a compaction.

**H1's effect is still unmeasured, and cycle 3 is the test.** H1 landed **34 minutes** before
compaction #2, so cycle 2 ran ~96% against the old 79 KB board. Cycle 3 — from
`2026-10-18 19:24` — is the first that runs against the compacted board from the outset. Materially
longer than 14.18 h means H1 helped; near 14 again means the board was never the dominant cost.
Record the interval when it lands.

**Two corrections to how this was first described.** Message *count* is not the unit — one session
here has **999 messages and zero compactions** at ~1.0M cumulative input; the wall is tokens. And a
compaction is lossy for the **model**, not the record: earlier messages stay stored and are merely
no longer sent, so the history is recoverable from the session log.

**The change-set — repo-side, for the dispatcher to land:**

1. **`opencode.json` at the repo root**: `{"compaction": {"auto": true, "keep": {"tokens": 60000}}}`.
   Project-level, so it scopes to this repo's sessions — and it **applies to every agent here, not
   only the dispatcher**, because compaction is session-scoped and cannot be set per agent.
   `auto: true` stays: it is the overflow recovery. `buffer` is left **unset** deliberately — the
   default is 10% of the limit (≈100k here), and the docs' example value of `16000` would be a
   five-fold *reduction*.
2. **`scripts/verify-pr.py`**: add `opencode.json` / `opencode.jsonc` to the agent-tier list.
   **Verified against the script**: as it stands, a file that steers every agent in this repo is
   script-tier and auto-merges with no judgment pass.
3. **No mode change.** `summary` is the default and correct here: `native` is documented for
   **OpenAI Responses models**, and a native checkpoint only works with the same
   provider+model+endpoint — it silently stops working when the model changes, which this loop has
   already done once (see **H2**).

**The agent-facing rule is not repo content.** The `## Context` section drafted for the dispatcher's
agent file applies to the **global** `~/.config/opencode/agents/openscanner/dispatch.md`, which this
repo does not carry (agent definitions are global by design — `ARCHITECTURE.md`, "Where things
live"). So it is **not a PR**: whoever owns the harness applies it. **Order matters — edit the
instruction file, then compact**, because a compaction re-baselines the session against the current
instructions.

## Agent structure — agreed refactor (E1–E10)

Raised from the watcher thread: the dispatcher lost turns to a **foreground blocking call**, which is
a *usage* rule rather than a tooling gap. That opened the larger question — whether the agent
structure itself carries duplicated rules and hand-rolled checks that the platform, or a maintained
tool, already does better.

**The dispatcher answered all four open points on 2026-10-19 and the decisions are recorded below —
this is no longer a proposal awaiting input.** Only `E5` moves now; the rest parks behind `#105`, and
the board sweep waits for `#294` to adjudicate so it is **one revision, not two**.

### What is measurably wrong

| | measurement |
| --- | --- |
| six agent files | **973 lines total** |
| the **Secrets** section | **28 lines × 6 copies** — **three byte-identical, three already diverged** |
| `dispatch.md` | 339 lines, the largest by 2× |
| `commands/reconcile.md` | still **no `agent:` line** — runs as whatever agent the session has, with full `rea` access (W7, open since the audit) |
| `builder` permissions | 7 denies, **no `edit * deny`** — unlisted paths writable by omission |
| `watch-sessions.sh` | its PR half duplicates `gh pr checks`; its session half blocks if run in the foreground |

The Secrets drift is the `aicers/agent-instructions` finding reproduced locally: hand-duplicated
blocks diverge, and the copies that diverge are the ones nobody re-reads.

### The edits

| # | file | change | urgency |
| --- | --- | --- | --- |
| **E1** | `AGENTS.md` *(new)* | the repo-wide rules currently duplicated: clean-room principle, walls in prose, Secrets, pre-run step, *point at rows*, the blocking-call rule | parked |
| **E2** | `opencode.json` | `"instructions": ["AGENTS.md"]` — **same commit as E1** | parked |
| **E3** | all six agent files | delete the duplicated Secrets + pre-run blocks; keep role, permissions, procedure | parked |
| **E4** | `commands/reconcile.md` | **give it an agent** — read-only on the repo, no device, no vendor artifacts; `reconcile` stays as the command that launches it. **Closes W7** (decided: the role is not retired) | parked |
| **E5** | `dispatch.md` | **`wait`, not `/api/event`, for completion; backgrounded or timeout-bounded, never foreground; the PR half backgrounded too.** Read as covering **product** code as well — *any blocking read gets a deadline* — which is what `#117` already carries, so rule and row agree rather than duplicate | **now** |
| **E6** | `builder.md` permissions | restore `edit * deny` plus the audit's allow list | **operator's call** |
| **E7** | `scripts/check-agents.py` | verify `AGENTS.md` exists; drift-check that no agent re-introduces a shared block; every command carries `agent:` | parked, after E3 |
| **E8** | `scripts/verify-pr.py` | **`shellcheck`** (`scripts/*.sh`, `.claude/hooks/*.sh`) + **`actionlint`** (`.github/workflows/**`) + **`lychee` offline/internal only** (network checks are a flake source). **`markdownlint` declined** — the whitespace gate already owns trailing whitespace, and a second home for one class is the drift this project exists to remove. Each adopted tool **replaces** a hand-rolled check in the same PR; the tier gate, record provenance, archive hashes and plan syntax stay | parked |
| **E9** | `scripts/watch-sessions.sh` | drop the PR half (`gh`, backgrounded, already maintained) | parked |
| **E10** | `.opencode/plugins/` *(optional)* | `ctx.event.subscribe` + `ctx.session.synthetic` — wake the dispatcher server-side, no tool call | optional |

### What this must not change

- **Permissions stay in frontmatter.** `AGENTS.md` is prose and cannot enforce a wall. E3 removes
  *narrative* duplication only — W1/W2/W4/W5 stay exactly where they are.
- **No new agents.** Six is the set; adding agents to fix orchestration is the move the workflow
  audit's §5.4 warns against.
- **The board/roadmap split.** E1 must not become a third place where row state is written.

### `AGENTS.md` — decided

**One file, natively read** (dispatcher, 2026-10-19). The shared text is short and unparameterised, so
drift-detected copies would buy a *detector* for a problem the single file **removes**. Each agent file
stays thin — its role and its walls — and points at `AGENTS.md` for the shared blocks. The dispatcher
owns its maintenance.

### Sequencing, and the one split that matters

```
E1 + E2 (one commit) ─► E3 (thin) ─► E7 (guard)
E5 — independent, now
E4, E6, E8, E9, E10 — independent, park behind the pivot
```

**Confirmed: only `E5` moves now.** It costs the dispatcher turns; everything else buys drift
prevention, not capability, while `#105` blocks the whole pivot. `E4` stays in the parked group even
though it is now *decided* — it is a config edit in the operator's reach, not a round item.

### The four answers, recorded

| | question | answer |
| --- | --- | --- |
| **I1** | `AGENTS.md`, or drift-checked copies? | **single file**; the dispatcher owns its maintenance |
| **I2** | which external linters? | **`shellcheck` + `actionlint` + `lychee` offline-only**; **`markdownlint` declined**; each adopted tool replaces a hand-rolled check in the same PR |
| **I3** | sequencing | **only `E5` now**; `E1`–`E4`/`E7`–`E9` park behind `#105`; `E5` also covers product code, agreeing with `#117` |
| **D1** | `analyze`/`reconcile`: agent or retire? | **agent** — the spec/impl/test classification must not move into the hardware record template, or the classifier becomes the party under test. W7 closes |

### The sweep is deferred, by agreement

Wait for **#294** to adjudicate, then fold `#111`–`#117` (the tick-latency thread, the stream-profile
question, portability) **and** the corrected ledger into **one** pass, not two. Expected once `#294`
lands: **`DONE`** `#65`/`#66`/`#77`/`#91` · **`HW`** `#71`/`#76`/`#98`/`#105`/`#112` ·
**`BLOCKED`** `#113` · **`BUILD`** `#117`.

**One caution carried forward:** `#112`'s `BLOCKED` is **stale** — its blockers (`#111` landed, the
hardware room synced) are gone, which is why `#294` moves it to `HW`. Do not carry `BLOCKED` forward
if `#294` has landed when the sweep runs.

### Reach — who can actually apply each

| edit | who |
| --- | --- |
| E1, E2, E7, E8, E9 | **dispatch** — repo files; `AGENTS.md` poses the same reach question `README.md` had, which is now resolved |
| E3, E4, E5, E6, E10 | **operator** — `~/.config/opencode/**` is outside every agent's edit allow |

**One process note.** E5 exists because a rule was stated where the caller did not read it at the
moment it mattered — the same failure `H3` records. The fix is the same: **put the rule where the
action happens**, which for a blocking call is the *Dispatching* section of `dispatch.md`, not a
hygiene appendix.

## The pivot — posture, parity, and what parks

### Clean-room posture (standing; §9)

Three constraints carry the whole effort's defensibility, and they do not relax because the target
changed:

- **Recon writes specs from observation** — the wire captures and the MIT-licensed public-SDK
  headers. That is the cleanest provenance this project has had, and it stays the method.
- **The builder never sees captures or vendor code.** It implements from `docs/recon/findings`
  specs, exactly as it does today. Protocol facts live in findings, tagged `[inferred]` until a run
  confirms them.
- **The extracted firmware and any OBScan/libOrbbecSDK analysis are recon-side, read-only inputs.**
  They inform specs; they never reach the builder or the committed implementation.

**The captures are evidence, not spec.** `raptor-protocol-capture/` is the observation record; what
ships is the findings doc derived from it. That keeps the wall where it has always been — and it is
why `#96` is a **recon** row, not a build row.

### What parks, and why

The pivot re-ranks everything on the board. Ranked against it:

| workstream | verdict | why |
| --- | --- | --- |
| `#96` → `#98` → `#99` → `#100` | **the queue** | the pivot, in dependency order |
| `#95`, `#97`, `#101`, `#102` | **stays — they gate the pivot** | G-C, G-B, G-D |
| `#90`, `#92`, `#93` | **stays** | small and live: `#90` is the `B1` guard, `#93` a real UI debounce bug |
| the owed reruns (`#65`/`#66`/`#71`/`#73`/`#76`/`#77`/`#89`) | **stays, low priority** | evidence debts on merged work; one room, no new code |
| `#82` | **parks** | the tracking status's fresh-value origin — the `G3` chain stops mattering if the pipeline is replaced |
| `#83`–`#88` (six OQs) | **parks** | each already carries its trigger; none is on the pivot's path |
| `G1`/`G2` goals (`#71`/`#76`/`#77`) | **parks after their reruns** | Phase C replaces the pipeline they patch |
| `G7` ("keep laser extraction behind the SDK") | **superseded by the pivot** | the pivot *is* replacing it; the goal's own condition has been met by decision |
| the Windows lane | unchanged | still handed off; the pivot makes cross-platform *possible* (Linux needs a udev rule, §5 L3), which is a later benefit |

**"Parks" means noted, not abandoned — and the condition is specific.** The `G`-goals patch the
vendor's pipeline; **Phase C replaces that same pipeline**. Building on the vendor's output while
planning to replace it would be work done twice, so the parking is causal, not a demotion. That is
the pivot's real cost and it should be stated plainly rather than left implied.

**One honest asymmetry.** The owed reruns stay *above* the parked work but *below* the pivot: they
are the only open items that can still fail — `#73`, `#77`, `#89` all landed code whose hardware
behaviour is unproven. Parking them would bank unverified work; running them costs one room.

### Firmware — explicitly off the critical path (§7)

Firmware replacement is **a future research track, not in scope now**. The value is low — §2 showed
the device is dumb, so the scanning smarts are host-side — and the risk is qualitatively different
from every other step: a failed write can brick the unit and is **not reversible by killing a
process**. It is also **not required for host-side independence**.

**Pre-gates, if it is ever pursued:** a recovery path proven on *this* unit **before** any write; the
full flash map, so calibration, serial and boot are preserved; and ideally a second unit.

**The extracted image is useful now, read-only and off-device**: it confirms the SoC and the
camera/laser driver details that inform the host loops, and cross-checks the vendor-command opcode
semantics. That is recon-side input (see the posture above), never builder-side.

## rev 19 — the pivot's first live run, and what it re-ranks

### `#105` — what leads, and in what order

The failure is a call that returns **success and produces nothing**, so the first requirement is
*legibility*, not a fix:

1. **Per-call status logging** (build-1, in flight) — the **prerequisite**, not a parallel track.
   Nothing downstream can be believed until accepted-vs-rejected is observable rather than inferred.
2. **Verbatim replay from the captures** (recon-1, **reframed**). Not "re-derive the shapes" —
   *extract the vendor's own request bytes and send them unchanged*. Replay **falsifies**;
   re-derivation **interprets**. A replay that streams proves the shapes are wrong; a replay that does
   not rules the shapes *out* and moves the weight to sequencing and cadence.
3. **Retire candidate (d) from evidence.** The captures carry the vendor's time-to-first-frame; if it
   falls inside 5 s, "5 s was too short" dies without another run.
4. **Only then interpret.** Candidate (b) — the laser/exposure sequence — leads, and the client already
   sends `SetProperty` for props 3/152/153/154/155 every iteration, so the open question is the
   **values and the ordering**. That is `#101`'s subject, which is why `#101` is promoted, not parked.

**This is a good failure.** The cheapest thing that could go wrong went wrong in the cheapest possible
way: a clean run that returns nothing, with the captures *and a working vendor client* in hand as the
comparison. Nothing was bricked, nothing is ambiguous, and the falsifying test is already on disk.

### What parks, and one promotion

| workstream | verdict | why |
| --- | --- | --- |
| `#105`, `#101` | **the queue** | `#105` gates acceptance; `#101` settles the values it turns on |
| `#97` (`G-B`) | **stays** | gates `#100`; a decision, and cheap |
| `#89`'s fix | **stays** | a *failed* observation, not a passed one |
| the six `HW` reruns | **stays** | evidence on merged work; one room, no new code |
| `#102`, `#103`, `#93` | **stays, low** | an audit, a doc pass, and a live UI bug |
| `#83`–`#88` (six OQs) | **parks** | each carries its trigger; none is on the pivot's path |

**`#101` moves the other way** — parked to on-path. It was filed as a completeness item, "vendor
properties' semantics"; `#105` turned it into a **diagnostic input**. Worth recording as the first
time a parked OQ has been *promoted by evidence* rather than by priority.

### The sweep (`H3`)

| this file said | board now | correction |
| --- | --- | --- |
| `#73` `HW` | **`DONE`** | the rerun passed — idempotent guard, the app's 120 s deadline, no orphan |
| `#89` `HW`, "a rerun owed" | **`BUILD`** | the run was **inconclusive** — its 5 s bound never fired, so the task is a *fix* |
| `#95` `TODO` | **`DONE`** | the calibration decode landed, with **run-dependent residuals** |
| `#98` `BUILD` | **`HW`** | code landed; the acceptance run failed → `#105` |
| `#104` — | **`DONE`** | session-model drift, with `check-agents.py --sessions/--fix` |
| — | **`#105` (new)** | the zero-frames blocker |

**Two of these were in open PRs, not on `main`, when I read.** `#73`'s move and `#95`'s `DONE` sit in
**#261**/**#262**, in flight; I planned against their imminent state and marked where the board has not
caught up. The alternative was a sweep that would be stale within the hour.

### Still open from rev 18: the README's licence claim

`README.md` still says **"MIT-licensed public-SDK headers"**, and the erratum still says "not confirmed
MIT". #260 corrected these in `docs/recon/findings/protocol-spec.md` instead — which now says the
accurate thing: *"the checkout's `src-read/LICENSE.txt` claims MIT; the upstream license is not
confirmed"*.

My rev-18 reading **corroborates and narrows** that: `OrbbecSDK_v2`'s own README §5 claims MIT *with
third-party exceptions* — which is also why GitHub reports the repo as **"Other"**. So two independent
in-repo statements now claim MIT, and the residual is a review of the specific files relied on, not the
question "is it MIT?". Both the README's sentence and the erratum should be brought to that wording;
it belongs with `#103`.

## rev 18 — README close-out, the sweep, and one new open question

**The README correction is closed**: applied verbatim as **#252**, the file now has an owner
(dispatch), and the erratum landed. Full record above.

### The sweep (`H3`) — what moved since rev 17

| this file said | board says now | correction |
| --- | --- | --- |
| `#96` is the pivot's first work item, `RECON` | **`#96` `DONE`** | Phase A closed — **G-A is cleared**, and **`#98` (Phase B) is `BUILD`** |
| `#98` "after G-A", no row state | **`#98` `BUILD`** | moved from `TODO`; the C1 client is in flight — it is now the critical path's head |
| `#91`/`#92` "code landed, row move pending" | **`#91` `HW`** | the move happened; it owes a **rerun**, not a row |
| the README's `Status`/`Known issues` need "a pass once Phase A exists" | **`#103` filed** | rowed — no longer a note here |
| — | **`#104` (new)** | session-model drift: extend `check-agents.py` to compare each live session's model against its agent definition |

**`#96` closing moves the critical path's head.** The live edge is now **`#98` → `#99`**, with `#95`
(G-C) and `#97` (G-B) still the two gates that matter.

### The README needs one more pass — and the erratum can now be narrowed

The section just applied says **"the MIT-licensed public-SDK headers"**, and `FULL-INDEPENDENCE.md`'s
erratum marks that attribution *unverified*. Having read the source, the erratum narrows from
"unconfirmed" to a **fact**:

- `orbbec/OrbbecSDK_v2`'s own README §5: *"This project is licensed under the **MIT License** with some
  other licenses for third-party libraries and extensions."*
- GitHub's classifier reports the repo as **"Other"** — because of that mixed licensing, **not**
  because the licence is unknown.

So "MIT-licensed" is **right for Orbbec's own source and wrong as a blanket description of the repo**.
The README should either say exactly that, or drop the licence adjective and defer to the erratum.
This sits in **`#103`'s neighbourhood**, but it is a *factual* fix rather than a "binding-era" pass —
it should not be lost inside that one.

### A new open question the same reading turned up

`OrbbecSDK_v2`'s README documents **two USB protocol families**: OpenNI for v1.x firmware, **UVC for
v2.x** — and Orbbec has been migrating devices between them since v2.5.5 (Oct 2025), noting that the
*same* product line can speak either depending on its **firmware major version**.

**`FULL-INDEPENDENCE.md` §5 assumes UVC in C2, and §8 does not list the family as an unknown.** So:
**which family does the Raptor Pro speak?** It is cheaply checkable — a firmware major version answers
it — and it materially shapes **`#100`**, because an OpenNI-protocol device is *not* "UVC streaming +
UVC controls + an extension-unit vendor channel". This wants an OQ on the `#100` chain **before** C2
is designed, not after.

## rev 16 close-out, and the board sweep

**rev 16's objective landed.** `G3`'s build row was filed and built — `#91`, with `#92` — so the last
`G`-goal without a build row now has one. `B1` was fixed, and **`#90`** puts the guard on the board so
a malformed permission list can no longer remove an agent silently; `#93` followed as a live UI
correction.

**Swept per `H3` — three corrections:**

- **`#73` and `#89` are `HW`, not `BUILD`.** Rev 16 recorded them as "`BUILD` with fixes in
  verification"; the board has since moved them, so they belong in the owed-rerun set rather than the
  builder queue.
- **`#91`/`#92` still read `BUILD`** although their code has landed. The row move is pending — noted
  so the next sweep does not read it as open work.
- **`#94` closed while this revision was being written.** At the moment of my sweep it read `RECON`
  with **no findings doc on main**; `calibration-store.md` then landed and `#94` → `DONE`. So the
  claim I would otherwise have made — two open rows doing one job — is **not** the case: `#94`
  located the calibration, `#95` decodes its payloads, and `#95`'s open questions are the hand-off
  between them. Recorded because the check ran a few minutes too early, which is the general hazard
  with row claims.

## rev 15 close-out — the walls, and the builder agent

**What landed.** The walls are real and were *probed*, not merely configured: `W1` (the dispatcher's
blanket `edit * deny`), `W2` (the builder's edit walls), `W4` (the verifier's vendor read denies) and
`W5` (`recon-sweep`'s shell denies) are fixed, and the dispatcher's wall was verified **empirically**
— an edit to `docs/recon/**` is refused while `scripts/**` and `docs/recomp/**` still work. That is
the strongest form this check can take, and it is the standard the remaining items should meet. The
shadow agents are gone. The repo-side change-set landed too: the root `opencode.json`, the corrected
`ARCHITECTURE.md` sentence (which now names the gap honestly — *edit walls enforced, shell patterns
mitigations, physical separation still to come*), and the six live OQs as rows **`#83`–`#88`** with
their triggers and cheapest retirements.

**What remains of the change-set.** The two items the audit always ranked as the real controls:

- **the shell-pattern denies** — mitigation only, since the audit conceded text patterns are
  bypassable by a variable, a glob, or a `cd`; and
- **the physical `.recon-cache` separation** — the version that holds if anyone challenges the
  clean-room claim.

Both are the operator's, both config-or-filesystem, and neither is dispatchable to a repo role.

**One correction to "fixed" — verified 2026-10-19, and it is the wall's mechanism, not its named
paths.** `W2`'s *named* leaks are closed: the builder can no longer write the findings it consumes,
the hardware records, `PLAN.md`, `.github/**` or `.claude/**`. But it is **not the mechanism the audit
specified**. The audit asked for `edit * deny` plus allows; what landed is **seven denies, zero
allows, and no blanket deny** (`blanket_deny: 0`). An enumeration closes the paths it names and
**nothing else** — so the builder still reaches `README.md`, `CLAUDE.md`, `docs/research/**`,
`docs/architecture/**`, `docs/tooling/**` and `THIRD_PARTY_LICENSES.md`, silently, by the **absence**
of a rule rather than by a decision.

**That is why the root README carried a factual error.** No role *owned* it: it appeared in no
agent's allow list, and the only reason anything could write it is that absent deny — so the drift was
**structural, not accidental**. It is the third unowned artifact after `docs/research/**` and
`docs/architecture/**`, and the first to produce a user-visible wrong claim.

**One of the two fixes has landed; one has not.** The **owner is fixed** — `README.md` is now in
dispatch's allow list, with the wall record updated in `ARCHITECTURE.md`, so the file is no longer
reachable only by accident. **`deny:*` on the builder is still absent (W2 is enumeration-based), and
restoring it is the operator's call** — not the planner's and not the dispatcher's. The owner fix
removes the consequence; it does not close the wall.

### The root README's factual error — **closed** (#252), one phrase outstanding

`README.md`'s "Possible future direction" section claimed the low-level layer
(`libOrbbecSDK.1.7.1.dylib`) is "Orbbec's own **public, open-source** SDK … the same one published on
GitHub". **Verified wrong against Orbbec's own GitHub org.** `orbbec/OrbbecSDK` describes itself as
*"Orbbec SDK v1&v2 **Pre-Compiled** Repo"* — **binaries, not source** — while
`orbbec/OrbbecSDK_v2` is *"Orbbec SDK v2 **Open-Source** Repo"*, and it is a **different** codebase.

So: **the open-source SDK starts at v2**, and v2 does not list the Raptor Pro's `0x0c03` (a live
enumeration probe saw zero devices). **The layer we actually bind has no source to read** — which is
the strongest argument for the pivot, not against it: the observed protocol is the only route to
independence, because the SDK route was never open.

The corrected section is handed to the operator with the verification. It is **not applied here**
because no role has edit reach on `README.md` — see the correction above. That is also why a claim
this wrong survived: nobody could fix it.

**`B1` — the builder agent does not load, and the cause is exact.** `openscanner/builder` is absent
from the loaded agents list, and the server log says so on every attempt: `Error: Agent not found:
openscanner/builder`. The file is present, and it parses under a lenient YAML reader. The difference
is in its permission list — **it is the only agent with incomplete entries**:

| agent | rules | issues |
| --- | --- | --- |
| **`builder`** | 21 | **2 incomplete entries**, plus a duplicated `resource`/`effect` pair |
| dispatch · planner · recon · recon-sweep · verifier | 19 · 16 · 7 · 7 · 11 | none |

Every other agent issues complete `{action, resource, effect}` triples. `builder.md` carries
`- action: opencode_list_mcp_resources` and a bare `- action: shell`, each with **no `resource` and
no `effect`**, and its `*.recon-cache*` entry repeats `resource`/`effect` inside one mapping.
opencode then drops the agent **silently** — no error on the file, only "Agent not found" when a
session tries to start. The fix is three lines: complete the two entries, split the duplicated pair.

**That is itself a finding about the wall work.** A malformed permission list makes an agent
*vanish*, with nothing pointing at the file — so a hand-edit to a wall can silently remove an agent
rather than merely weaken it, and the failure surfaces far away, as "the agent doesn't exist".
**Wall edits need a validation step**: every permission entry a complete triple, and all six agents
present in `debug agents` after the edit. That belongs with the remaining change-set items, and it is
cheaper than the items it protects.

## Wall integrity and the workflow change-set (rev 15)

Two independent reports — a **workflow audit** (walls, models, drift) and an **optimization
proposal** (rewrite rates, landing load, context) — were checked against the *live* configuration.
Every claim the planner spot-checked reproduced from the loaded rules
(`opencode-cli debug agents`), not merely from the `.md` files. This section consolidates them into
one change-set, states the planner's amendments and why, and separates what is repo-editable from
what lives in the harness.

**Provenance.** Both source reports live outside the repo. They are not reproduced here; the
findings below are the parts that drive rows. If they are wanted as repo artifacts, the dispatcher
lands them.

### What was verified

| # | Finding | Ground truth (loaded config) |
| --- | --- | --- |
| W1 | the dispatcher can edit anything | 4 `edit` allows, **no `edit * deny`** — and the 04:16 backup *had* it |
| W2 | the builder's edit wall is the header only | one rule: `deny obscan_api.h`; findings, `tests/results`, `PLAN.md`, `.github`, `.claude` all editable |
| W3 | `.recon-cache` is walled on the **read tool** only | all four agents carry the read deny; none has a shell rule for it, and shell denies match command text alone |
| W4 | the verifier is not walled from the vendor app | its read denies are `.recon-cache/**` **alone** — no CrealityScan, no `/Library/Creality/` |
| W5 | `recon-sweep` can start Ghidra through the shell | `shellN: 0` — no shell denies at all, on the fan-out agent |
| W6 | six shadow agents are live | `agents/openscanner.backup-20261018-041651/` loads as selectable agents |
| M1 | the model table is wrong in every row | the doc says nemotron/glm/beam/space-bunny; the loop runs deepseek×3 + qwen×2 |

**W1 and W6 are regressions, not omissions.** The `edit * deny` existed in the backup, and the live
allows are *broader* than the backup's — something dropped a base rule, and the backup preserves a
copy of the rules that still had it.

### The change-set, ordered

1. **Remove the shadow agents** — `agents/openscanner.backup-*/` out of `agents/`. *(operator, seconds)*
2. **Restore the edit walls** — `edit * deny` above the allows on dispatch and builder, then
   builder's allows (`engine/src`, `mesh`, `ui`, `tools`, `tests/hardware`, `**/CMakeLists.txt`).
   *(operator, config)*
3. **Shell denies for the vendor/Ghidra patterns**, on every agent **except `recon`**. *(operator)*
4. **Wall the verifier from the vendor app** — the two read denies plus the rea MCP resource denies
   the builder already has. *(operator)*
5. **Make the clean-room wall physical** — move `.recon-cache` out of the repo tree, reachable only
   by recon. *(operator, larger)*
6. **Correct `ARCHITECTURE.md`** — the "enforced" sentence, the model table, and the wall-table
   omissions (recon's `PLAN.md` allow; the dispatcher's `scripts/**` and `/private/var/folders/**`).
   *(dispatcher, `docs/recomp/**`)*
7. **Give `analyze`/`reconcile` an agent, or retire the role.** *(operator + `commands/`)*
8. **Align the models** with the stated reasoning — or rewrite the reasoning. *(operator)*
9. **Resolve the prompt drift** — land the compaction config and `compact-session.sh`, or delete
   those lines from the prompt. *(dispatcher)*
10. **Repo-doc drift** — `CLAUDE.md`'s stale interposer line; the hardware hook's match pattern;
    `builder.md`'s duplicate step 6. *(dispatcher)*
11. **One shared instructions file** for the duplicated Secrets / Pre-run blocks. *(operator + repo)*
12. **A fresh dispatcher session per round.** *(operator)*

### The planner's amendments, and why

**A1 — the shadow agents go first, not third.** The audit ranks them third. Removing them takes
seconds, and until it is done every wall fix is partly undone by an agent that may be *picked
instead* — carrying older rules and, in the backup `recon-sweep`, a different model. Fixing the
front door while a second door stands open.

**A2 — the physical wall is the control, not a hardening step.** The audit concedes that text-pattern
shell denies are bypassable. That makes step 3 *mitigation* and step 5 the only thing genuinely
enforced. Do 3 today; start 5 immediately; and **stop describing the walls as enforced** until 5
lands — which is why amendment 6 is a fix, not a nicety.

**A3 — the recon variant bump has a cost the audit doesn't weigh.** It recommends recon → high or
max, and the reasoning is sound: spec errors are the most expensive to get wrong. But recon's
session measured **16.9M input tokens — the largest in the loop**, ~2.7× the dispatcher. Raising the
variant raises that directly. The honest framing is *"recon at max **and** keep the one-row-per-
session rule tight"* — the cost driver is the context, not the variant.

**A4 — the two reports conflict on the builder's self-check, and the PR must say which wins.** The
optimization report (§2.4) wants the builder to run the `/openscanner/verify` checklist exactly; the
audit (P3) wants the real gate kept independent. Both are right. State the reconciliation in one
line: **the builder runs the checklist, does not post a verdict, and the independent session still
posts the SHA-pinned one.**

**A5 — an implementation trap: the Ghidra shell-deny must not reach `recon`.** "Every agent except
recon" is correct and easy to get wrong. `recon` has `shellN: 0` *by design* — it is the only agent
that must run Ghidra; `recon-sweep`'s `shellN: 0` is the hole. Name the difference, or someone will
"fix" recon and remove the loop's only decompile path.

**A6 — the verifier's GitHub identity is a credential.** Wanting one is right, but the change must
say where it lives — operator credential store, or a GitHub App — and that it never enters the repo,
a PR body or a comment. Same rule as the OpenCode service password.

**A7 — the optimization report's §2.1 overstates its own effect.** "About half of today's blocking
findings never reach a PR": the largest single mechanical bucket is stale prose (~7 of ~14), which
is **not mechanically checkable** — it is §2.3's sweep, a procedure, not a gate. Scriptable are the
template/`Head` check and the citation checks: perhaps 4–5 of 14. The realistic effect is **a third,
not a half**, which moves the emphasis onto §2.3 and demotes §2.1 from first place.

**A8 — §5.1 folds the classifier into the party under test.** Folding "spec wrong / impl wrong / test
wrong" into the hardware record template asks the hw-test agent — which *wrote* the observation — to
adjudicate whose fault a mismatch is. The audit's own fallback (give `analyze` an agent and a wall)
preserves the independence. **Retire the role if you like; do not retire the independence.**

**A9 — the planner's own churn, measured this session.** Absent from both reports because it is
mid-round and pre-PR: **three stale passes on one revision, 9 commits ahead of main, and three
`rev 14` commits because a pushed hand-off was amended.** Three rules, all cheap: *land a revision
before starting the next*; *never amend a pushed hand-off* — it rewrites the SHA the branch is
identified by, and `git cherry` can then never match it; *prefer families to row numbers*, already
recorded in **`H3`**.

**A10 — §1's `node --check` is already implemented.** `verify-pr.py` carries `ui-js-syntax`, and the
template/`Head` check exists as **INFO** — so the real work is promoting an existing check to a
failure. Worth stating because it makes §2.1 cheaper, and because it shows both reports are
snapshots of a fast-moving loop: **re-check their top-ranked items against the code before landing.**

## Windows fork — handed off

**Out of scope for the macOS lane.** By operator rule (2026-10-18): Mac sessions work the
macOS fork only; the Windows 11 machine's agents own the Windows fork; cross-machine traffic
is knowledge hand-offs over the coordination channel, not shared work. `#57` (the Win64 gate)
was the last macOS item there, and its answer is recorded on its own row and in the findings
it names.

This file therefore stops tracking the Windows workstream. Its charter and the coordination
file live at `docs/recomp/WINDOWS-FORK.md` and `docs/recomp/FORK-COORDINATION.md`, and the
fork is created and seeded — the coordination file's log is the record, and the contract pin
is set there. **Do not open Windows rows from this roadmap.**

## Risks

1. **The record is still the failing artifact, and it is the round's top risk.** `#392` is the only open
   record PR and it fails `archives` (a cited `sha256` that does not match the log beside it; one that
   resolves to no file) and `record-provenance` (two records citing no raw evidence). Cost: the next
   session produces evidence the gate rejects, and the room — the loop's scarcest resource — is spent
   twice. Cheapest: **`P12` before the session** (the generated citation line and the template rule), and
   repair-or-close `#392`.
2. **The instrument can lie about what it measures.** `#134`'s lesson is structural: the live and synthetic
   numbers are different instruments on different paths, and the last claimed improvement was a name
   mismatch. Cheapest: every duration in a record states the client count and the UI-open state beside it;
   every bundle carries `#140`'s manifest and the binary sha.
3. **One interface per run.** `#157`'s own note. Cost: a session that expects both ports in one run comes
   back with half a corpus and a record that reads as failure. Cheapest: the floor is one interface; every
   record names its port, its frame count and its reply rate.
4. **One serialized room, three different setups.** The room batch (scanner + daemon), `#145`/`#150`'s
   captures (controlled motion), and `#126` (a known scene) cannot share a session. Cost: a run voided by a
   mid-batch build, or a batch that silently mixes setups. Cheapest: freeze builds at the session's start;
   keep the three separate; fold `#145` and `#150` into one sitting (the proposal in **Blocked**).
5. **The dispatcher's own session is over threshold — measured, not predicted.** `check-agents.py
   --sessions` reports one dispatch session at ~1.49M input tokens against a 1M threshold; the roster is
   otherwise clean (0 live drift, down from rev 24's 19). Cost: judgment drift on a summary is exactly what
   a long dispatch session produces, and this round's orders need the board read correctly. Cheapest:
   recreate before the batch (`scripts/new-session.sh`, `#109`'s recipe) and keep the board the single
   queue.
6. **`[inferred]` claims the code leans on.** (a) `#153`'s on-change exposure rests on one record's reading
   that the setter is not needed per tick — the A/B's `exposure n=` windows confirm it; (b) `#159`'s
   ground-truth frame (orphan 3) — a static read before `#126`; (c) `frame-codec.md`'s band cut and pixel
   scale — `#132`.
7. **The capture corpus is a sample, not a stream.** `#128` measured it; any band rule or pixel scale
   derived from it inherits n = 11. Cheapest: `#132`'s gap-free runs with provenance in the manifest.
8. **Bundles sit outside the gate.** A record can cite an artifact the verifier cannot read; `#140`'s
   manifest is the tether and `#392` shows it failing. Cheapest: risk 1's generated line names a path and a
   sha that the manifest chain resolves.
9. **The rate target is a decision, not a build.** `#151`'s census supplies the fact; the choice is the
   operator's. Cost: a round spent on a wait primitive that cannot meet its own acceptance. Cheapest:
   answer it after the census, and only then is `#113` "hold the source rate" or `C1`/`C2`/`S3`.
10. **Capture scope creep.** Keep `#140`'s capture opt-in and bounded; an always-on full-reply dump in the
    hot path becomes the problem it measures.
11. **The dylib may not be the slice we specced.** Every ABI claim came from one pinned slice and the
    loader checks only that the path exists. Cheapest: hash-at-load, refuse on mismatch.
12. **The plan itself can be the stale artifact — this revision is the evidence.** Rev 24 was written
    against `2dd8a7e`; `main` moved 18 commits in the hour, and four of its board claims were wrong by the
    time anyone read it (`#112` and `#109` were `DONE`; `#93`'s work had landed; "four PRs at
    `NEEDS CHANGE`" was one). Its *objective* survived;
    its *evidence* did not. Cheapest: land this revision before dispatch; keep row state out of this file
    where a pointer will do (applied here — **Critical path** and **Blocked** cite rows rather than
    restate them); treat the provenance line as a check the dispatcher can run in one command.
13. **The loop's documents drift.** The OQ register is a subset of the live OQs; `ARCHITECTURE.md`'s role
    table names six agents and superseded models. Cheapest: one dispatch pass (order 6 of the seeds).
14. **A new capability is a new wall hole.** `ARCHITECTURE.md` records that the shell denies are
    text-pattern mitigations and the physical `.recon-cache` separation has not landed. Any tool a role
    gains is a read surface. Cheapest: add no tool to `builder`, `verifier` or `recon-sweep`; keep browser
    and desktop surfaces with the operator; **skills enter as instructions by pointer, never as tool
    grants** (see **Capability review**).
15. **A round aimed at an unscheduled resource.** The objective is one session long. If the operator's
    calendar does not open, orders 2–3 idle — but the round is not empty: orders 1, 4, 5 and 6 are
    device-free and ordered so they still land. What must not happen is the reverse: device-free work
    presented as if it were the objective, which would make a slipped room look like a completed round.

**Retired this revision:** rev 24's risk 5 (unmerged measurements — `#366`/`#375`/`#387` landed); its risk
6's drift figure (0 live sessions drift now, from 19); rev 24's risk 16 (merged into 5); and the note that
`#81`/`#93` stood in contradiction (`#93` landed).

## Capability review — skills, and the wall

The operator asked which of the available skills would help this swarm. There is **no `skills/` directory in
this repository** (checked: none at the root, under `.opencode/`, or in the agent config), so this reviews
the catalog the harness exposes against this loop's roles, not a repo artifact. The verdict is shaped by one
rule: **a skill is instructions plus tools, and a role's permission table is the wall.** A skill can hand an
agent a surface its table withholds.

| Skill | Verdict for this loop | Why |
| --- | --- | --- |
| `reverse-engineer-anything` (REA) | **Already embodied — do not adopt as a skill** | The swarm's REA is the `rea_*` MCP, with `recon` the only launcher and `recon-sweep` read-only (`ARCHITECTURE.md`, **The walls**). A generic "decompile and reconstruct" skill is *weaker* than the agent files' rules, and a second statement of the same rules is the drift the E1/H2 work exists to prevent. Recon-side only, never builder. |
| `built-in-browser` / `chrome-browser` | **Useful, operator-side only** | It closes a real gap: `#112`'s HUD `data`/`render` fps have no server endpoint, and `#76`/`#141`'s UI halves need the page driven. Device-free against the synthetic server. But a browser is an unwalled read surface (`file://` reaches `.recon-cache`; page source and screenshots capture anything) and it carries persistent sign-ins. Never grant it to `builder`, `verifier` or `recon-sweep`; never let a browser observation stand as a verdict — it is a record's input. |
| `computer-use` | **Not for an agent — the operator's hands** | The vendor app is a sanctioned oracle for `#126`/`#159`'s sequential run, but desktop control is a screenshot surface on the same machine as the vendor binary and it automates irreversible UI. Fine for a human-attended Claude Code session on the scanner box; not a swarm capability. |
| `docs` / `docx` / `pptx` / `pdf` / `xlsx` / `google-workspace` | **Decline for anything the gate reads** | The repo's evidence is markdown records, raw logs, and the CSV/SVG `#152` and `#159` already produce. `verify-pr.py` cannot check a binary document, so a `.docx` or `.xlsx` under `tests/results/**` would be evidence outside the gate — the failure PR #392 already demonstrates in a cheaper form. Out-of-repo operator reports only. |
| `opencode` | **Useful, dispatch/operator side** | It documents agents, permissions, models, MCP servers and sessions — the machinery behind `#104`/`#109` and the agent-structure refactor. Read-only in effect: nothing it says widens a wall. |
| `skill-creator` | **Defer — only if it replaces, never adds** | The loop already carries its rules in six agent files, and measured duplication is a known problem (the Secrets block: 28 lines × 6 copies). A skill such as "write a hardware record" is worth building only as the *single* home the agent files then point at. That is an E-refactor decision, not this round's. |
| `deep-research` | **Low value now** | The open questions are answerable from captures on disk (`#144`, P9) or from the device (`#132`), not from the literature. If it is used at all — Phase C library choices — its output is a recon-side finding, and vendor documentation is not a substitute for the binary. |
| `report` | **Narrow fit** | Only for OpenCode's own defects (session vanish, model drift). Machinery hygiene, not recomp work. |
| `learn` / `morning` / `import-memory` | **No fit** | None maps to a role in this loop. |

**The rule to carry forward.** No capability is added to `builder`, `verifier` or `recon-sweep` — those are
the roles the clean-room claim rests on, and `ARCHITECTURE.md` already records that the shell denies are
mitigations while the physical `.recon-cache` separation has not landed (risk 15). Browser and desktop
surfaces stay with the operator, pointed at loopback or the vendor app, and their output enters the loop as
a record's input, never as a verdict.

### The operator's candidate set, 2026-10-20

A directory of sixteen candidate skills (fifteen distinct — `property-testing` and `property-testing 2`
are duplicates) was offered for evaluation, together with a copy of the live agent files. The verdicts
below are against **those files**, not against the skills in the abstract: the question is whether a
candidate adds a discipline the role is missing, or restates one that already has a home.

| Candidate | Verdict | Why, against `builder.md` / `verifier.md` / `recon.md` |
| --- | --- | --- |
| `error-handling` (64 L) | **Adopt — builder; verifier as a lens** | Fills a real gap: `builder.md` carries no error discipline, and the loop keeps paying for its absence in different clothes — a silent hang (`#106`), a skip recorded as a failure (`#130`), a daemon rejection visible only in a log line (`#157`), an unbounded vendor call (`#89`). Take the *classification* (precondition / recoverable / fatal / user-facing) and the boundary rule. The homes for statuses do not move: `#69`'s exit map and `#130`'s precondition contract. Instructions only. |
| `refactoring` (66 L) | **Adopt — builder** | The repo has already named the seams (`S1`/`C1`/`S2`/`S3`, `SCALE-ARCHITECTURE.md` §4–5) and measured the monoliths (`#148`); what it lacks is the discipline that makes a split reviewable — a characterization test first, one behaviour-preserving commit, no mixed rewrite. Its equivalence gate must **point at** `perf-budget.py` + `ctest` + the byte-identical wire, not invent one. Instructions only. |
| `property-testing` (+ its duplicate) | **Defer; delete the copy** | Narrow but real fit: the msgpack packer's round-trip, `frame_bins`' reassembly, `frame_decode`'s band arithmetic. It names no C++ library, and adding one (RapidCheck) is a supply-chain and licence decision the clean-room posture makes expensive. Write the invariant by hand in `ctest` until a codec row asks for more. |
| `load-testing` (69 L) | **Decline as written** | Its harnesses are k6/Locust/Artillery — the wrong surface for a WS+msgpack port. The repo already owns the equivalent (`scripts/perf-budget.py`, `#366`'s 10/24/50 Hz harness, `#142`'s frame-age probe), so adopting it creates a **second** load harness, the duplication the one-home rule forbids. Its one transferable idea (soak/spike) is already `#151`'s. |
| `memory-leaks` (59 L) | **Decline as written** | Its tools are browser/Node/Python/JVM heaps; our surfaces are C++ borrowed buffers, the synchronous WS send, and the browser's accumulated point buffer (`#154` owns the cap). The measured method is `#147`'s (`vmmap` + `sample` + the 0-allocation invariant in `PERFORMANCE-CONTRACT.md`). A skill naming the wrong tools sends the next investigation to the wrong place. |
| `code-review` (27 L) | **Decline — superseded by the verifier** | `verifier.md` is stricter and walled: read-only, one anchored verdict per comment, identity hashing, "the spec silent ⇒ a finding, not an approval". A generic checklist is a weaker second home. |
| `git-workflow` (29 L) | **Decline — settled and mechanized** | One worktree per role, PR + merge commit, a verdict bound to a head sha, a pre-commit whitespace hook, `safe-rebase`/`pr-integrate`/`pr-rebase`. Generic branching advice invites drift on rules the loop enforces with scripts. |
| `github-workflows` (29 L) | **Decline** | `.github/**` is agent-tier and bespoke (`verify-pr` + `verdict-gate` + `pull_request_target`); the gate *is* the wall's enforcement. Generic Actions advice is drift on it. |
| `api-design` (27 L) | **Decline** | There is no REST surface: the contract is a C ABI (the header) and a WS msgpack protocol (`frame_protocol.h` + findings). The risk is HTTP semantics invented for a binary protocol. |
| `caching-strategies` (30 L) | **Decline** | "Cache" here is a latest-frame slot and a decimation/tier policy (`SCALE-ARCHITECTURE.md` §4), not HTTP/CDN/Redis. Naming the wrong layer is worse than silence. |
| `component-patterns` (28 L) | **Decline** | `ui/` is four hand-written JS files plus a WebGPU/WebGL2 renderer — no component framework to design for. |
| `testing-library` (55 L) | **Decline** | Needs React/Vue/Svelte/DOM components; the UI tests are node scripts (`ui/frame_age_test.js`). |
| `eslint-config` (28 L) | **Decline** | No ESLint config and no `package.json` in the repo; a JS toolchain is a supply-chain change with no gate to consume it. |
| `gitops` (30 L) | **Decline** | No Kubernetes. |
| `mcp-development` (30 L) | **Decline** | The only MCP is the global `rea` server, outside the repo; a new MCP surface is a new tool surface — a new wall hole (risk 14). |

**The pattern is not that these are bad skills; it is that twelve of them describe a different product.**
Two describe disciplines this loop keeps relearning in prose — error classification at boundaries, and
behaviour-preserving change with a test that pins the behaviour first — which is what makes them worth
adopting. Adoption is **by pointer**: one line in `builder.md` (and a lens line in `verifier.md`) naming
the skill, with the repo's own homes for statuses, budgets and the wire named *inside* that line. A skill
that carried tools would widen a wall; these carry instructions only, and the rule above still holds — no
capability is added to `builder`, `verifier` or `recon-sweep`. The files are outside the repo, so **the
operator installs and the dispatcher records**; this table is the durable record.

## Next round, as work-order seeds

For the dispatcher to expand — not dispatches in themselves. Ordered so each item's input exists when it
starts.

1. **Room pre-flight (builder + dispatch, no device).** **`P12`** (the record factory: a generated raw-log
   citation line with a computed sha, and a template that requires it), `#392` repaired or closed, and the
   binary check (`strings` the built and bundled worker for the suffixed method name — `#107`'s
   stale-copy lesson). **Acceptance:** a wrong hand-typed hash is the only way left to fail `archives`.
   **Then freeze builds for the session.** **Wall:** `scripts/**`, `tests/results/_TEMPLATE.md`.
2. **The session's floor (hardware + operator).** Replay, then live IR, in that order; one record each,
   each naming its port, its frame count, its reply rate and its binary sha. Stop rule per the objective.
   **Wall:** `tests/results/**`. **No build lands between this order and order 3.**
3. **The session's stretch (hardware + operator).** Live depth, then `#71`'s A/B. Skip the depth run if the
   IR run failed at the first RPC; keep the A/B regardless. **Wall:** `tests/results/**`.
4. **Beside the room (recon, no device).** `#144`, then **`P9`**. One row per session. **Wall:**
   `docs/recon/**`.
5. **Capacity (builder, no device, parallel).** **`P11`** (`S1`), **`P10`** (the counters' accessor),
   `#149`'s reader, `#154`. Each frees one row and feeds the perf contract. **Wall:** `engine/**`,
   `tools/scan_server/**`.
6. **Board and trust pass (dispatch).** `#93` stays `HW` (its observation rides the A/B run); close or re-point `#134`, `#155`, `#138`; file
   `P9`/`P10`/`P11`/`P12` and the parked-OQ row; state the OQ-register rule (orphan 6); correct
   `ARCHITECTURE.md`'s role table. **Wall:** `PLAN.md`, `docs/recomp/ARCHITECTURE.md`.
7. **Skills, by pointer (operator installs; dispatch records).** Two adopted (`error-handling`,
   `refactoring`), one deferred with its library question stated (`property-testing`), twelve declined;
   one pointer line each in the agent files, no tool grants. **Wall:** the agent files (outside the repo).
8. **Operator items — scheduled, not worked.** The consolidated capture session (`#145` + `#150`'s three);
   `#126`'s known scene (after orphan 3); `#149`'s archive format; the target rate, after the census.

**Explicitly not this round:** `#126`/`#159`'s curve, `#145`/`#146`/`#150`'s captures (different setups),
`#99`/`#100` (the destination), `#113` and the seams behind it, and `#149`'s write path (the format
decision). The dispatcher can refuse them without re-deriving why.

**Ordering note.** Order 1 precedes order 2 or the session's output is uncitable. Orders 4 and 5 run
throughout — they need no device and do not compete for the room. Order 3 starts only after order 2's
floor. Order 6 can land any time. **No build lands mid-session** (risk 4).

## Superseded plans

- **2026-10-20 rev 24 → rev 25 — same objective, tightened: the session's floor decides the round.** Rev 24
  fixed the batch at four runs and put the evidence chain ahead of the room. In the hours since,
  `#366`/`#375`/`#387` and `#93` landed, so the evidence chain is one PR (`#392`), and four of rev 24's
  board claims were stale (`#112`, `#109` `DONE`; `#93` landed but reading `BUILD`; "four PRs at
  `NEEDS CHANGE`"). Reading the 2026-10-20 records already on `main` supplied the fact that re-ranks the
  session: both live runs died at the **first RPC**, and `#157`'s fix landed *after* them, so the next
  session's value is concentrated in its first two minutes. Rev 25 splits the batch into a **floor**
  (replay + one 30 s IR run) and a **stretch**, adds a stop rule (a failed floor cancels the depth run but
  not the A/B — a different binary path), turns the record factory into row **`P12`**, consolidates `#145`
  with `#150`'s three captures into one operator sitting, and records the verdict on the operator's sixteen
  candidate skills. Rev 24's own post-rebase note (2026-10-20) recorded the same three merges and that
  `#109` was already `DONE` when rev 24 was written; it is folded here rather than kept as a second
  header. **Reversal reason:** new landings plus the prior session's own records — not a change of target.

- **2026-10-20 rev 23 → rev 24 — the room round: the code prerequisite landed, and the record became the
  risk.** Rev 23 set the objective as "fix `#157` so the live half can run". Three things landed after it
  was written: `#157` (PR #393), `#158` (PR #382), and the board moves through `#159` (PR #397). So the
  round's first order completed itself, and the binding constraints moved: (a) the **room** is now the
  only thing between the pivot and `#105`/`#98`/`#132`/`#127`, and (b) the **record** — PR #392 fails
  `archives` and `record-provenance`, so the 2026-10-20 hardware evidence cannot be cited. Rev 24 moves
  the objective from "fix the defect" to "spend the session once and make it citable", fixes the batch at
  **four runs in a fixed order** (because `#157`'s note says the binary opens one interface per run), and
  puts the evidence chain ahead of the room. **Reversal reason:** the plan's premise (a code blocker)
  ceased to exist; the risk that replaced it is measured, not anticipated.

- **2026-10-20 rev 22 → rev 23 — the live-path round: one defect outranks a whole optimisation.** Rev 22
  made deadline-paced publishing the objective, on `#151`'s device-free finding that the run-loop
  overshoot is a fixed rate tax. Three things landed after it was written and re-ranked the work: (a)
  `#141`'s **live** split (record in PR #376) put the per-tick exposure re-assert above every other
  non-wait cost — a live cost the synthetic harness could not see; (b) `#153` landed **after** that
  measurement and removes it; (c) `#151`'s own addendum records that a deadline wait removes drift but not
  the platform's wake latency, so the pacing change is necessary but not sufficient. In the same session
  the live client failed at its first RPC — `#157`, a method-name defect — which gates five rows and the
  pivot's next link. Rev 23 therefore moves the objective from "make the rate a designed number" to "make
  the next room session decisive": fix `#157`, split the batch into a replay half that needs no fix and a
  live half that does, and confirm the live cost model (`#153`/`#141`) on one binary. **Reversal reason:**
  the live measurement contradicted the instrument the prior objective rested on, and the pivot's blocker
  turned out to be a one-file code defect rather than a scheduling problem. The pacing work is not
  dropped — `#151` lands as `#113`'s method, and the 50–60 Hz question becomes the operator's target-rate
  decision, with the structural seams (`C1`/`S3`) named as the path if the answer is "higher".

- **2026-10-20 rev 21 → rev 22 — the pacing round, and a provenance correction.** Rev 21 aimed at a
  decode-grade capture corpus (`#127` → `#132`). Its own proposals were then filed as rows `#140`–`#146`
  and `#140` landed, so the capture half is *tooling complete and room-bound* — the round would have
  idled on the operator's calendar. In the same day the board measured the live path instead
  (`#147`'s runtime baseline, `#151`'s rate harness) and the answer re-ranked the work: the publish
  rate is a **pacing artifact**, not a throughput limit, and one device-free change frees five rows.
  Rev 22 takes that as its objective, keeps the pivot batch as a precedence-first order rather than a
  round objective, sweeps an OQ list that had drifted in both directions, and records the
  API/dylib/daemon distinction the row list kept assuming. **Reversal reason:** new measurements plus
  the observation that the prior objective's remaining step is operator-scheduled, not code.

- **2026-10-19 rev 20 → rev 21 — capture and instrument, and the frame path becomes their first
  consumer.** Rev 20 aimed at `#105` as "get frames out"; the board then answered two links of it
  (`#122`'s envelope unwrap, `#131`'s named 11-element array), so the remaining question is the
  band rule and the pixel scale — an **evidence** problem. The operator's scope input (telemetry on
  performance-sensitive code; capture every hardware artifact even before its decoder exists) is
  adopted with one amendment: name the surfaces and the schema, do not build a framework ahead of
  its consumers. `#132` is promoted as the round's first run, with a live recorder (P1) so the next
  decode needs no new capture. Reversal reason: the board's own evidence plus an operator priority
  input, not a change of target.

- **2026-10-19 rev 19 → rev 20 — the agent-structure proposal (E1–E10), parked behind the pivot.**
  Rev 20 recorded the dispatcher's four answers and the measured drift in the six agent files, and
  sequenced only E5 now. It did not re-rank the product work; its objective stayed rev 19's `#105`.
  Superseded because the product evidence moved (see rev 21), not because the harness proposal was
  wrong — E1–E4/E7–E10 still park.

- **2026-10-19 rev 18 → rev 19 — the pivot's first live run re-ranks the board.** Phase B's code
  landed (`#98`) and its first session pulled **zero frames**, so `#105` now gates `#98`'s acceptance
  and therefore C and D. Rev 19 puts `#105` first, **reframes the capture work from re-derivation to
  verbatim replay**, promotes `#101` from parked to on-path, and sweeps the night's moves — two of
  which were in open PRs rather than on `main` when the sweep ran. It also records the session-model
  mechanism under `H2`. Reversal reason: the pivot's own evidence, not a change of target.

- **2026-10-19 rev 17 → rev 18 — README close-out, sweep, and one new open question.** The staged
  README text was applied verbatim as #252 and the file gained an owner, so the staging block was
  retired to a short record; `W2`'s enumeration-based builder wall was kept as a standing finding;
  `#96` closing moved the critical path's head to `#98`; and reading the SDK's own docs narrowed the
  licensing erratum and raised the Raptor Pro's USB protocol family as a new unknown for `#100`.

- **2026-10-19 rev 16 → rev 17 — the pivot.** The operator set **full host-side independence from
  Creality Scan** as the priority target, so the objective becomes the Phase A–D chain
  (`FULL-INDEPENDENCE.md` §6) and everything else is re-ranked against it: the `G`-goals park
  *causally* — Phase C replaces the pipeline they patch — and firmware is explicitly off the path.
  Rev 16's objective landed (`G3`'s build row, `#91`/`#92`; `B1` fixed, with `#90` guarding it). The
  sweep corrected three row claims. Reversal reason: an operator priority change, not a drift.

- **2026-10-18 rev 15 → rev 16 — sweep, not a reversal.** Rev 15's walls work landed and was probed,
  so the objective returns to the reconstruction workstream: `G3`'s build row is the last unstarted
  capability. Rev 16 adds the close-out (what landed, what remains of the change-set) and leads with
  a hard blocker the audit could not have caught — `openscanner/builder` does not load, because it is
  the only agent with incomplete permission entries. Every row claim was re-read against the board
  per **`H3`**; the six OQ rows (`#83`–`#88`) and the `G3` recon chain (`#78`–`#82`) are new since
  rev 15.

- **2026-10-18 rev 14 → rev 15 — the objective moves to the walls, and two outside reports are
  consolidated.** An audit of the loaded agent configuration found the clean-room walls partly
  aspirational — verified here against `opencode-cli debug agents`, not the `.md` files — so the
  walls outrank the feature rows. Rev 15 folds that audit and an optimization report into one
  change-set, states **ten amendments with reasoning** (`A1`–`A10`), and reorders the proposals
  (shadow agents first, §2.1 demoted, §5.1's independence preserved). Reversal reason: a new input
  that changes what the most important work is, not a change of direction in the features.
- **2026-10-18 rev 13 → rev 14 — the objective moves to `G1`'s bind half, plus `H4` and a full row
  re-read.** The app shell and its run-discovered defects (`#72`/`#73`/`#74`) all landed while this
  revision was being written, so the only unstarted capability left is `#71`. Rev 14 adds **`H4`** —
  the measured compaction cycles, the `opencode.json` decision, the `verify-pr.py` tier fix, and the
  cycle-3 test — and replaces Risk 8's prose with a pointer to it. Every row claim was re-read
  against the board first (`H3`); this took **three** passes, because the board moved 61 commits and
  then 6 more during the write. Reversal reason: the work landed faster than the plan could name it.
- **2026-10-18 rev 12 → rev 13 — a correction, not a direction change.** Rev 12 claimed three
  times that `G1`'s adoption row had never been filed. It had: **`#59`, `DONE`** (`e842e71`, PR
  #169). Rev 13 corrects that, records the verdict (**bind the toggles; reimplement the
  decision**), and adds **`H3`** — *grep the board before asserting a row's state* — the guard
  that would have caught the planner's own error. Also folded in: the current row set (`A2`
  `#63`, `A3` `#65`, `A4` `#66`), `#64` `DONE` (the voxel claim did not survive), and `#67` in
  flight. Reversal reason: the dispatcher checked the board against this file's claim, which is
  the check the planner should have run.
- **2026-10-18 rev 11 → rev 12 — the `A2` decision, recorded as binding.** Rev 11's aim (the app
  workstream) stands, and its first row (`A1`) landed. Rev 12 records the operator's answer on
  the bundle — **no SDK; require the user's install** — turns it into three concrete
  requirements, files `A2`–`A4`, marks `H1` landed (79,660 → 17,758 bytes), and sweeps the board
  after the stall-test and app rows. It also stops re-proposing `G1` as a track and states the
  debt plainly instead. Reversal reason: the operator answered the open question.
- **2026-10-18 rev 10 → rev 11 — additive: two new workstreams, no objective change.** Rev 10's
  aim (`G1`) stands. Rev 11 adds the app workstream (`A1`–`A4`, with `A1` as its first row) and
  the loop-hygiene items (`H1` board compaction, `H2` the model-doc reconciliation), and folds
  `OQ-S6` into the acceptance run. Reversal reason: new operator inputs and a measurement of the
  dispatcher's own session — not a change of direction. Rebased onto rev 10 **after** the
  `#21` closure landed (a first attempt would have reverted it).
- **2026-10-18 rev 9 → rev 10 — objective change and a scope hand-off.** Rev 9's aim was
  `V1`, which landed with the rest of the viewer family; the capture/ABI phase is closed, so
  rev 10 moves the aim to the reconstruction workstream (`G1`). The Windows workstream is
  handed off by operator rule and removed from this file's tracking. Also swept: the planning
  sections, and `G6`, whose drain half recon has corrected out. Reversal reason: the work it
  planned is done, and a new input (the operator's machine rule) ended one workstream.
- **2026-10-18 rev 8 → rev 9 — additive, no objective change.** Added the Windows fork
  workstream and its two documents. Reversal reason: a new input, not a change of direction.
- **2026-10-18 rev 7 → rev 8 — refresh pass, no objective change.** Rev 7 restated loop state
  instead of pointing at it and the loop moved under it; rev 8 pointed at the rows and
  separated the landed decimation from the forward protocol design. Reversal reason: a
  verifier read of PR #97.
- **2026-10-18 rev 6 → rev 7 — objective change.** Rev 6's aim was "keep the live view live
  as the cloud grows"; `#32`/`#38` landed that and the operator opened the viewer direction.
- **2026-10-17 rev 5 → rev 6 — objective change.** Rev 5's aim was "make the live view stream
  real points"; `#25` landed and OQ-S5 resolved, so rev 6 moved the aim to *scaling* it.
- **2026-10-17 rev 2 — "Close the scan family's live-data loop: `#24` → `#25` → OQ-S5."**
  Superseded by rev 3, and fully landed: rev 2 treated `#25`'s getter-publish path as settled
  and rev 3 added the decision pass, which landed as findings §6.
- **2026-10-17 rev 1 — "Drain the hardware queue, and open the scan family in parallel."**
  Superseded by rev 2: the scan family was opened via `#23`, and the queue's most obvious item
  already had a passing run on `main`.
- **Landed elsewhere, no longer tracked here:** the Windows fork drafts (`WINDOWS-FORK.md`,
  `FORK-COORDINATION.md`) and the scan-viewer architecture doc
  (`docs/architecture/scan-viewer.md`) — each keeps one home.

---

## README correction — **applied and closed** (#252)

The text staged here was **applied verbatim** as **PR #252** and is on `main`: the layer table, the
repo-URL correction, the "no source to read" argument, the phase path, the firmware exclusion, and the
clean-room posture. **The routing instruction that stood here is retired** — it has done its job.

- **Owner fixed.** `README.md` is now in **dispatch's** allow list and the wall record in
  `ARCHITECTURE.md` is updated, so the file is no longer reachable only by accident.
- **The underlying error is closed.** The SDK-layer claim was verified against Orbbec's own GitHub
  org, and the corrected section shipped; the finding stands above.
- **The erratum landed.** `FULL-INDEPENDENCE.md` marks its "MIT-licensed public source" attributions
  unverified. The verification below **narrows** that erratum rather than contradicting it.

### What this did *not* settle

**W2's builder wall is still enumeration-based** (no `deny:*`), so unlisted paths stay editable — the
argument above stands unchanged, and **whether to restore `deny:*` is the operator's call**, not the
planner's and not the dispatcher's. The owner fix removes the *consequence* this README suffered; it
does not close the wall.

**And the README still says "MIT-licensed public-SDK headers"** in the section just applied — see the
sweep below for why that now needs one more pass.

---
