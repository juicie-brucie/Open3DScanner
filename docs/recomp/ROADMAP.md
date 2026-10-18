# Recomp roadmap

Forward strategy for the recon → build → hw → reconcile loop. Where
[`PLAN.md`](PLAN.md) is the live **queue** (one line per task, move the status as you
go), this file is the **plan**: the critical path, what is blocked and on what, the
risks, and the objective of the next round.

Owner: the `openscanner/planner` agent. The dispatcher consumes it and applies the row
moves it proposes. Statuses, claims and evidence live on the board — this file points at
them and never restates them.

_Last planned: 2026-10-18 (rev 12 — records the `A2` decision with the three consequences it
creates, files A2–A4, marks `A1` and `H1` landed, folds in the `OQ-7b`/`OQ-S6` answers, and
sweeps the board after the stall-test and app rows. rev 11 added the app workstream and the
loop-hygiene items; rev 10 moved the objective to the reconstruction workstream;
rev 9/8/7/6/5/4/3/2/1 are superseded — see **Superseded plans**)._

## Objective for the next round

**Ship the shell: `A2` — the `.app` bundle and the WKWebView window — whose only gate the
operator has now resolved.**

The app family is the live workstream. `A1` landed (PR #177), so the client already has an
origin the app owns; `A2` is the row that turns that into a double-clickable app, and **its
SDK-bundling gate is answered** (see **The `A2` decision** — the operator confirmed it twice).
That makes it the cheapest unstarted row on the board. `A3` and `A4` follow, with `A3`
inheriting `OQ-S6`'s answer rather than its question.

**`G1`'s adoption row is a standing debt, not a second track.** It has been proposed in rev 10,
rev 11 and now rev 12 and never filed. Either file it this round or drop the goal deliberately:
a proposal that survives three revisions unactioned is the orphan failure this file warns
about, and re-proposing it a fourth time is not planning.

## Critical path

```
A2 (bundle + WKWebView — gate answered) ─► A3 (lifecycle) ─► A4 (settings/failure paths)

A1 ✅ landed (#177)        H1 ✅ landed (#178: PLAN.md 79,660 → 17,758 bytes)
OQ-S6 ✅ answered from the existing records — no run owed, no row owed
#62 ✅ answerable by `smoke_test` as it stands — no build change needed

#53 (HW — the WebGPU-rejection path only, app-relevant) ─► one browser run
G1 (live plane removal) ─► NO ROW, third revision running → file it or drop it
G2 (coverage heatmap) ─► build (prereq #40 landed) ─► G3 (owned tracking, per-pose reasons)
```

Ranked by what it frees, not by board order:

1. **`A2`.** The only unstarted row whose gate is answered, and the one that makes the app real.
   Everything in the family waits behind it.
2. **`G1`'s adoption row — file or drop.** It blocks nothing, but it has been proposed three
   times. Filing is one dispatcher action; dropping frees the roadmap of a debt it keeps
   carrying. Silence is the only option that costs without buying.
3. **`#53`'s last item.** The WebGPU-*rejection* fallback: cheap, browser-side, and app-relevant
   — a bundled WebView meets exactly that machine.
4. **`#62`.** Now the cheapest hardware item on the board: `smoke_test` already never registers
   the device-message listener and prints the device count, so the answer needs a run, not code.
5. **`G2`.** Its prerequisite landed with `#40`; it is the only G-goal that unblocks another.

## Ready now

| Row | Role | What it needs |
| --- | --- | --- |
| `A2` (unfiled) | builder | The `.app` bundle + a WKWebView window. **Gate answered** — see **The `A2` decision**; carries the search-list and dylib-pin checks. |
| `A3`, `A4` (unfiled) | builder | Lifecycle and the failure paths; both after `A2`. `A3` inherits `OQ-S6`'s answer rather than its question. |
| `G1`'s adoption row (unfiled) | recon | Third revision running — **file or drop, deliberately**. |
| `#53` | hardware / browser | One item left: the WebGPU-**rejection** fallback. App-relevant. |
| `#62` | hardware | OQ-7b's listener-less with-device run — `smoke_test` answers it as it stands. |
| `G2`'s build row (unfiled) | builder | The under-covered point set painted on the live cloud. `#40` landed; unfiled, not blocked. |

## Blocked, and on what

| Row | Blocked on |
| --- | --- |
| `A3`, `A4` | `A2` — they extend the shell it creates. |
| `G1`'s build row | `G1`'s adoption row — recon before build, per the corpus rule. (Neither exists yet.) |
| `G3` | `G2` — the feedback layer it needs to display per-pose reasons. |
| `G4`, `G5` | A fusion feature actually being planned; a denoise/fusion harness measures something that does not exist yet. |
| the Windows lane | Nothing. Handed off by operator rule — see **Windows fork**. |

## Orphaned open questions

An OQ with no task row is unowned work. The previous sweep tracked a stale subset; this one
reads every findings file's open list.

**Dead — closed, keep off the list:**

- **OQ-1** — resolved by `#50`; **OQ-5** by `#47`. Both are answered in their findings.
- **OQ-4** — closed by `#52`.
- **OQ-S12** — answered by `#60`: the pose trigger is a real export and is declared, so the
  "declare vs document" question never had to be taken.
- **OQ-S13**, **OQ-S14** — closed by `#58`.
- **OQ-S4** — deferred to the marker pass by decision; not yet live.

**Live, unowned — each gets an owner or an explicit trigger.** A "park" without a trigger is
unowned work with a nicer name, so every row names both the condition that revives it *and* the
cheapest thing that retires it.

| OQ | Trigger — when it becomes live | Cheapest retirement |
| --- | --- | --- |
| **OQ-S6** | **answered 2026-10-18 from the existing records**: the tail fits the watchdog budget with room to spare (the stall test's teardown ≈ 11 s against 600 s; the 15.6M-vertex case ≈ 19 s) | **retired — no run owed.** `A3` designs against the answer |
| **OQ-7b** | **answerable as it stands** — `smoke_test` never registers the device-message listener and prints the device count | `#62`: a run of an existing tool. **No build change, no skip-listener flag** |
| **OQ-S2** | a consumer wants the cloud *without* the color getter (a pause-time snapshot) | recon batch with the next scan-family row |
| **OQ-S3** | a torn or partial cloud is observed, or a second reader is added | recon: read both lock sites statically |
| **OQ-S7** | a consumer needs *absolute* world orientation (a world-axis grid, or `G3`'s pose graph) | one `get_global_pose` at a known device pose |
| **OQ-S9** | a torn or frozen cloud is observed at the update cadence | double-call loop during an active scan |
| **OQ-S10** | a consumer needs *intrinsics* (`G3`'s tracking, or a calibration workflow) | analyzer pass on the session DB row, or a runtime hexdump |
| **OQ-S11** | a consumer wants the marker disc as a viewer overlay | entry-register capture + a small-`n` hexdump |
| **OQ-7c** | a second sensor line is supported | static string/symbol read |
| **OQ-7d** | only its parent question, `#52`, which is closed | **verify against `#52` and close it** |

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

The client is already built; what was missing is **a shell and a transport**. There is no
packaging anywhere (no `MACOSX_BUNDLE`, no `Info.plist`), and the UI is not served by the app —
`README.md` requires a separate `python3 -m http.server`, and `file://` is explicitly unreliable
there. `docs/architecture/scan-viewer.md` §2 named the destination (a web client plus a **thin
native shell**), so this is a decided direction, not a new one. The A-labels are the planner's;
the dispatcher files the rows.

| # | Foundation | Board row | State |
| --- | --- | --- | --- |
| **A1** | Serve `ui/` from the capture process — one origin, no external static server | **`#61`** | **landed** (PR #177, `42c2366`); the row moves to `DONE` in the next board PR |
| **A2** | The `.app` bundle + a WKWebView window (`MACOSX_BUNDLE`, `Info.plist`, icon) | none — **file it** | **gate answered by the operator** (see below); spec-ready |
| **A3** | Lifecycle: spawn and supervise the worker, surface a crash, and make **quit = graceful `pause`/`stop`/`destroy`** | none — **file it** | after `A2`; **inherits `OQ-S6`'s answer** |
| **A4** | Settings and failure paths — missing-SDK dialog, port, session length | none — **file it** | after `A2` |

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

## Loop hygiene (H1–H2)

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
   before a build row is committed — and it is also the row that keeps failing to get filed.
2. **The user's dylib may not be the slice we specced — new, and created by the `A2` decision.**
   Every ABI claim in the header was recovered from **one pinned slice** (the sha256 recorded in
   the [`device-message-listener`](../recon/findings/device-message-listener.md) findings).
   Requiring the user's own install means a newer CrealityScan supplies a *different* dylib and a
   **silently reassigned ABI** — the exact failure this whole recomp exists to prevent. The
   loader today checks only that the path exists. Cheapest retirement: verify the hash at load
   and refuse on mismatch — `A2`'s third consequence.
3. **Evidence provenance (`#21`) — closed 2026-10-18, one residual.** Added records now need a
   verifier verdict (agent-tier) and a non-`.md` companion raw artifact in the same PR. The
   residual is a *plausible-but-false* record surviving a verdict; the only thing that retires
   it is a spot re-run against the artifact, so treat a record as evidence of a run, not proof.
4. **A second evidence corpus outside the gate.** `logs/**` is tracked and is the evidence
   base for the corpus's §1, but it is neither `tests/results/**` nor `docs/recon/**`, so the
   `run-records` check and the tier rules do not reach it. Cheapest retirement: an operator
   decision on whether it stays.
5. **One serialized hardware resource.** `#53`'s rejection-path test and `#62` queue behind one
   manual session. Batch per session, cheapest-first.
6. **Recon step budget.** `recon` is capped at `steps: 40`, one board row per session — so
   `G1`'s adoption, if it is filed, is a session of its own and must not carry adjacent context.
7. **Verification debt.** Claims tagged `[inferred]` that code depends on: the parked
   `OQ-S7/S9/S10/S11` residuals, `OQ-S2`/`S3`, and the tips/control rows `#9` left `[inferred]`.
8. **The dispatcher's own context grows monotonically — mitigation applied, not closed.**
   Measured 2026-10-18: one 21-hour session, 303 messages, 6.37M fresh input, 484M cached read,
   against a board that was 91% archive. `H1` has now cut the board to 17,758 bytes, so the
   biggest fixed cost is down ~4.5×; the *drift* risk remains and should be re-measured after a
   few rounds rather than declared retired.

## Next round, as work-order seeds

For the dispatcher to expand — not dispatches in themselves.

1. **`A2`** → builder: the `.app` bundle + the WKWebView window. Its gate is resolved, so this
   is the cheapest unstarted row. It carries the three consequences named in **The `A2`
   decision** — the macOS search-list, the missing-app dialog (shared with `A4`), and the
   **dylib-pin check**. **Wall:** builder edits `app/`, `engine/`, `CMakeLists`; no header
   change, so no recon precedes it.
2. **`G1`'s adoption row — file it, or drop the goal** → dispatcher, then recon if filed. Third
   revision running. Filed: recon adopts the corpus's plane-removal ABI + decision-fn, answers
   the **re-homing question**, and gives a **bind vs reimplement** verdict — one target, one
   session, **Wall:** recon owns the header and `docs/recon/**`. Dropped: say so, and the goal
   leaves this file rather than being re-proposed.
3. **`#53`'s rejection path** → hardware / browser: force WebGPU to reject and confirm the
   fallback reaches WebGL2 rather than dead-ending. One browser-side run, and it is
   app-relevant — a bundled WebView meets exactly that machine. **Wall:** `tests/results/**`.
4. **`A3`** → builder (after `A2`): spawn and supervise the worker, surface a crash, make quit
   the graceful teardown. It inherits **`OQ-S6`'s answer** — the tail fits the budget by a wide
   margin (~11 s against 600 s in the stall test; ~19 s on the 15.6M-vertex case) — so it designs
   against that, and owes neither a run nor a row.
5. **`#62`** → hardware: `smoke_test` already never registers the device-message listener and
   prints the device count, so this is a run of an existing tool — **no build change, no
   skip-listener flag**. Batch it with (3).

Also ready, non-competing: `G2`'s build row (unfiled but unblocked — `#40` landed), and `#61`'s
row move if its acceptance is complete.

## Superseded plans

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
