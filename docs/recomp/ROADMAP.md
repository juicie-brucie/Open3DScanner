# Recomp roadmap

Forward strategy for the recon → build → hw → reconcile loop. Where
[`PLAN.md`](PLAN.md) is the live **queue** (one line per task, move the status as you
go), this file is the **plan**: the critical path, what is blocked and on what, the
risks, and the objective of the next round.

Owner: the `openscanner/planner` agent. The dispatcher consumes it and applies the row
moves it proposes. Statuses, claims and evidence live on the board — this file points at
them and never restates them.

_Last planned: 2026-10-18 (rev 7 — adds the viewer family V1–V5 and stages the
`docs/architecture/scan-viewer.md` draft for the dispatcher to land. rev 6 moved the
objective to scaling the live path; rev 5 captured the research goals G1–G7; rev 4 the
reconciliation; rev 3 the corpus; rev 2/rev 1 superseded — see **Superseded plans**)._

## Objective for the next round

**Give the interface a shape: land the viewer's frame protocol (`V1`).**

The live path works and now holds at scale (`#25` → `#32` → `#38`). What the app lacks is a
*shape*: the wire format is positions-only, unversioned and pose-free, so scanner-POV
following and a second client are both impossible today. `V1` (`frame_protocol.h` + a codec
on each end) is the foundation every other viewer row needs, and it is the cheapest thing
that unblocks the whole family. Alongside it, file V4's recon row (the pose getters) — it
uses recon's budget, not the hardware room, so it runs in parallel.

## Dispatcher handoff — operator's manual `#25` rerun (2026-10-17) — **LANDED**

Kept for provenance only; both observations were filed and are now `DONE`. The rerun landed
as `tests/results/2026-10-17-plan25-manual-rerun.md`.

1. **`#32` — the live view stalls at scale** (1.8 ms at 11,958 points → **339.7 ms** at
   1,630,136 points, `data 0.0 fps` while `render` held 60.1 fps). **Landed** (PR #77,
   `9cd0854`): a fixed **20,000-point decimated sample per tick**. This **retires Risk 1**
   and is most of **V2**.
2. **`#33` — `pkill` and the worker.** **Landed** (PR #77): the README teardown guidance was
   corrected — `pkill` *does* match the worker, and the observed linger is the graceful
   SIGTERM teardown (which can hang in `context_destroy`, → `#37`), not a leak.
   **Retires Risk 2.**

One follow-on is open: **`#38`** — the *final-mesh* broadcast is still uncapped (a
15.6M-point frame, 29.2 fps in the rerun).

## Critical path

```
V1 (frame protocol) ─► V2 (point budget — mostly landed as #32) ─► V3 (replay source)
        └─► V4 (pose + follow POV; needs a recon row for the pose getters) ─► V5 (WebGPU client)

#34 (frame API) · #35 (loader release) ─► the listener live path    #36 · #37 (RECON) ─► scale / teardown
#38 (final-mesh cap) ─► re-run            #30 (RECON) ─► get_raw_cloud's native body
#11 (HW — unblocked, #20 DONE) ─► #5      #3, #4 (TODO, no spec) ─► scan family
```

Ranked by what it frees, not by board order:

1. **Live-path scaling (new).** The operator's stated focus is capturing and writing scan
   data from the hardware, and this is the first thing that breaks in real use. It gates
   nothing on the board yet — which is why it needs a row.
2. **`#30`.** Cheap, and plausibly entangled with (1): both are about what the per-tick
   path does with the cloud.
3. **`#11` → `#5`.** Now **unblocked** — `#20` is `DONE`, so the `.ips` re-read that gated
   the re-run's design has landed. `#15` fixed the target (the remove-side race). `#11` is
   a real `SIGSEGV` on a feature the board already marks `DONE`, and it needs the
   **serialized** hardware room.
4. **`#26`–`#29` (RECON).** The listener channel and the preview lifecycle; `#28` is the
   revisit trigger for the live-path decision.
5. **`#3`, `#4`.** `TODO` with no spec; the rest of the scan family.

## Ready now

| Row | Role | What it needs |
| --- | --- | --- |
| `#11` | hardware | Unblocked: `#20` is `DONE`. The controlled remove-side-race re-run. |
| `#38` | builder | Bound the final-mesh broadcast like the live path. The tail of `V2`. |
| `#34`, `#35` | recon / build | The frame API in the header subset; the unresolved `realtime_model_mesh_release`. Both gate the listener live path. |
| `#36`, `#37` | recon | OQ-P5 (the five direct-out getters' layouts); the large-mesh teardown hang. |
| `#30` | recon | `get_raw_cloud`'s native body vs. the raw∥color lockstep; isolated run polling only `get_raw_cloud`. |
| `#26`, `#27`, `#28`, `#29` | recon | The adopted OQ rows; one target each (Risk 6). |
| `#7` | recon / build | OQ-7b/c/d follow-ups ([`findings/plan7-detection-path.md`](../recon/findings/plan7-detection-path.md)); OQ-7b is a hardware observation. |
| `#23` | recon | `SPEC-READY`; its follow-ups are `#24` (done), `#30`, and the wider scan surface. |

## Blocked, and on what

| Row | Blocked on |
| --- | --- |
| `#5` | `#11`. A `[hw-contradicted]` tag is cleared by a run, not by argument. |
| the listener live path | `#34` (the frame API in the header subset) then `#35`; `#28` (OQ-P3) answered that `FRAME_READY`'s payload is a frame struct. |
| `#3`, `#4` | Recon capacity — both are `TODO` with no spec. Do **not** fold them into a recon row that is already carrying a target (Risk 6). |
| `#21` | A **user decision** on tiering added run records. |
| G1–G5, G7 | Being picked up; each needs its own row, and a recon row before any build row. |

## Orphaned open questions

An OQ with no task row is unowned work — the spec claims that lean on it are load-bearing
on nothing. Current state:

- **Adopted since rev 5:** OQ-P1 → `#26`, OQ-P2 → `#27`, OQ-P3 → `#28`, OQ-P4 → `#29`.
  OQ-S1 answered under `#24`; OQ-S5 resolved under `#25`.
- **OQ-S2** (does pause/stop publish temp→show?) — **still orphaned.** Decides whether the
  per-tick poll is the only publish path; the `V1`/`V2` protocol work will touch the same
  path, so adopt it there rather than as a separate row.
- **OQ-S3** (`+0x2e0` vs `+0x320` lock asymmetry) — **still orphaned**, low priority. The
  decimation work (`#32`) already sits on this path; re-check before rowing separately.
- **OQ-1** — the `[inferred]` residual sits under `#1` (`DONE`). **Flag on `#1`** or it is lost.
- **OQ-5** (`tips`/`control`/`calib` `*_name` jump tables) — **still orphaned**; propose a recon row.
- **OQ-6** — accepted as-is by decision. Correct to leave unrowed.
- **OQ-7b** — what `#12`'s `get_device_num()` note waits on. **Adopt as a hardware row.**

## Goals carried forward from the research corpus (captured once)

`docs/research/**` (five docs, committed 2026-10-16, before the recon→findings structure)
holds the only statement of several of OpenScanner's own product goals. The corpus is
**frozen** — a historical artifact: no in-place maintenance, no adoption into findings.
Its vendor-fact claims are partly superseded by later recon, so any goal picked up from it
re-derives the facts it needs through recon like any other target. Captured once so they
are tracked and not lost. The dispatcher files the rows; the G-labels are the planner's.

| # | Goal | Origin | Needs first | Horizon |
| --- | --- | --- | --- | --- |
| **G1** | Live plane removal for **resource management** (cut data per frame during capture), not a post-scan cosmetic cut — the user's own "it does not work at all" complaint | `plane-removal-reverse-engineering.md` §5 | a recon row to adopt the ABI + decision-fn finding, **or** a build-only RANSAC/signed-distance path that skips vendor logic | after capture |
| **G2** | Coverage heatmap: replace the one-scalar "insufficient data" verdict with a spatial under-covered set painted on the live cloud | `scan-quality-and-coverage…` §5.2; `pipeline-quality-assessment.md` §3.3 | header-subset additions (`get_global_pose`, `get_markers`) → recon then build | after capture |
| **G3** | Owned tracking (marker detection + bundle adjustment) with per-pose rejection reasons surfaced | `pipeline-quality-assessment.md` §3.4 | G2's feedback layer to display them | medium–large |
| **G4** | Own denoising + fusion pipeline (outlier removal → PCA normals → bilateral/RIMLS → WLOP; ICP → pose-graph with loop closure → Poisson/BPA), Open3D as the MIT anchor | `point-cloud-denoising-and-fusion.md` §1–2 | a fusion feature actually being planned | long |
| **G5** | Fusion-accuracy evaluation harness (unidirectional MSD/RMS/min–max, non-parametric stats, repeatability across runs) | `point-cloud-denoising-and-fusion.md` §3 | G4, or any fusion change worth measuring | with G4 |
| **G6** | Fix our own SDK usage: drain the data listener, align voxel sizes | `pipeline-quality-assessment.md` §3.1 | **half open** — the live-path decision kept the listener registered and unconsumed (findings §6); the voxel-size half is untouched | **now** |
| **G7** | Keep laser extraction + calibration behind the SDK until the "drop OBScan entirely" decision | `pipeline-quality-assessment.md` §3.5 | that decision (README, "Possible future direction") | standing |

Two design constraints from the corpus bind any row above: keep capture decoupled from
fusion via a bounded queue (`live-scanning-pipeline-overview.md`), and keep the
frame-acceptance gate and the bundle adjuster as **two separate systems** rather than one
unified heuristic (`pipeline-quality-assessment.md` §2).

**Why captured, not adopted.** All three vendor-RE docs rest on a "Ghidra cannot complete
on this binary" premise that no longer holds — recon now runs Ghidra/REA over a persisted
snapshot (see the method line of
[`findings/data-process-message-listeners.md`](../recon/findings/data-process-message-listeners.md)
and [`docs/tooling/rea-ghidra-setup.md`](../tooling/rea-ghidra-setup.md)) — and the corpus
predates the listener ABI and detection-path findings that supply the vendor's actual live
channel. The *goals* survive that; the *facts* behind them must be re-derived per goal.

One item for recon when G1 is picked up: the plane-removal doc attributes its three toggle
slots to `OBScanDataManagement`, while current recon homes the data-management subobject
at `scan+0xf0` on `OBScanBase` ([`findings/scan-live-data.md`](../recon/findings/scan-live-data.md) §1).
Same object, possibly a re-homing — verify, don't assume.

## Viewer family (V1–V5) — the interface, captured 2026-10-18

The current UI is a proof of concept: `ui/app.js`, a dependency-free WebGL2 `gl.POINTS`
viewer with no camera interaction, no pose, and no transport budget — it renders the whole
cloud every tick. The target is a robust interface that renders the active scan, follows
the scanner's point of view, and maps cleanly onto Windows, Linux and macOS. Full
architecture: `docs/architecture/scan-viewer.md`, **staged at the end of this file** for
the dispatcher to land. The dispatcher files the rows; the V-labels are the planner's.

| # | Goal | Needs first | Horizon |
| --- | --- | --- | --- |
| **V1** | `frame_protocol.h` + a codec on both ends — versioned envelope, typed messages; today's wire shape becomes v1 | nothing; this is the foundation | **first** |
| **V2** | Server-side decimation to a point budget + client backpressure | V1 | **first** — retires the live-path latency risk (Risk 1) |
| **V3** | `ReplayCaptureSource` + one recorded session, so the viewer develops without the scanner | V1 | **first** — unblocks V4/V5 from the serialized hardware room |
| **V4** | `pose` message + the `follow` camera mode (scanner POV) | V1; **a recon row for `get_global_pose`/`get_markers`**, which the header does not bind | next |
| **V5** | WebGPU renderer + orbit controls, WebGL2 fallback; optional thin native shell | V1–V3 | next |

**The first two rows to file**, per the operator's direction: the **recon row for the pose
getters** (V4's blocker — nothing else in this family is blocked on recon) and the
**decimation/budget work** (V2). V2 is the same fix the latency risk asks for, so one row
retires both.

Device-layer portability is a separate, harder thread: the seam (`CaptureSource`) can be
built now, but the device layer only becomes OS-agnostic when the capture source moves off
OBScan to the Orbbec SDK — see `README.md`'s "Possible future direction" and **G7**.

## Risks

1. ~~The live path is unbounded in the cloud size~~ — **retired by `#32`** (PR #77). Kept for
   the record: the operator's rerun observed the stall, the dispatcher filed it, the builder
   decimated the per-tick sample to 20k points. The remaining scale item is `#38` (final
   mesh), which is **V2**'s tail.
2. ~~A leaked worker is a second SDK user~~ — **retired by `#33`** (PR #77). The linger was
   the graceful SIGTERM teardown, not a leak; the README guidance was the defect. The
   teardown *hang* it exposed is `#37`.
3. **Evidence provenance (`#21`).** Added records and logs under `tests/results/**` are
   script-tier, so a fabricated or mis-transcribed record auto-merges with no judgment
   pass. Two manual reruns now sit **outside** the repo entirely (the earlier live-scan
   test, and this one) — the class of evidence that most needs a home. Cheapest retirement:
   the user decision on tiering; interim, land each manual report as a record.
4. **A second evidence corpus outside the gate.** `logs/**` is tracked and is the evidence
   base for `pipeline-quality-assessment.md` §1, but it is neither `tests/results/**` nor
   `docs/recon/**`, so the `run-records` check and the tier rules do not reach it. Cheapest
   retirement: a **user decision** on whether `logs/**` stays (and under what discipline).
5. **One serialized hardware resource.** `#11`, the latency re-run, and OQ-7b all queue
   behind one manual session. Batch per session, cheapest-first; never spend the room on
   `#11` before its design is settled (`#20` did that).
6. **Recon step budget.** `recon` is capped at `steps: 40`, one board row per session —
   `#26`–`#29` are each a session, and a `#24`-style order that also asks for `#3`/`#4`
   context comes back empty.
7. **Verification debt.** Spec claims tagged `[inferred]` that code already depends on:
   OQ-S2/S3, OQ-1's residual, and the tips/control rows `#9` left `[inferred]`.

## Next round, as work-order seeds

For the dispatcher to expand — not dispatches in themselves.

1. **Land the staged architecture doc** → dispatcher: move the section at the end of this
   file into `docs/architecture/scan-viewer.md`, delete the staging section, and PR it.
   `docs/architecture/**` is agent-tier, so it needs a verifier verdict.
2. **`V1`** → builder: `frame_protocol.h` + a codec on each end; today's wire shape becomes
   v1. **Wall:** builder edits `engine/src`, `tools/`, `ui/`; denied the header.
3. **`V4`'s recon row** → recon: spec `obscan_scan_get_global_pose` and
   `obscan_scan_get_markers` (unbound in the header) — what each returns and in which
   frame. One target, one session; this is V4's only blocker.
4. **`#38`** → builder: bound the final-mesh broadcast like the live path.
5. **`#11`** → hardware: the controlled remove-side-race re-run; archive raw log and crash
   report per the record rules. Unblocked. **Wall:** hardware writes only `tests/results/**`.

Also ready, non-competing: `#34`/`#35` (recon/build — the listener live path), `#36`/`#37`
(recon), `#30` (recon), `#7` follow-ups (recon/build).

## Superseded plans

- **2026-10-18 rev 6 → rev 7 — objective change.** rev 6's aim was "keep the live view live
  as the cloud grows"; `#32` (and `#38`) landed that, and the operator opened a new
  direction — the viewer architecture — so rev 7 moves the aim to the viewer's foundation
  (`V1`). Reversal reason: the objective was achieved and a new input arrived, the same
  pattern as rev 6.
- **2026-10-17 rev 5 → rev 6 — objective change.** rev 5's aim was "make the live view
  stream real points"; `#25` landed and OQ-S5 is resolved, so rev 6 moves the aim to
  *scaling* it. Reversal reason: the objective was achieved, and a new input (the
  operator's rerun) named the next failure.
- **2026-10-17 rev 2 — "Close the scan family's live-data loop: `#24` → `#25` → OQ-S5."**
  Superseded by rev 3, and now fully landed. rev 2 treated `#25`'s getter-publish path as
  the settled answer; the research corpus recorded a second, vendor-chosen live path, so
  rev 3 added the decision pass — which landed as findings §6.
- **2026-10-17 rev 1 — "Drain the hardware queue, and open the scan family in parallel."**
  Superseded by rev 2. Half landed (the scan family was opened via `#23`); the other half
  was wrong as written — the queue's most obvious item (`#2`/`#19`) already had a passing
  run on `main`.
