# Recomp roadmap

Forward strategy for the recon → build → hw → reconcile loop. Where
[`PLAN.md`](PLAN.md) is the live **queue** (one line per task, move the status as you
go), this file is the **plan**: the critical path, what is blocked and on what, the
risks, and the objective of the next round.

Owner: the `openscanner/planner` agent. The dispatcher consumes it and applies the row
moves it proposes. Statuses, claims and evidence live on the board — this file points at
them and never restates them.

_Last planned: 2026-10-18 (rev 10 — the capture/ABI phase is closed, so the objective moves
to the reconstruction workstream; the Windows lane is handed off; the planning sections are
swept against the board at `4067ece`. rev 9 handed off the Windows drafts; rev 8 was the
refresh pass; rev 7 added the viewer family; rev 6/5/4/3/2/1 are superseded — see
**Superseded plans**)._

## Objective for the next round

**Turn the page from binding to building: open the reconstruction workstream with `G1`,
live plane removal — starting with the adoption row that turns the corpus's claim into a
spec.**

The board says the capture/ABI phase is over: 53 rows `DONE`, the viewer family landed
(`V1`, `V3`, `V4`; `V2`'s backpressure half and `V5` are among the three `HW` rows waiting on
one run), and the Windows lane handed off. What is left on the macOS board is **one
acceptance run** (clearing `#41`/`#53`/`#56`) and **one user decision** (`#21`). There is no
unstarted workstream except the G-goals, and among them `G1` is the corpus's own next step
once the usage fix is set aside — and the operator's own complaint. It is also the only G-goal
that ends in working software without needing another goal first.

The round's aim is therefore *G1's spec*, not G1's code: the corpus doc is frozen and its
plane-removal ABI was never adopted into findings, so it needs a recon row before a build row
(the rule this file has carried since rev 5). `G2` is the natural second row — its header
prerequisite (`#40`) has landed — and is queued below.

## Critical path

```
G1 (live plane removal) ─► recon adoption row ─► build row (bind the ABI for A/B, or
                            reimplement RANSAC + signed-distance)  ─► live per-frame filtering

G2 (coverage heatmap) ─► build (prereq #40 landed) ─► G3 (owned tracking, per-pose reasons)
G4/G5 (denoise + fusion, the accuracy harness) ─► need a fusion feature to exist first

#56 · #41 · #53 (HW) ─► ONE acceptance run (the stall test + V5's visual) ─► three rows close
#58 (RECON, in flight) ─► OQ-S13/S14        #21 (TODO) ─► operator decision
```

Ranked by what it frees, not by board order:

1. **`G1`.** Opens the reconstruction workstream, is the operator's stated pain, and needs
   one recon row to start. Nothing else unstarted frees as much.
2. **The acceptance run.** Not an aim, but the cheapest rows-cleared on the board: three
   `HW` rows on one manual session. Hardware does not compete with recon.
3. **`G2`.** Its prerequisite landed with `#40`, and it is the only G-goal that unblocks
   another (`G3` depends on its feedback layer).
4. **`G6`.** The corpus ranked "fix our own SDK usage" first; §1.5's drain half is now
   **corrected by recon** (see **Orphaned work**), so what remains is the voxel-size claim —
   live, but unverified, and therefore also an adoption row rather than a build row.

## Ready now

| Row | Role | What it needs |
| --- | --- | --- |
| the acceptance run | hardware | One session; the spec is `tests/hardware/plan56-stall-and-v5-visual.md`. Closes `#56`, `#41`, `#53`. |
| `G1`'s adoption row (unfiled) | recon | Turn the corpus's plane-removal ABI + decision-fn into a findings doc, and answer the re-homing question below. |
| `G2`'s build row (unfiled) | builder | The under-covered point set from the vendor's own per-point result, painted on the live cloud. `#40` landed. |
| `#58` | recon | In flight. The `OQ-S13`/`S14` residues (`#55`'s). |
| OQ-7b's row (unfiled) | hardware | The listener-less with-device run; `#12`'s note waits on it. |
| `#21` | operator | The tiering decision for added run records — a policy call, not agent work. |

## Blocked, and on what

| Row | Blocked on |
| --- | --- |
| `G3` | `G2` — the feedback layer it needs to display per-pose reasons. |
| `G4`, `G5` | A fusion feature actually being planned; a denoise/fusion harness measures something that does not exist yet. |
| `G1`'s build row | `G1`'s adoption row — recon before build, per the corpus rule. |
| the Windows lane | Nothing. Handed off by operator rule — see **Windows fork**. |
| `#21` | The operator. |

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
2. **Evidence provenance (`#21`).** Added records and logs under `tests/results/**` are
   script-tier, so a fabricated or mis-transcribed record auto-merges with no judgment pass.
   Cheapest retirement: the operator decision; interim, land each manual report as a record.
3. **A second evidence corpus outside the gate.** `logs/**` is tracked and is the evidence
   base for the corpus's §1, but it is neither `tests/results/**` nor `docs/recon/**`, so the
   `run-records` check and the tier rules do not reach it. Cheapest retirement: an operator
   decision on whether it stays.
4. **One serialized hardware resource.** The acceptance run and `OQ-7b` queue behind one
   manual session. Batch per session, cheapest-first.
5. **Recon step budget.** `recon` is capped at `steps: 40`, one board row per session. `G1`'s
   adoption, `#58`, and any `OQ-S12` row are each a session — an order that also asks for
   adjacent context comes back empty.
6. **Verification debt.** Claims tagged `[inferred]` that code depends on: the parked
   `OQ-S7/S9/S10/S11` residuals, `OQ-S2`/`S3`, and the tips/control rows `#9` left `[inferred]`.

## Next round, as work-order seeds

For the dispatcher to expand — not dispatches in themselves.

1. **`G1`'s adoption row** → recon: adopt the corpus's plane-removal ABI + decision-fn into a
   findings doc; answer the **re-homing question** and give a **bind vs reimplement** verdict.
   **Wall:** recon owns the header and `docs/recon/**`; the row's deliverable is a spec, no
   code. One target, one session.
2. **The acceptance run** → hardware: one session, the combined stall/V5 spec. Closes `#56`,
   `#41`, `#53`; archive the raw log and any crash report per the record rules. **Wall:**
   hardware writes only `tests/results/**`.
3. **`G2`'s build row** → builder: the under-covered point set painted on the live cloud, from
   the vendor's own per-point result. **Wall:** builder edits `ui/`, `tools/`; the getters'
   semantics stay recon's.
4. **`OQ-S12`'s row** → recon: decide declare-the-export vs document-the-getter. **Wall:**
   recon owns the header.
5. **`OQ-7b`'s row** → hardware: the listener-less with-device run, batched with (2).

Also ready, non-competing: `#58` (recon, in flight), `#21` (operator decision), and the
`G6` voxel-size adoption if a recon session frees up.

## Superseded plans

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
