# Recomp roadmap

Forward strategy for the recon → build → hw → reconcile loop. Where
[`PLAN.md`](PLAN.md) is the live **queue** (one line per task, move the status as you
go), this file is the **plan**: the critical path, what is blocked and on what, the
risks, and the objective of the next round.

Owner: the `openscanner/planner` agent. The dispatcher consumes it and applies the row
moves it proposes. Statuses, claims and evidence live on the board — this file points at
them and never restates them.

_Last planned: 2026-10-18 (rev 11 — adds the app workstream A1–A4 alongside `G1`, and the loop
hygiene items H1–H2, the first of which is the board-compaction step the dispatcher owns. rev 10
moved the objective to the reconstruction workstream and handed off the Windows lane; rev 9
handed off the Windows drafts; rev 8 was the refresh pass; rev 7 added the viewer family;
rev 6/5/4/3/2/1 are superseded — see **Superseded plans**)._

## Objective for the next round

**Two tracks, deliberately — `G1` (open the reconstruction workstream) and `A1` (stand up the
app's transport foundation). They share a round because they share no resource: `G1` opens with
a recon row, `A1` is a builder row, and neither touches the serialized hardware room. `H1` — the
board compaction — is housekeeping the dispatcher owns and owes itself.**

**Track 1 — `G1`, live plane removal, starting with the adoption row that turns the corpus's
claim into a spec.** The board says the capture/ABI phase is over: 53 rows `DONE`, the viewer
family landed, the Windows lane handed off. What is left there is one acceptance run, and the
`#21` provenance hole is closed. Among the G-goals, `G1` is the corpus's own next step and the
operator's complaint, and the only one that ends in working software without needing another
goal first. Its aim this round is the *spec*, not the code — the corpus doc is frozen and its
ABI was never adopted into findings, so recon precedes build.

**Track 2 — `A1`, serve `ui/` from the capture process, so the client stops needing an external
static server.** The app family's only true blocker: the shell and everything after it are small
once the client has an origin the app owns. Cheap, builder-side, non-competing.

This is a deliberate exception to one-objective-per-round: two aims, two resources, no
contention. **If only one can run, `G1` is the aim and `A1` waits.**

## Critical path

```
G1 (live plane removal) ─► recon adoption row ─► build row (bind the ABI for A/B, or
                            reimplement RANSAC + signed-distance) ─► live per-frame filtering
A1 (serve ui/ from the process) ─► A2 (bundle + WKWebView) ─► A3 (lifecycle) ─► A4 (settings)
                                     └─ A2 gated on the operator's SDK-bundling call
H1 (board compaction) ─► the dispatcher's own per-round read drops ~10×  (self-serving, not blocking)

G2 (coverage heatmap) ─► build (prereq #40 landed) ─► G3 (owned tracking, per-pose reasons)
G4/G5 (denoise + fusion, the accuracy harness) ─► need a fusion feature to exist first

#56 · #41 · #53 (HW) ─► ONE acceptance run (the stall test + V5's visual + OQ-S6's tail) ─► three rows close
#58 (RECON, in flight) ─► OQ-S13/S14        #21 (closed 2026-10-18)
```

Ranked by what it frees, not by board order:

1. **`G1`.** Opens the reconstruction workstream, is the operator's stated pain, and needs one
   recon row to start. Nothing else unstarted frees as much.
2. **`A1`.** The app family's only blocker, and cheap: it gives the client an origin the app
   owns, which is what every later A-row assumes. Builder-side, so it does not contend with
   `G1`'s recon session.
3. **The acceptance run.** Not an aim, but the cheapest rows-cleared on the board: three `HW`
   rows (plus the folded `OQ-S6` observation) on one manual session. Hardware contends with
   nothing above.
4. **`H1`.** It frees no row, but it shrinks what the dispatcher itself must hold every round —
   see **Loop hygiene**. A dispatcher reading a 6 KB board makes better calls than one reading
   a 67 KB one, so this pays back into every item above it.
5. **`G2`.** Its prerequisite landed with `#40`, and it is the only G-goal that unblocks
   another (`G3` depends on its feedback layer).
6. **`G6`.** The corpus ranked "fix our own SDK usage" first; §1.5's drain half is now
   **corrected by recon** (see **Orphaned work**), so what remains is the voxel-size claim —
   live, but unverified, and therefore an adoption row rather than a build row.

## Ready now

| Row | Role | What it needs |
| --- | --- | --- |
| `A1` (unfiled) | builder | Serve `ui/` from the capture process — one origin, no external static server. The app family's first row. |
| `H1` | dispatcher | Compact the board: archive each `DONE` row's history to `docs/recomp/log/` and leave a pointer. See **Loop hygiene**. |
| the acceptance run | hardware | One session; the spec is `tests/hardware/plan56-stall-and-v5-visual.md`. Closes `#56`, `#41`, `#53` — and carries the `OQ-S6` tail observation. |
| `G1`'s adoption row (unfiled) | recon | Turn the corpus's plane-removal ABI + decision-fn into a findings doc, and answer the re-homing question below. |
| `G2`'s build row (unfiled) | builder | The under-covered point set from the vendor's own per-point result, painted on the live cloud. `#40` landed. |
| `#58` | recon | In flight. The `OQ-S13`/`S14` residues (`#55`'s). |
| OQ-7b's row (unfiled) | hardware | The listener-less with-device run; `#12`'s note waits on it. |
| `#21` | **closed** | Agent-tier + `record-provenance` landed 2026-10-18; nothing outstanding. |

## Blocked, and on what

| Row | Blocked on |
| --- | --- |
| `G3` | `G2` — the feedback layer it needs to display per-pose reasons. |
| `G4`, `G5` | A fusion feature actually being planned; a denoise/fusion harness measures something that does not exist yet. |
| `G1`'s build row | `G1`'s adoption row — recon before build, per the corpus rule. |
| `A2`, `A3`, `A4` | `A1` for the origin; and **`A2` additionally on the operator's SDK-bundling call** — it decides what the bundle may contain. |
| the Windows lane | Nothing. Handed off by operator rule — see **Windows fork**. |
| `H1` | Nothing — the dispatcher owns both the board and `docs/recomp/log/`, so this is self-serve. |
| `#21` | **Closed** (2026-10-18) — the remedy is on main; the decision's authority is recorded on the row. |

## Orphaned open questions

An OQ with no task row is unowned work. The previous sweep tracked a stale subset; this one
reads every findings file's open list.

**Dead — closed, keep off the list:**

- **OQ-1** — resolved by `#50`; **OQ-5** by `#47`. Both are answered in their findings.
- **OQ-4** — closed by `#52`.
- **OQ-S4** — deferred to the marker pass by decision; not yet live.

**Live, unowned — each needs a row or an explicit park:**

- **OQ-S12** — the pose's *publication trigger* is not in the header subset, and the findings
  name it as the thing the viewer's `pose` message depends on. **But `V4` shipped and was
  HW-confirmed with the getter**, so this is a gap in the trigger's semantics, not a hole in
  the pose channel. Decide: declare the export, or document the getter as the live channel.
- **OQ-S2** — does pause/stop publish temp→show? Not load-bearing (the live path is decided
  and landed), so: **park with a trigger** — adopt into the next scan-family recon row.
- **OQ-S3** — the `+0x2e0`/`+0x320` lock asymmetry on the cloud we read every tick. A latent
  race; never observed. **Live, low priority** — check before rowing separately.
- **OQ-S13**, **OQ-S14** — `#58`, in flight.
- **OQ-S6** — the stop tail vs the watchdog. `#43` landed the watchdog side; **verify** this
  is closed before rowing it.
- **OQ-S7**, **OQ-S9**, **OQ-S10**, **OQ-S11** — answered with residuals, none load-bearing.
  **Park with triggers**, listed in their findings.
- **OQ-7b** — live; a hardware observation. Propose a row (it is the only `#12`-owed item).
- **OQ-7c** — static-only and low-value (which VID owns `0x1B20`). **Park.**
- **OQ-7d** — the one `#52` closed the parent of. **Verify** and close.

## Orphaned work — the corpus's `G6`, corrected

The sweep found one corpus claim that recon has since **refuted in part**, and it changes a
goal rather than adding a row:

- **`G6`'s "drain the data listener" half is no longer soundly motivated.** The findings
  correct §1.5's causal story ("we never drain → the queues fill"): the queues overflow in
  the vendor app too, and the story is in tension with the same corpus's §1.1. So this is not
  a defect to fix on the corpus's reasoning — **do not row it on that basis**.
- **`G6`'s voxel-size half stands as a claim, unverified.** `tools/scan_server` still sets
  the values the corpus compares against the app's, so the comparison is still live — but it
  is a corpus claim, so it needs adoption (recon) before any build row.

## Goals carried forward from the research corpus (captured once)

`docs/research/**` is **frozen** — a historical artifact: no in-place maintenance, no adoption
into findings. Its vendor-fact claims are partly superseded by later recon, so any goal picked
up re-derives the facts it needs through recon like any other target. Captured so they are
tracked and not lost.

| # | Goal | Needs first | State |
| --- | --- | --- | --- |
| **G1** | Live plane removal for **resource management** (cut data per frame during capture), not a post-scan cosmetic cut | a recon adoption row | **the objective** |
| **G2** | Coverage heatmap: replace the one-scalar "insufficient data" verdict with a spatial under-covered set | nothing — `#40` landed | ready to row |
| **G3** | Owned tracking (marker detection + bundle adjustment) with per-pose rejection reasons | `G2`'s feedback layer | blocked |
| **G4** | Own denoising + fusion pipeline, Open3D as the MIT anchor | a fusion feature being planned | parked |
| **G5** | Fusion-accuracy evaluation harness | `G4` | parked with `G4` |
| **G6** | Fix our own SDK usage: *drain the listener* + align voxel sizes | — | **re-scoped: the drain half is corrected out; the voxel half needs adoption** |
| **G7** | Keep laser extraction + calibration behind the SDK until the "drop OBScan" decision | that decision (`README.md`) | standing |

Two corpus design constraints bind any row above: keep capture decoupled from fusion via a
bounded queue, and keep the frame-acceptance gate and the bundle adjuster as **two separate
systems**.

**The re-homing question, for `G1`'s adoption row** (this is the part that must be answered
before a build row): the corpus attributes the plane toggles to `OBScanDataManagement`, while
current recon homes the data-management subobject on `OBScanBase`. Same object, possibly a
re-homing — **verify, do not assume.** The adoption row should also settle **bind vs
reimplement**: the corpus gives a reimplement verdict for the filter and keeps the vendor's
four exports as an A/B reference.

## App workstream (A1–A4) — a basic macOS app, captured 2026-10-18

The client is already built; what is missing is **a shell and a transport**. There is **no
packaging anywhere** — every target is a plain executable, with no `MACOSX_BUNDLE` and no
`Info.plist` — and the UI is not served by the app: `README.md` requires a separate
`python3 -m http.server`, and `file://` is explicitly unreliable there.
`docs/architecture/scan-viewer.md` §2 already named the destination (a web client plus a
**thin native shell**), so this is the next step of a decided direction, not a new one. The
A-labels are the planner's; the dispatcher files the rows.

| # | Foundation | Board row | State |
| --- | --- | --- | --- |
| **A1** | Serve `ui/` from the capture process — one origin, no external static server | none — **file it** | **the first row**; nothing blocks it |
| **A2** | The `.app` bundle + a WKWebView window (`MACOSX_BUNDLE`, `Info.plist`, icon) | none | **blocked on the operator's SDK-bundling call** |
| **A3** | Lifecycle: spawn and supervise the worker, surface a crash, and make **quit = graceful `pause`/`stop`/`destroy`** | none | after `A2` |
| **A4** | Settings and failure paths — missing-SDK dialog, port, session length | none | after `A2` |

**Toolchain: nothing new is required.** This machine has CommandLineTools with no Xcode.app —
Apple clang, `codesign`, `notarytool`, `iconutil` and CMake are all present. The two absent
tools, `actool` and `ibtool`, compile asset catalogs and storyboards; a **programmatic
ObjC++ shell** (no storyboard, `.icns` via `iconutil`) avoids both. Full Xcode becomes
necessary only for storyboards, asset catalogs, Instruments, or App Store submission.

**The constraint on `A2`, and the one question for the operator.** The bundle must **not**
contain the vendor's dylib: redistributing OEM code inside an MIT project is a licensing
problem, and `.recon-cache/` is kept out of git precisely to avoid it. The safe default is
therefore *require the scanner app installed, resolve its path, and fail with a dialog* —
which is what the loader already does. `A2` gets written only once that is confirmed, because
it decides what the bundle may contain.

**Rejected shapes**, for the record: a native SwiftUI + Metal rewrite (forks the UI away from
the portable client and from `V5`); Tauri (a new toolchain — revisit only if the Windows lane
wants to share a shell); and "package the binary and open a browser tab" (not an app).

**Why this is a macOS row and not a Windows one:** it is a macOS-only deliverable, so it sits
inside the machines-split-by-fork rule. The Windows lane wraps the same client in its own shell.

## Loop hygiene (H1–H2)

Housekeeping on the loop's own machinery, not on the product. Both items came from measuring
the dispatcher session itself on 2026-10-18 — the same reason the old inline `## Log` section
was split into `docs/recomp/log/`.

### H1 — Board compaction (the dispatcher's own read)

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

### H2 — Reconcile the dispatcher's documented model

`ARCHITECTURE.md` documents the dispatcher's model as `opencode/nemotron-3-ultra-free`,
justified by a long-context tier. The live dispatch session is running
`deepseek-v4.1-flash#max`. One of the two is stale. It matters more than a typo because the
original choice was justified *by context capacity* — the same property `H1` protects. Fix
the doc or the config, and record the reason where the choice is recorded.

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

1. **The seam between binding and building is unproven.** Every G-goal above the seam is
   unexercised: the project has bound an ABI and won a live view, but has never run its own
   reconstruction math on live data. `G1`'s adoption row is where that gets tested cheaply,
   before a build row is committed.
2. **What the app bundle may contain is undecided, and it gates `A2`.** Bundling the vendor's
   dylib would redistribute OEM code inside an MIT project; `.recon-cache/` is kept out of git
   to avoid exactly that. Safe default: require the scanner app installed and resolve its path,
   failing with a dialog. Cheapest retirement: the operator's one-line answer.
3. **Evidence provenance (`#21`) — closed 2026-10-18, one residual.** Added records now need a
   verifier verdict (agent-tier) and a non-`.md` companion raw artifact in the same PR. The
   residual is a *plausible-but-false* record surviving a verdict; the only thing that retires
   it is a spot re-run against the artifact, so treat a record as evidence of a run, not proof.
4. **A second evidence corpus outside the gate.** `logs/**` is tracked and is the evidence
   base for the corpus's §1, but it is neither `tests/results/**` nor `docs/recon/**`, so the
   `run-records` check and the tier rules do not reach it. Cheapest retirement: an operator
   decision on whether it stays.
5. **One serialized hardware resource.** The acceptance run and `OQ-7b` queue behind one
   manual session. Batch per session, cheapest-first.
6. **Recon step budget.** `recon` is capped at `steps: 40`, one board row per session. `G1`'s
   adoption, `#58`, and any `OQ-S12` row are each a session — an order that also asks for
   adjacent context comes back empty.
7. **Verification debt.** Claims tagged `[inferred]` that code depends on: the parked
   `OQ-S7/S9/S10/S11` residuals, `OQ-S2`/`S3`, and the tips/control rows `#9` left `[inferred]`.
8. **The dispatcher's own context, and it grows monotonically.** Measured 2026-10-18: one
   21-hour session, 303 messages, 6.37M fresh input, 484M cached read — against a board that is
   91% archive. The failure mode is judgment drift on stale history, not a crash, and this repo
   has already paid for it once (rev 7 → rev 8). Cheapest retirement: `H1`, then re-measure.

## Next round, as work-order seeds

For the dispatcher to expand — not dispatches in themselves.

1. **`H1`, board compaction** → dispatcher: apply the archival rule at the `→ DONE`
   transition, bring `PLAN.md` under 20 KB, and record the before/after bytes in the PR. Self-
   serve — `PLAN.md` and `docs/recomp/log/**` are already dispatch's, and no new agent is
   involved. Do this one **first**: it shrinks what every later item is read against.
2. **`G1`'s adoption row** → recon: adopt the corpus's plane-removal ABI + decision-fn into a
   findings doc; answer the **re-homing question** and give a **bind vs reimplement** verdict.
   **Wall:** recon owns the header and `docs/recon/**`; the row's deliverable is a spec, no
   code. One target, one session.
3. **`A1`** → builder: serve `ui/` from the capture process so the client has an origin the app
   owns. One origin, no external static server, and the `python3` step leaves the README.
   **Wall:** builder edits `engine/`, `tools/`; no header change, so nothing here needs recon.
4. **The acceptance run** → hardware: one session, the combined stall/V5 spec. Closes `#56`,
   `#41`, `#53`. Add **one observation for `OQ-S6`** — the stop tail against the watchdog on
   the scale case, which `#43`'s teardown change post-dates; it retires or reopens that OQ for
   the cost of reading a number in a session that is already running. Archive the raw log and
   any crash report per the record rules. **Wall:** hardware writes only `tests/results/**`.
5. **`G2`'s build row** → builder: the under-covered point set painted on the live cloud, from
   the vendor's own per-point result. **Wall:** builder edits `ui/`, `tools/`; the getters'
   semantics stay recon's.
6. **`OQ-S12`'s row** → recon: decide declare-the-export vs document-the-getter. **Wall:**
   recon owns the header.
7. **`OQ-7b`'s row** → hardware: the listener-less with-device run, batched with (4).

Also ready, non-competing: `#58` (recon, in flight), the `G6` voxel-size adoption if a recon
session frees up, **`H2`** (the model-doc reconciliation, one line), and **`A2` once the
operator answers the SDK-bundling question**.

## Superseded plans

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
