# Recomp roadmap

Forward strategy for the recon → build → hw → reconcile loop. Where
[`PLAN.md`](PLAN.md) is the live **queue** (one line per task, move the status as you
go), this file is the **plan**: the critical path, what is blocked and on what, the
risks, and the objective of the next round.

Owner: the `openscanner/planner` agent. The dispatcher consumes it and applies the row
moves it proposes. Statuses, claims and evidence live on the board — this file points at
them and never restates them.

_Last planned: 2026-10-17 (rev 5 — captures the research corpus's **goals** once, as
G1–G7, and drops the adopt-in-place plan; rev 4 added the reconciliation, rev 3 folded in
the corpus, rev 2/rev 1 are superseded, see **Superseded plans**)._

## Objective for the next round

**Make the live view stream real points.** Land the color-cloud publish path
(`#24` → `#25`) and prove it in one hardware run (OQ-S5), and — because the research
corpus names a *second*, vendor-chosen live path — settle which path is ours and unblock
the loser's prerequisite (drain the data listener we are already required to register).

`#23` turned "0 points, always" into a mechanism ([`findings/scan-live-data.md`](../recon/findings/scan-live-data.md) §3),
and `#24`+`#25` are the contract+build pair that converts it into a live view. But
[`docs/research/pipeline-quality-assessment.md`](../research/pipeline-quality-assessment.md) §1.3/§1.5
records the vendor app using a *different* live channel and our own tooling registering
the data listener without draining it. Those claims are unreviewed (see Risk 2) but they
bear directly on what `#25` should build, so the round must decide before it builds.
This is one aim — live points — reached by whichever path the decision picks.

## Critical path

```
decide the live path (getter-publish vs listener-channel)
   ├─► #24 (color-cloud contract) ─► #25 (per-tick publish) ─┐
   └─► drain the data listener (builder) ────────────────────┤
                                                             └─► hardware run (OQ-S5) ─► scan family: #3, #4, OQ-S1…S4

#20 (re-read .ips) ─► #11 (remove-side-race re-run) ─► #5 (narrow or drop "reproducibly")

#2 / #19 (data-process run ALREADY LANDED) ─► board reconcile ─► #2, #19 DONE; OQ-P1…P4 stay open

docs/research/** goals (frozen corpus) ─► G1–G7 captured once ─► rows filed per goal (G6 now; G1–G5, G7 downstream)
```

Ranked by what it frees, not by board order:

1. **Live points** (`#24`/`#25` or the listener path). Gates the whole scan family and the
   first user-visible win; nothing else unblocks as much.
2. **`#20` → `#11` → `#5`.** `#20` is free (no recon, no hardware) and gates a
   **serialized** run; `#15` already fixed the target — the **remove-side race, not the
   re-add** (`PLAN.md` #15).
3. **The research corpus's goals (G1–G7).** Captured once under **Goals carried forward**;
   the corpus itself stays frozen. Only G6 is near-term; the rest are a downstream backlog
   the dispatcher files as rows.
4. **`#2` / `#19`.** Already satisfied in evidence; they gate nothing downstream but
   mislead the round (Risk 1).

## Ready now

| Row | Role | What it needs |
| --- | --- | --- |
| `#24` | recon | Header subset + findings for the color-cloud getter and its release; OQ-S1 folds in. Single target. |
| `#25` | builder | Per-tick publish in `scan_server`; **after** the live-path decision and `#24`'s typedef. |
| drain the data listener | builder | Consume what the listener delivers (or decide not to register it). Cheap; §1.5's evidence is the reason. |
| `#20` | dispatcher / analyze | Re-read the three archived `.ips` against `#5` and the crash analysis. No hardware, no recon. |
| `#2`, `#19` | dispatcher | Reconcile the **already-landed** `data_process_message_listener_test` record against the rows. |
| `#17` | hardware | A run settles OQ-4's async half; instrumentation landed in `#40`. |
| `#7` | recon / build | OQ-7b/c/d follow-ups ([`findings/plan7-detection-path.md`](../recon/findings/plan7-detection-path.md)); OQ-7b is a hardware observation. |

## Blocked, and on what

| Row | Blocked on |
| --- | --- |
| `#25` | The live-path decision, then `#24`'s typedef. |
| the listener live path | OQ-P3 (the `data` payload semantics are unmapped) — the drain itself is not blocked, only *using* the frames is. |
| `#11` | `#20`'s re-read (design validity) **then** the serialized hardware room. |
| `#5` | `#11`/`#20`. A `[hw-contradicted]` tag is cleared by a run, not by argument. |
| `#3`, `#4` | Recon capacity — both are `TODO` with no spec. Do **not** fold them into `#24` (Risk 6). |
| `#21` | A **user decision** on tiering added run records. |

## Orphaned open questions

An OQ with no task row is unowned work — the spec claims that lean on it are load-bearing
on nothing. Proposed adopters (numbers are proposals; the dispatcher assigns):

- **OQ-S1** (`scan+0x4c8` state writers) — **fold into `#24`.**
- **OQ-S2** (does pause/stop publish temp→show?) — **propose a recon row.** Decides whether `#25`'s poll is the only publish path.
- **OQ-S3** (`+0x2e0` vs `+0x320` lock asymmetry) — **propose a recon row**, low priority.
- **OQ-S4** (marker-framework `+0x4fa` users) — correctly deferred; leave unrowed.
- **OQ-S5** — `#25`'s acceptance; **adopt under `#25`.**
- **OQ-P1/P2**, **OQ-P3**, **OQ-P4** — `#2`'s run does not settle them. **OQ-P3 is now doubly load-bearing**: it gates the listener live path (Risk 2). Propose recon rows before `#2` closes.
- **OQ-1** — the `[inferred]` residual sits under `#1` (`DONE`). **Flag on `#1`** or it is lost.
- **OQ-5** (`tips`/`control`/`calib` `*_name` jump tables) — no `#3`-as-such row exists. **Propose a recon row.**
- **OQ-6** — accepted as-is by decision. Correct to leave unrowed.
- **OQ-7b** — what `#12`'s `get_device_num()` note waits on. **Adopt as a hardware row.**

## Goals carried forward from the research corpus (captured once)

`docs/research/**` (five docs, committed 2026-10-16, before the recon→findings structure)
holds the only statement of several of OpenScanner's own product goals. The corpus is
**frozen** — a historical artifact: no in-place maintenance, no adoption into findings.
Its vendor-fact claims are partly superseded by later recon, so any goal picked up from it
re-derives the facts it needs through recon like any other target. This section captures
the **goals once**, so they are tracked and not lost. The dispatcher files the rows; the
G-labels are the planner's, not row numbers.

| # | Goal | Origin | Needs first | Horizon |
| --- | --- | --- | --- | --- |
| **G1** | Live plane removal for **resource management** (cut data per frame during capture), not a post-scan cosmetic cut — the user's own "it does not work at all" complaint | `plane-removal-reverse-engineering.md` §5 | a recon row to adopt the ABI + decision-fn finding, **or** a build-only RANSAC/signed-distance path that skips vendor logic | after capture |
| **G2** | Coverage heatmap: replace the one-scalar "insufficient data" verdict with a spatial under-covered set painted on the live cloud | `scan-quality-and-coverage…` §5.2; `pipeline-quality-assessment.md` §3.3 | header-subset additions (`get_global_pose`, `get_markers`) → recon then build | after capture |
| **G3** | Owned tracking (marker detection + bundle adjustment) with per-pose rejection reasons surfaced | `pipeline-quality-assessment.md` §3.4 | G2's feedback layer to display them | medium–large |
| **G4** | Own denoising + fusion pipeline (outlier removal → PCA normals → bilateral/RIMLS → WLOP; ICP → pose-graph with loop closure → Poisson/BPA), Open3D as the MIT anchor | `point-cloud-denoising-and-fusion.md` §1–2 | a fusion feature actually being planned | long |
| **G5** | Fusion-accuracy evaluation harness (unidirectional MSD/RMS/min–max, non-parametric stats, repeatability across runs) | `point-cloud-denoising-and-fusion.md` §3 | G4, or any fusion change worth measuring | with G4 |
| **G6** | Fix our own SDK usage: drain the data listener, align voxel sizes | `pipeline-quality-assessment.md` §3.1 | nothing — cheap, and it serves **this round's** objective | **now** |
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

1. **The board is behind its own evidence — highest, and live.** `tests/results/2026-10-17-data-process-message-listener-test.md`
   is a clean 16/16 PASS on `main`, run *after* the `#53` launcher wiring, with its raw
   log and launcher banner present. Rows `#2` and `#19` still read "Remaining: a hardware
   run." Cost if it lands: a round spends the **serialized** room re-running a test that
   already passed. Cheapest retirement: a dispatcher reconcile pass. Live instance of
   Risk 3.
2. **Two unreconciled live-path claims.** [`findings/scan-live-data.md`](../recon/findings/scan-live-data.md) §3
   (the show cloud publishes only via the color getter) and
   [`docs/research/pipeline-quality-assessment.md`](../research/pipeline-quality-assessment.md) §1.3/§1.5
   (the vendor app uses the listener, not the getter; our tooling registers it and never
   drains it) are not strictly contradictory but point at different builds. Cost if it
   lands: builder builds the path the decision would have rejected, and a serialized run
   proves the wrong thing. Cheapest retirement: a decision pass before `#25` is built.
   Note the research half is **unreviewed** — it must be adopted (or refuted) by recon.
3. **Evidence provenance (`#21`).** Added records and logs under `tests/results/**` are
   script-tier, so a fabricated or mis-transcribed record auto-merges with no judgment
   pass. Cheapest retirement: the user decision on tiering; interim, require a companion
   raw log whose command line matches the record.
4. **A second evidence corpus outside the gate.** `logs/**` is tracked and is the evidence
   base for `pipeline-quality-assessment.md` §1, but it is neither `tests/results/**` nor
   `docs/recon/**`, so the `run-records` check and the tier rules do not reach it. Cost:
   two evidence homes, one gated and one not. Cheapest retirement: a **user decision** on
   whether `logs/**` stays (and under what discipline) or is folded into the record home.
5. **One serialized hardware resource.** `#11`, `#17`, the live-points run, and OQ-7b all
   queue behind one manual session. Batch per session, cheapest-first; never spend the
   room on `#11` before `#20` reads.
6. **Recon step budget.** `recon` is capped at `steps: 40`, one board row per session. A
   `#24` order that also asks for `#3`/`#4` context comes back empty — split it. Same for
   the G1–G5 goals when their rows are filed: one goal per session, facts first.
7. **Verification debt.** Spec claims tagged `[inferred]` that code already depends on:
   OQ-S1…S3 (scan, `#25` leans on them), OQ-P3, OQ-1's residual, and the tips/control
   rows `#9` left `[inferred]`.

## Next round, as work-order seeds

For the dispatcher to expand — not dispatches in themselves.

1. **Live-path decision** → recon/analyze: read `scan-live-data.md` §3 against
   `pipeline-quality-assessment.md` §1.3/§1.5 and the header; pick the path, and record
   the decision (and the loser's prerequisite) in the findings home. Cheap, no hardware.
2. `#24` → recon: color-cloud getter + release into the header subset, §3 semantics and
   OQ-S1. **Wall:** recon owns the header and `docs/recon/**`; deliverable is the typedef
   pair + findings, no code.
3. `#25` → builder (after the decision and `#24`): loader export + per-tick call so the
   live view publishes. **Wall:** builder edits `engine/src` and `tools/`, is denied the
   header. Acceptance is OQ-S5.
4. **Drain the data listener** → builder: consume what the listener delivers (or justify
   not registering it — `#2`'s spec requires the slot before preview). **Wall:** builder
   edits `tools/`. Cheap; independent of the decision.
5. **Hardware** → one run of the live path, settling OQ-S5. Archive raw log and any crash
   report per the record rules. **Wall:** hardware writes only `tests/results/**`.

Also ready, and non-competing (no recon, no hardware — they protect the next hardware
window rather than consume it):

6. `#20` → dispatcher/analyze: re-read the three archived `.ips` against `#5` and the
   crash analysis; correct whichever is wrong.
7. `#2` / `#19` → dispatcher: reconcile the landed data-process record against the rows,
   and adopt OQ-P1…P4 before closing `#2`.
8. **G1–G7** → dispatcher: file rows for the goals captured under **Goals carried forward**
   (G6 is this round's drain/config work; the rest are downstream). No corpus maintenance —
   the research docs stay frozen. One row per goal, and any goal that needs vendor facts
   gets its own recon row before a build row, exactly like a normal target.

## Superseded plans

- **2026-10-17 rev 2 — "Close the scan family's live-data loop: `#24` → `#25` → OQ-S5."**
  Superseded by rev 3. Kept as the round's *build* target, but rev 2 treated `#25`'s
  getter-publish path as the settled answer; the `docs/research/**` corpus records a
  second, vendor-chosen live path, so rev 3 adds the decision pass before the build.
  Reversal reason: a new input (the research corpus), not a contradiction of `#23`.
- **2026-10-17 rev 1 — "Drain the hardware queue, and open the scan family in parallel."**
  Superseded by rev 2. Half landed (the scan family was opened via `#23`); the other half
  was wrong as written — the queue's most obvious item (`#2`/`#19`) already has a passing
  run on `main`.
