# Recomp roadmap

Forward strategy for the recon → build → hw → reconcile loop. Where
[`PLAN.md`](PLAN.md) is the live **queue** (one line per task, move the status as you
go), this file is the **plan**: the critical path, what is blocked and on what, the
risks, and the objective of the next round.

Owner: the `openscanner/planner` agent. The dispatcher consumes it and applies the row
moves it proposes. Statuses, claims and evidence live on the board — this file points at
them and never restates them.

_Last planned: 2026-10-18 (rev 9 — adds the Windows fork workstream and stages its charter
plus the fork-coordination file. rev 8 was the refresh pass; its architecture-doc staging
block has landed and moved to `docs/architecture/scan-viewer.md`. rev 7 added the viewer
family; rev 6/5/4/3/2/1 are superseded — see **Superseded plans**)._

## Objective for the next round

**Give the interface a shape: land the viewer's frame protocol (`V1`) — which currently has
no board row.**

The live path works and now holds at scale (`#25` → `#32` → `#38` → `#39`). What the app
lacks is a *shape*: the wire format is positions-only, unversioned and pose-free, so
scanner-POV following and a second client are both impossible today. `V1`
(`frame_protocol.h` + a codec on each end) is the foundation every other viewer row needs.

**First action for the dispatcher: file the `V1` row.** Rev 7 proposed three rows and the
dispatcher filed them (`#39`, `#40`, `#41`) — but not the foundation itself, so the
objective's own row is missing. `V4`'s blocker (`#40`) is already filed and needs no
further planning.

## Viewer family (V1–V5) — the interface, captured 2026-10-18

The UI is still a proof of concept: `ui/app.js`, a dependency-free WebGL2 `gl.POINTS`
viewer with no camera interaction and no pose. The target is a robust interface that renders
the active scan, follows the scanner's point of view, and maps cleanly onto Windows, Linux
and macOS. Full architecture: `docs/architecture/scan-viewer.md` (**staged at the end of
this file**; boundaries below). The V-labels are the planner's.

| # | Goal | Board row | State |
| --- | --- | --- | --- |
| **V1** | `frame_protocol.h` + a codec on each end — versioned envelope, typed messages; today's wire shape becomes v1 | **none — file it** | **the objective**; nothing blocks it |
| **V2** | A point budget on the wire, plus client backpressure | `#32`, `#38` (budget) · `#41` (backpressure) | **budget landed**; backpressure open |
| **V3** | `ReplayCaptureSource` + one recorded session, so the viewer develops without the scanner | none | unfiled; needs V1 |
| **V4** | `pose` message + the `follow` camera mode (scanner POV) | `#40` (recon) | filed; needs V1 |
| **V5** | WebGPU renderer + orbit controls, WebGL2 fallback; optional thin native shell | none | unfiled; needs V1–V3 |

**Landed vs forward — do not read one as the other.** `#32` shipped a **fixed 20,000-point
*stride* sample per tick**, and `#38` capped the final-mesh frame; `#39` adds the on-demand
full-detail path. That is the *budget* half of V2, and it is **done**. The protocol rules in
the architecture doc — **voxel** decimation, delta encoding, quantization, and backpressure
— are a **forward design** layered on top; only backpressure is rowed (`#41`). The doc marks
which is which.

Device-layer portability is a separate, harder thread: the seam (`CaptureSource`) can be
built now, but the device layer only becomes OS-agnostic when the capture source moves off
OBScan to the Orbbec SDK — see `README.md`'s "Possible future direction" and **G7**.

## Dispatcher handoff — operator's manual `#25` rerun (2026-10-17) — **CLOSED**

Kept for provenance only. The rerun landed as
`tests/results/2026-10-17-plan25-manual-rerun.md`; both observations it raised were filed and
are `DONE` (`#32` the live-view stall, `#33` the `pkill`/worker linger — the latter was a
README-guidance defect, not a leak). Its follow-on chain ran to completion too: `#38` capped
the final-mesh frame and `#39` (in flight, PR #95) restores a full-detail path. The teardown
*hang* it exposed is still open as `#37`.

## Critical path

```
V1 (frame protocol — UNFILED, the objective) ─┬─► V4 (pose + follow POV; blocker #40 is filed)
                                              ├─► V3 (replay source) ─► V5 (WebGPU client)
                                              └─► V2 backpressure (#41; the budget half landed as #32/#38)

#39 (full-detail path, PR #95) ─► the viewer's final-model path
#34 (frame API) · #35 (loader release) ─► the listener live path
#36 · #37 (RECON) ─► scale / teardown        #11 (HW — unblocked, #20 DONE) ─► #5
#7 (BUILD) ─► OQ-7b/c/d                      #23 (SPEC-READY) ─► the wider scan surface
```

Ranked by what it frees, not by board order:

1. **`V1`.** It is the objective, it has no row, and every other viewer row waits on it.
   Filing it is the cheapest action available.
2. **`#39`.** In flight (PR #95) and user-visible: the final model on screen is currently a
   20k preview. Its row is the live edge of the viewer family.
3. **`#40`.** Filed and blocking `V4`; recon-only, so it does not compete for the hardware
   room.
4. **`#11` → `#5`.** Unblocked (`#20` is `DONE`; `#15` fixed the target to the remove-side
   race). A real `SIGSEGV` on a feature the board marks `DONE`, and it needs the
   **serialized** hardware room.
5. **`#34` → `#35`.** The listener live path's prerequisites.
6. **`#36`, `#37`, `#7`, `#23`.** Recon/analysis backlog.

## Ready now

| Row | Role | What it needs |
| --- | --- | --- |
| `V1` (unfiled) | dispatcher | File the row; then builder owns `frame_protocol.h` + codecs. |
| `#39` | builder | In flight (PR #95). The on-demand full-detail path. |
| `#40` | recon | V4's blocker: spec `get_global_pose`/`get_markers`. One target, one session. |
| `#41` | builder | V2's remainder: client backpressure on the frame stream. |
| `#11` | hardware | Unblocked: `#20` is `DONE`. The controlled remove-side-race re-run. |
| `#34`, `#35` | recon / build | The frame API in the header subset; the unresolved `realtime_model_mesh_release`. |
| `#36`, `#37` | recon | OQ-P5 (the five direct-out getters' layouts); the large-mesh teardown hang. |
| `#7` | recon / build | OQ-7b/c/d follow-ups ([`findings/plan7-detection-path.md`](../recon/findings/plan7-detection-path.md)). |
| `#23` | recon | `SPEC-READY`; its follow-ups have been spun out into their own rows. |

## Blocked, and on what

| Row | Blocked on |
| --- | --- |
| `#5` | `#11`. A `[hw-contradicted]` tag is cleared by a run, not by argument. |
| the listener live path | `#34` (the frame API in the header subset) then `#35`. |
| `V3`, `V4`, `V5` | `V1`. |
| `#21` | A **user decision** on tiering added run records. |
| G1–G5, G7 | Being picked up; each needs its own row, and a recon row before any build row. |

## Orphaned open questions

An OQ with no task row is unowned work — the spec claims that lean on it are load-bearing
on nothing. Current state:

- **All four PLAN #2 OQs are closed out** — OQ-P1…P4 were adopted as `#26`–`#29`, which have
  all landed. OQ-S1 was answered under `#24`; OQ-S5 resolved under `#25`.
- **OQ-S2** (does pause/stop publish temp→show?) — **still orphaned.** The `V1` protocol
  work touches the same publish path, so adopt it there rather than as a separate row.
- **OQ-S3** (`+0x2e0` vs `+0x320` lock asymmetry) — **still orphaned**, low priority. The
  landed decimation (`#32`) already sits on this path; re-check before rowing separately.
- **OQ-1** — the `[inferred]` residual sits under `#1` (`DONE`). **Flag on `#1`** or it is lost.
- **OQ-5** (`tips`/`control`/`calib` `*_name` jump tables) — **still orphaned**; propose a recon row.
- **OQ-6** — accepted as-is by decision. Correct to leave unrowed.
- **OQ-7b** — what `#12`'s `get_device_num()` note waits on. **Adopt as a hardware row.**

Not an OQ, but the same failure mode: **`V1` is the round's objective and has no row.** An
objective with no row is unowned work with extra steps.

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
| **G2** | Coverage heatmap: replace the one-scalar "insufficient data" verdict with a spatial under-covered set painted on the live cloud | `scan-quality-and-coverage…` §5.2; `pipeline-quality-assessment.md` §3.3 | header-subset additions (`get_global_pose`, `get_markers`) — **overlaps `#40`** | after capture |
| **G3** | Owned tracking (marker detection + bundle adjustment) with per-pose rejection reasons surfaced | `pipeline-quality-assessment.md` §3.4 | G2's feedback layer to display them | medium–large |
| **G4** | Own denoising + fusion pipeline (outlier removal → PCA normals → bilateral/RIMLS → WLOP; ICP → pose-graph with loop closure → Poisson/BPA), Open3D as the MIT anchor | `point-cloud-denoising-and-fusion.md` §1–2 | a fusion feature actually being planned | long |
| **G5** | Fusion-accuracy evaluation harness (unidirectional MSD/RMS/min–max, non-parametric stats, repeatability across runs) | `point-cloud-denoising-and-fusion.md` §3 | G4, or any fusion change worth measuring | with G4 |
| **G6** | Fix our own SDK usage: drain the data listener, align voxel sizes | `pipeline-quality-assessment.md` §3.1 | **half open** — the listener is still registered and unconsumed (findings §6); `#31` added instrumentation, not a consumer. The voxel-size half has no row. | open |
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

## Windows fork — a second workstream, captured 2026-10-18

The operator is standing up a Windows dev environment for a Windows port. The repo is
**`Open3DScanner-Win64`** — standalone, *not* a GitHub fork relationship; the parent stays the
pinned upstream. Charter: `WINDOWS-FORK.md` and coordination: `FORK-COORDINATION.md`, both
**staged at the end of this file** for the dispatcher to land.

**The gate is a question, not a task:** does Creality ship a Windows build with the same
`obscan_*` surface? If not, this is not a port at all — it is the Orbbec-SDK path in
`README.md`'s "Possible future direction". Everything else waits on that answer, so **no rows
are proposed this round**; the gate is an operator answer, not dispatchable work.

Already true, from the tree: the loader is **`#ifdef`-split** (`LoadLibraryA` /
`GetProcAddress` / `GetLastError` vs `dlopen`) and `engine/CMakeLists.txt` has a `WIN32`
branch — the engine is closer than it looks. Not true: `tools/` and `tests/hardware/` are
`if(APPLE)` throughout, the hardware guardrail is a bash hook that will not fire on Windows,
and every recon artifact is macOS arm64 — so the ABI must be recovered again, not
reinterpreted.

**The one rule that matters:** `obscan_api.h` is single-sourced in the parent. Platform
differences go into it as `#ifdef`-guarded declarations with a Windows findings doc beside the
macOS ones. A second header in a second repo is a second contract, and two contracts drift —
the exact failure the one-home rule exists to prevent.

**The channel already exists.** `scripts/remote-opencode.sh` (parent, PR #150) addresses a
second machine's OpenCode server through the desktop app's server list; the Windows box is
registered as `Windows11` and answers (OpenCode 2.0.32, verified 2026-10-18). The charter's §5
names it as the coordination channel, and the fork's copy of the coordination file is seeded
with its first `[win]` entry at repo-creation time.

## Risks

1. ~~The live path is unbounded in the cloud size~~ — **retired by `#32`**, and the
   follow-on chain (`#38`, `#39`) is closed or in flight. Kept for the record only.
2. ~~A leaked worker is a second SDK user~~ — **retired by `#33`** (a README-guidance
   defect). The teardown *hang* it exposed is still open as `#37`.
3. **Evidence provenance (`#21`).** Added records and logs under `tests/results/**` are
   script-tier, so a fabricated or mis-transcribed record auto-merges with no judgment
   pass. Manual reruns keep arriving from outside the repo — the class of evidence that
   most needs a home. Cheapest retirement: the user decision on tiering; interim, land each
   manual report as a record.
4. **A second evidence corpus outside the gate.** `logs/**` is tracked and is the evidence
   base for `pipeline-quality-assessment.md` §1, but it is neither `tests/results/**` nor
   `docs/recon/**`, so the `run-records` check and the tier rules do not reach it. Cheapest
   retirement: a **user decision** on whether `logs/**` stays (and under what discipline).
5. **One serialized hardware resource.** `#11` and OQ-7b queue behind one manual session
   (the scale re-runs are done). Batch per session, cheapest-first.
6. **Recon step budget.** `recon` is capped at `steps: 40`, one board row per session.
   `#36`, `#37` and `#40` are each a session; an order that also asks for adjacent context
   comes back empty.
7. **Verification debt.** Spec claims tagged `[inferred]` that code already depends on:
   OQ-S2/S3, OQ-1's residual, and the tips/control rows `#9` left `[inferred]`.

## Next round, as work-order seeds

For the dispatcher to expand — not dispatches in themselves.

1. **File `V1`** → dispatcher: the objective has no row. `frame_protocol.h` + a codec on
   each end; today's wire shape becomes v1. **Wall:** builder edits `engine/src`, `tools/`,
   `ui/`; denied the header.
2. **Land the staged architecture doc** → dispatcher: move the staged block into
   `docs/architecture/scan-viewer.md` (boundaries in the staging header below), delete the
   block from this file, and PR it. `docs/architecture/**` is agent-tier → needs a verifier
   verdict.
3. **`#39`** → builder: finish the on-demand full-detail path (in flight, PR #95).
4. **`#11`** → hardware: the controlled remove-side-race re-run; archive raw log and crash
   report per the record rules. Unblocked. **Wall:** hardware writes only `tests/results/**`.

Also ready, non-competing: `#40` (recon), `#41` (builder), `#34`/`#35` (recon/build),
`#36`/`#37` (recon), `#7` follow-ups (recon/build).

## Superseded plans

- **2026-10-18 rev 7 → rev 8 — refresh pass, no objective change.** Rev 7's objective
  (`V1`) stands; what changed is that rev 7 restated loop state instead of pointing at it,
  and the loop moved under it. Rows `#3`/`#4`, `#26`–`#30`, `#38` were described as open
  while the board had them `DONE`; the landed decimation (`#32`) was described as pending
  alongside the forward protocol design. Rev 8 points at the rows and separates the two.
  Reversal reason: a verifier read of PR #97, not a change of direction.
- **2026-10-18 rev 6 → rev 7 — objective change.** rev 6's aim was "keep the live view live
  as the cloud grows"; `#32` (and `#38`) landed that, and the operator opened a new
  direction — the viewer architecture — so rev 7 moved the aim to the viewer's foundation
  (`V1`). Reversal reason: the objective was achieved and a new input arrived.
- **2026-10-17 rev 5 → rev 6 — objective change.** rev 5's aim was "make the live view
  stream real points"; `#25` landed and OQ-S5 is resolved, so rev 6 moved the aim to
  *scaling* it.
- **2026-10-17 rev 2 — "Close the scan family's live-data loop: `#24` → `#25` → OQ-S5."**
  Superseded by rev 3, and now fully landed. rev 2 treated `#25`'s getter-publish path as
  the settled answer; the research corpus recorded a second, vendor-chosen live path, so
  rev 3 added the decision pass — which landed as findings §6.
- **2026-10-17 rev 1 — "Drain the hardware queue, and open the scan family in parallel."**
  Superseded by rev 2. Half landed (the scan family was opened via `#23`); the other half
  was wrong as written — the queue's most obvious item (`#2`/`#19`) already had a passing
  run on `main`.

---

# STAGING — Windows fork drafts

**Dispatcher: these are drafts, not roadmap content.** Two documents are staged here, each
with its own boundaries. Extract each into its own file, delete this whole block from
`ROADMAP.md` in the same PR, and PR it.

| Draft | Lands at | Tier |
| --- | --- | --- |
| `WINDOWS-FORK.md` | the **fork** repo, `docs/recomp/WINDOWS-FORK.md` — or temporarily in the parent at `docs/recomp/WINDOWS-FORK.md` until `Open3DScanner-Win64` exists | agent-tier |
| `FORK-COORDINATION.md` | the **parent**, `docs/recomp/FORK-COORDINATION.md` | agent-tier |

**Boundaries:** each document is exactly the text between its `--- BEGIN <name>` and
`--- END <name>` marker lines, excluding the markers. There is no staging block in this
file's committed base, so the PR diff shows no removal to compare against; these boundaries
are the contract. Relative links are written for each file's final home.

---

--- BEGIN WINDOWS-FORK.md

# Windows fork — charter

Forward strategy for the Windows port of OpenScanner. This is the fork's **charter**: what it
is, what it shares with the parent, how work is coordinated, and what must be true before it
builds.

Owner: the `openscanner/planner` agent. Statuses, claims and evidence live on the board —
this file points at them and never restates them.

_Last planned: 2026-10-18._

## 1. The decision: name and shape

**Name: `Open3DScanner-Win64`** (owner `juicie-brucie`, sibling of `Open3DScanner`).

- `Win64` rather than `Windows` because the target is **x64** — the vendor ships an x64
  Windows build, and Windows-on-ARM is a separate question (Open question 2). The parent pins
  an **arm64** slice for the same reason; the arch belongs in the name.
- Alternatives: `Open3DScanner-Windows` (friendlier, less precise); `openscanner-win`
  (lowercase, breaks the repo family's naming).

**Shape: standalone, not a GitHub *fork* relationship.** A GitHub fork shares history and
routes PRs at the parent by default, which is wrong here — the fork's CI runs on Windows, its
vendor artifacts differ, and it must not accidentally PR platform-specific changes into the
parent's gate. Standalone, with the parent as a **pinned upstream**.

## 2. What stays canonical in the parent

The fork's most important rule, because it protects the one thing the recomp loop is built on.

**`engine/include/openscanner/obscan_api.h` is single-sourced in the parent.** A second
header in a second repo is a second contract, and two contracts drift. Therefore:

- Platform differences go into the **one** header as `#ifdef _WIN32`-guarded declarations,
  with a Windows findings doc alongside the macOS ones — never a forked copy.
- If the Windows DLL's ABI genuinely differs (calling convention, packing, export names), the
  difference is recorded as a **platform note in the findings**, not as a divergent header.
- The fork consumes the header as a **synced copy with a recorded pin** (commit + sha256),
  checked in CI. A submodule is the alternative; a synced copy is simpler for one file and
  fails loudly when stale.
- `docs/recon/findings/**` stays the home for recovered semantics. The fork adds Windows
  findings there; it does not fork the directory.

Everything else — `mesh/`, `ui/`, most of `engine/`, `tools/` — is shared code with a platform
split, and the fork tracks the parent as upstream.

## 3. Portability map

What actually needs work, from the current tree (the loader and CMake already carry the split
— this is not a guess):

| Layer | State | Windows work |
| --- | --- | --- |
| `engine/src/obscan_loader.cpp` | **already `#ifdef`-split** — `LoadLibraryA`/`GetProcAddress`/`GetLastError` vs `dlopen`/`dlsym` | point it at the Windows DLL path; the macOS app path is inside the Apple branch |
| `engine/CMakeLists.txt` | already has a `WIN32` branch | verify it compiles; the headers were built for clang |
| `mesh/` (Manifold, libigl, meshio) | portable, permissively licensed | build only; the `igl/copyleft` rule is unchanged |
| `ui/` | portable (web) | none |
| `tools/`, `tests/hardware/` | **`if(APPLE)` throughout** | port CMake + the worker/launcher split; the interpose-dylib mechanism is macOS-specific |
| `.claude/hooks/guard-hw-run.sh` | **bash**, POSIX path semantics | does not run on Windows — needs an equivalent before any hardware work |
| Recon artifacts (header offsets, findings) | recovered from the **macOS arm64** dylib | **a new recon target**, not a port — see §6 |

## 4. Branch and PR conventions

Mirror the parent's mechanics, so an operator switching machines keeps one mental model:

- **One worktree per role, one session per worktree.** A room belongs to the session in it;
  never share a checkout, never tear down someone else's room.
- **Branches: `<role>/<thing>`** — `recon/win-plan1`, `build/win-loader`, `hw/win-plan2`,
  `dispatch/land-win-recon1`.
- **Every change lands through a PR with a merge commit.** Never commit to `main`; never
  squash. `main` is protected.
- **Two gates, same shapes:** a deterministic `verify-pr` run in CI (a Windows port of the
  script) plus an `openscanner/verifier` verdict on the exact head commit for agent-tier
  paths. Port the tier list; do not redefine it.
- **Vendor artifacts are never committed.** `.recon-cache/` is gitignored in both repos; the
  Windows DLLs are a different target with their own sha256 pins, recorded in
  [`FORK-COORDINATION.md`](FORK-COORDINATION.md).
- **Run records** (`tests/results/**`) keep the parent's discipline: one file per run, never
  edited in place, corrections as dated addenda.

## 5. Coordination channel

The two machines talk through the OpenCode desktop app's **server list** plus
`scripts/remote-opencode.sh` (parent, PR #150). The Windows box is registered there as
`Windows11`; the script resolves the endpoint and credential from the app's state by display
name, so **neither the URL nor the credential is recorded in either repo.**

- **Who uses it: the dispatcher, not the planner.** The script's own header names its purpose —
  coordinating with "a dispatcher running on a second machine". Each side's dispatcher can read
  the other's sessions (`active`, `sessions`, `read`) and hand it work: `prompt` queues,
  `steer` exists only to stop a running agent before it commits something already known to be
  wrong.
- **Secrets.** Credentials are read at runtime and passed to `curl` through a stdin config,
  never `argv`, so they never reach `ps` or a transcript. Keep that property in anything built
  on it; never echo one, never put one in a prompt to either machine, never commit one.
- **What it is not.** Not a file channel and not an MCP server — it cannot write the other
  repo's files. That is exactly why §2's single-source rule and the coordination file's
  one-writer-per-file rule exist: the channel carries *orders and reports*, the repos carry
  *state*.
- **Conventions do not change with the channel.** A work order sent across machines has the
  same shape as a local one: the board row, the evidence, the deliverable, the wall, and what
  to report back.

## 6. Recon: the real cost

The parent's ABI claims were recovered from the macOS arm64 dylib, pinned per findings doc.
**None of that transfers silently.** A Windows build needs:

1. **The vendor's Windows artifacts.** Whether Creality ships a Windows build with the same
   `obscan_*` surface is **open question 1** — the gate on everything else. If no `obscan_*`
   DLL exists for Windows, this is not a port; it is the Orbbec-SDK path in `README.md`'s
   "Possible future direction".
2. **A fresh recon pass** against the Windows binaries — the C ABI is the same *interface*,
   but x64 MS calling convention ≠ AAPCS64, and struct layout/packing must be re-verified
   rather than assumed. A new target with new findings, not a reinterpretation.
3. **Its own pins** — sha256 per Windows artifact, in the coordination file.

Budget it as the parent's PLAN #1 was budgeted: one target per recon session.

## 7. Guardrails

The parent's hardware safety rests on a `PreToolUse` hook; on Windows that hook does not fire,
so **the fork starts with no hardware backstop at all**. Before any Windows hardware run:

- Port `guard-hw-run.sh` (or replace it) so the equivalent invariants hold: no manual
  library-path injection, no vendor-artifact edits, no listener tests outside the designated
  worktree.
- Keep the parent's hard rules verbatim — one SDK user at a time, no power-cycle shortcuts, no
  retry after a timeout, capture the exit code and any crash report.

## 8. What the fork needs before its first build

In order, each gating the next:

1. **Answer open question 1** — does a Windows `obscan_*` binary exist?
2. A Windows toolchain decision (MSVC vs clang-cl) and a CMake configure that gets `mesh/` and
   `engine/` compiling.
3. The contract pin wired into CI (fails loudly when the header is stale).
4. A PowerShell/Python equivalent of `guard-hw-run.sh`.
5. A first Windows recon target, with its own findings doc and pins.

## Open questions

1. **Does Creality ship a Windows build of the scan SDK with the `obscan_*` surface?** The gate
   on the whole fork. (operator / vendor inspection)
2. Is the Windows target x64 only, or is Windows-on-ARM in scope? Affects the name and the
   artifact set. (operator)
3. Does the parent's `#ifdef _WIN32` loader split actually compile against a real DLL, or was
   it written blind? It has never been exercised. (builder, once an artifact exists)
4. What replaces the interpose-dylib mechanism on Windows, if the teardown crash needs the
   same containment? (builder/recon)
5. Does the fork need its own agent definitions and worktrees, or does one shared config do?
   (operator)

--- END WINDOWS-FORK.md

---

--- BEGIN FORK-COORDINATION.md

# Fork coordination — `Open3DScanner` ↔ `Open3DScanner-Win64`

The two repos run independent loops and share one contract. This file is the cross-repo
coordination record: the contract pin, the platform registry, and an append-only log.

Owner: the parent's `openscanner/planner`. The fork keeps its own copy, seeded from this one.

## 1. The two-writer problem, and the rule

Both loops append to a log; neither can write the other's repo. So:

- **One writer per file.** This file is owned by the parent's planner. The fork keeps its own
  copy, seeded from here.
- **The parent's copy is canonical** for cross-platform decisions. A fork decision that
  affects the parent is folded back by the operator (or a fork→parent PR) — never by an agent
  writing across repos.
- **Entries are append-only, dated, and prefixed** `[mac]` / `[win]`. Appending is the only
  edit; nothing above the last entry is rewritten. That keeps a merge trivial.

## 2. The contract pin

The header is single-sourced in the parent (see `WINDOWS-FORK.md` §2). Each repo records the
revision it consumes and checks it in CI, so a stale copy fails loudly instead of quietly
diverging.

| Repo | Parent commit | `obscan_api.h` sha256 | Checked in CI |
| --- | --- | --- | --- |
| `Open3DScanner` (parent) | `main` (self) | regenerated per commit | n/a |
| `Open3DScanner-Win64` (fork) | _(unset)_ | _(unset)_ | not yet wired |

## 3. Platform registry

One row per artifact a platform's recon depends on. Vendor artifacts are never committed; only
their pins are.

| Platform | Artifact | Role | sha256 | Pinned by |
| --- | --- | --- | --- | --- |
| macOS arm64 | the parent's scan dylib | the parent's recon target | per its findings docs | parent findings |
| Windows x64 | _(none yet — open question 1)_ | | | |

## 4. Seeding the fork

The fork receives this file at repo-creation time with its **`[win]` seed entry already
written**, so the log exists as a log before anyone needs it and the fork's first dispatch has
somewhere to record. Seeded once, by whoever creates the repo; after that it is appended only
by the fork's own planner.

For the fork's copy: keep the parent's `[mac]` lines as history and add the seed below.

- `[win]` 2026-10-18 — repo `Open3DScanner-Win64` created; `WINDOWS-FORK.md` and this file
  seeded from the parent at `<parent-commit>` (the contract pin's parent commit, §2). Contract
  pin not yet wired. Blocked on: does a Windows `obscan_*` binary exist?

## 5. Log

_Append dated one-liners: what changed, which side, and the outcome. Newest last._

- `[mac]` 2026-10-18 — Windows fork chartered; repo named `Open3DScanner-Win64`; contract
  single-sourcing and the pin rule agreed. Blocked on: does a Windows `obscan_*` binary exist?
- `[mac]` 2026-10-18 — coordination channel confirmed live: `Windows11` registered in the
  desktop app's server list and answering (OpenCode 2.0.32); `scripts/remote-opencode.sh` named
  as the channel in `WINDOWS-FORK.md` §5.

--- END FORK-COORDINATION.md

---

# END STAGING
