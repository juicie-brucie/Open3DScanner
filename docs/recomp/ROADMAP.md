# Recomp roadmap

Forward strategy for the recon → build → hw → reconcile loop. Where
[`PLAN.md`](PLAN.md) is the live **queue** (one line per task, move the status as you
go), this file is the **plan**: the critical path, what is blocked and on what, the
risks, and the objective of the next round.

Owner: the `openscanner/planner` agent. The dispatcher consumes it and applies the row
moves it proposes. Statuses, claims and evidence live on the board — this file points at
them and never restates them.

_Last planned: 2026-10-17 (rev 6 — the live path landed, so the objective moves to scaling
it, and the operator's manual `#25` rerun is handed to the dispatcher. rev 5 captured the
research goals G1–G7; rev 4 the reconciliation; rev 3 the corpus; rev 2/rev 1 superseded —
see **Superseded plans**)._

## Objective for the next round

**Keep the live view live as the cloud grows.**

The premise is proven: `#25` is `DONE` and OQ-S5 is resolved — live points publish during
active scanning (`tests/results/2026-10-17-scan-server-live-acceptance.md`) and the
live-path decision landed ([`findings/scan-live-data.md`](../recon/findings/scan-live-data.md) §6).
The next failure is **latency**, and the operator's manual rerun (below) found it. Bound
the per-tick cost so the view stays live as a scan progresses, and settle `#30` — which
may share a root cause.

## Dispatcher handoff — operator's manual `#25` rerun (2026-10-17)

The operator reran `#25` read-only and watched it live. The report lives **outside the
repo** (`~/Downloads/plan25-manual-rerun-2026-10-17.txt`) and is **not committed**. It
independently confirms OQ-S5's premise — live points accumulate and a mesh builds as the
scanner moves — and adds two observations the official record does **not** carry:

1. **Latency scales with accumulated geometry, unbounded.** The HUD's `latency` went from
   `~1.8 ms` at `points=11,958` (frame #113) to **`339.7 ms`** at `points=1,630,136`
   (frame #505), with `data` fps falling to **0.0** while `render` held **60.1 fps**. The
   live path is O(points) per tick with no cap, and points grow without bound during a
   scan — so the view stalls exactly as the user keeps working. The acceptance run sampled
   only up to 883 points, so it never entered this regime.
   - **Possible link to `#30`:** if `scan_server` polls both getters per tick and both
     return the same large buffer (which is what `#30`'s lockstep shows), that is duplicated
     work on the same path. A hypothesis, not a finding — for the role that can measure.
2. **`pkill -f "scan_server --live"` does not stop the worker.** It kills the launcher; the
   `scan_server --live-worker …` child survives, holds the port and the device, and must be
   killed by PID. A leaked worker is a **second SDK user**, which this loop treats as a hard
   error. README's teardown guidance covers Ctrl+C, not this path.

**Proposed:** land the report as a run record under `tests/results/**` (dispatcher, per the
`live-scan-manual-test` precedent), and file one row per observation. The latency row should
carry the acceptance criteria, since "the view stays live" is the whole point of `#25`.

## Critical path

```
latency / scale (operator's rerun) ─► profile the per-tick cost ─► bound it ─► hardware re-run
        └─ likely shares a root cause with #30 (both getters polled per tick)

#11 (HW — now UNBLOCKED, #20 is DONE) ─► #5 (narrow or drop "reproducibly")

#26 #27 #28 #29 (RECON) ─► the listener channel; #28 is the live-path decision's revisit trigger
#30 (RECON) ─► get_raw_cloud's native body, and the raw∥color lockstep

#3, #4 (TODO, no spec) ─► scan family        G1–G7 (captured) ─► downstream backlog
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
| latency / scale (new) | dispatcher → builder/analyze | Land the operator's report as a record, then profile the per-tick path under load. |
| worker teardown (new) | builder | Make the launcher terminate its worker (or document the two-step stop); a leaked worker is a second SDK user. |
| `#11` | hardware | Unblocked: `#20` is `DONE`. The controlled remove-side-race re-run. |
| `#30` | recon | `get_raw_cloud`'s native body vs. the raw∥color lockstep; isolated run polling only `get_raw_cloud`. |
| `#26`, `#27`, `#28`, `#29` | recon | The adopted OQ rows; one target each (Risk 6). |
| `#7` | recon / build | OQ-7b/c/d follow-ups ([`findings/plan7-detection-path.md`](../recon/findings/plan7-detection-path.md)); OQ-7b is a hardware observation. |
| `#23` | recon | `SPEC-READY`; its follow-ups are `#24` (done), `#30`, and the wider scan surface. |

## Blocked, and on what

| Row | Blocked on |
| --- | --- |
| `#5` | `#11`. A `[hw-contradicted]` tag is cleared by a run, not by argument. |
| the listener live path | `#28` (OQ-P3): if `FRAME_READY`'s `data` carries a consumable cloud, the live-path decision is re-taken. |
| `#3`, `#4` | Recon capacity — both are `TODO` with no spec. Do **not** fold them into a recon row that is already carrying a target (Risk 6). |
| `#21` | A **user decision** on tiering added run records. |
| G1–G5, G7 | Being picked up; each needs its own row, and a recon row before any build row. |

## Orphaned open questions

An OQ with no task row is unowned work — the spec claims that lean on it are load-bearing
on nothing. Current state:

- **Adopted since rev 5:** OQ-P1 → `#26`, OQ-P2 → `#27`, OQ-P3 → `#28`, OQ-P4 → `#29`.
  OQ-S1 answered under `#24`; OQ-S5 resolved under `#25`.
- **OQ-S2** (does pause/stop publish temp→show?) — **still orphaned.** Decides whether the
  per-tick poll is the only publish path; overlaps the latency work, so adopt it there.
- **OQ-S3** (`+0x2e0` vs `+0x320` lock asymmetry) — **still orphaned**, low priority. The
  latency work may touch the same guards, so re-check before rowing separately.
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

## Risks

1. **The live path is unbounded in the cloud size — new, and live.** See the handoff above.
   Cost if it lands: the live view stalls as a scan progresses, which is the opposite of
   what `#25` was landed to fix. Cheapest retirement: a profile of the per-tick path
   (getter / double-poll / serialization / broadcast), then a cap.
2. **A leaked worker is a second SDK user.** The operator's rerun shows `pkill` on the
   launcher leaves the `--live-worker` child holding the port and the device. Cost: two SDK
   users on one device — the loop's hardest error — and a blocked next run. Cheapest
   retirement: launcher-side termination propagation, or a documented two-step stop.
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

1. **Land the operator's rerun** → dispatcher: commit the report as a run record under
   `tests/results/**` (per the `live-scan-manual-test` precedent), then file the two rows
   (latency/scale, worker teardown).
2. **Profile the per-tick path** → builder/analyze: measure where the 340 ms goes at ~1.6M
   points — the getter, the double-poll (`#30`), the serialization, or the broadcast — and
   report a number per stage. **Wall:** builder reads its own source; the getters' native
   bodies are recon's, not builder's.
3. **Bound the per-tick cost** → builder (after 2): cap or decimate the broadcast, or move
   to a delta/frame channel, so the view stays live as the cloud grows. Acceptance: the
   operator's regime, re-run.
4. **`#11`** → hardware: the controlled remove-side-race re-run; archive raw log and crash
   report per the record rules. Now unblocked. **Wall:** hardware writes only `tests/results/**`.
5. **`#26`–`#29`** → recon, one session each: the adopted OQ rows. `#28` is the live-path
   decision's revisit trigger.

Also ready, non-competing (no hardware): `#30` → recon; `#7` follow-ups → recon/build.

## Superseded plans

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
