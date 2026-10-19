# Recomp roadmap

Forward strategy for the recon → build → hw → reconcile loop. Where
[`PLAN.md`](PLAN.md) is the live **queue** (one line per task, move the status as you
go), this file is the **plan**: the critical path, what is blocked and on what, the
risks, and the objective of the next round.

Owner: the `openscanner/planner` agent. The dispatcher consumes it and applies the row
moves it proposes. Statuses, claims and evidence live on the board — this file points at
them and never restates them.

_Last planned: 2026-10-18 (rev 14 — the app is built and running, so the objective becomes the
defects that *running* it exposed (`#72`/`#73`/`#74`); adds **`H4`**, session compaction, with the
measured cycles and the `opencode.json` decision; and refreshes every row claim against the board,
per `H3`. rev 13 corrected the `G1` misread and added `H3`; rev 12 recorded the `A2` decision;
rev 11 added the app workstream; rev 10 moved the objective; rev 9/8/7/6/5/4/3/2/1 are superseded —
see **Superseded plans**)._

## Objective for the next round

**Make the app behave like an app: the three defects that only appeared once it ran (`#72`, `#73`,
`#74`).**

`A1` and `A2` are `DONE`, so the shell exists and launches. What it does *not* do yet is behave
like an application: **`#72`** — bundled, its own `ui/` lookup resolves relative to a cwd of `/`,
so every request 404s forever, silently; **`#73`** — ⌘Q does not terminate it and the SIGTERM
teardown never fires; **`#74`** — a set-but-missing `OPENSCANNER_LIBRARY` is silently skipped, so
the missing-SDK dialog cannot name it. Three `BUILD` rows, all found by *running* the thing rather
than reading it, and two of them are the app's most basic contract with the OS.

**`#73` also unblocks `A3`** (`#65`, `BLOCKED`): the lifecycle row *is* the teardown path that
never fires, so it waits on the fix rather than on design.

Two threads queue behind it, non-competing: **`G1`'s bind half** (`#71`, `BUILD` — the toggles into
the live path, per `#59`'s verdict) and **`G2`'s semantics** (`#75`, `RECON`).

## Critical path

```
#72 · #73 · #74 (BUILD — the defects running the app exposed) ─► it behaves like an app
                                    └─ #73 ─► A3 (#65, BLOCKED — the lifecycle teardown)

#71 (BUILD — G1's bind half: the toggles into the live path)     #75 (RECON — G2's semantics)
#66 (HW — A4, awaiting its run)

A1 · A2 ✅ DONE     H1 ✅ landed     #53 · #59 · #62 · #64 · #67 ✅ DONE
G1's REIMPLEMENT half is still UNFILED — #71 binds the toggles; the decision stays the vendor's.
```

Ranked by what it frees, not by board order:

1. **`#73`.** ⌘Q not terminating is the app's most basic failure — and it is the blocker on `A3`,
   so one fix frees two rows.
2. **`#72`.** The bundled app cannot find its own UI; every request 404s silently, with no error
   to chase. Fixing it is what makes the other two observable.
3. **`#74`.** Small, but it is the difference between "the missing-SDK dialog names the problem"
   and "the override silently does nothing".
4. **`#71`.** G1's bind half, per `#59`'s verdict — the reconstruction workstream's first
   landed capability.
5. **`#75`.** G2's semantics, re-derived because the corpus is frozen. Recon; no hardware.
6. **`#66`.** A4's acceptance run — hardware, and it queues behind nothing above.

## Ready now

| Row | Role | What it needs |
| --- | --- | --- |
| `#72`, `#73`, `#74` | builder | The app's run-discovered defects: the `ui/` cwd lookup, ⌘Q not terminating, the silent library override. |
| `#71` | builder | G1's bind half — the toggles into the live path. The header already declares them (`#59`). |
| `#75` | recon | G2's semantics, re-derived — the corpus is frozen. No hardware. |
| `#66` | hardware | A4's acceptance run. |
| G1's **reimplement** half (unfiled) | builder | The verdict says the vendor's *decision* is what the operator complained about. Needs its own row, or `#71` will quietly absorb it. |

## Blocked, and on what

| Row | Blocked on |
| --- | --- |
| `A3` (`#65`) | `#73` — the teardown path that never fires *is* the row's subject. |
| G3 | `G2` — the feedback layer it needs to display per-pose reasons. |
| G4, G5 | A fusion feature actually being planned; a denoise/fusion harness measures something that does not exist yet. |
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
| **G1** | Live plane removal for **resource management** (cut data per frame during capture), not a post-scan cosmetic cut | — | **adoption `#59` `DONE`**; **build row open** — bind the toggles, reimplement the decision |
| **G2** | Coverage heatmap: replace the one-scalar "insufficient data" verdict with a spatial under-covered set | nothing — `#40` landed | ready to row |
| **G3** | Owned tracking (marker detection + bundle adjustment) with per-pose rejection reasons | `G2`'s feedback layer | blocked |
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
| **A3** | Lifecycle: spawn and supervise the worker, surface a crash, and make **quit = graceful `pause`/`stop`/`destroy`** | **`#65`** | **`BLOCKED` on `#73`** — the teardown path that never fires is the row's own subject |
| **A4** | Settings and failure paths — missing-SDK dialog, port, session length | **`#66`** | **`HW`** — built, awaiting its run |

**Running the app found the rest.** `#72` / `#73` / `#74` are not design gaps: they are what a real
bundle discovers when it resolves its own paths and answers the OS. They are the family's next work,
ahead of `A3` and `A4`.

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

### H2 — Reconcile the dispatcher's documented model

`ARCHITECTURE.md` documents the dispatcher's model as `opencode/nemotron-3-ultra-free`,
justified by a long-context tier. The live dispatch session is running
`deepseek-v4.1-flash#max`. One of the two is stale. It matters more than a typo because the
original choice was justified *by context capacity* — the same property `H1` protects. Fix
the doc or the config, and record the reason where the choice is recorded.

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
8. **The dispatcher's own context — mitigation applied, and now measurable.** One long-lived
   session on a 1M window, compacting roughly every **10–15 h**, which puts its verbatim memory
   horizon at about half a day. The failure mode is judgment drift on a summary, not a crash, and
   this repo has paid for it once (rev 7 → rev 8). Board compaction (`H1`) is landed; session
   compaction (`H4`) carries the measurement, the config decision, and the cycle-3 test.

## Next round, as work-order seeds

For the dispatcher to expand — not dispatches in themselves.

1. **`#73`** → builder: make ⌘Q and window-close reach the graceful `pause`/`stop`/`destroy`
   path. **Wall:** builder edits the app shell (`tools/macos_app/`) and `engine/`. It is the
   blocker on `A3` (`#65`), so landing it frees two rows.
2. **`#72`** → builder: resolve `ui/` from the bundle rather than the process cwd, so a launched
   `.app` (cwd `/`) serves its own files instead of 404ing silently.
3. **`#74`** → builder: make a set-but-missing `OPENSCANNER_LIBRARY` fatal, so the missing-SDK
   dialog can name it. Small, and it is what makes `A4`'s dialog useful.
4. **`#71`** → builder: G1's bind half — the three plane toggles into the live path, per `#59`'s
   verdict. **Wall:** `tools/` + `engine/`; read the header (`obscan_api.h:748-767`), do not edit
   it. **And file G1's reimplement half separately** — `#71` binds the toggles only, and the
   operator's complaint is about the vendor's decision, which `#71` leaves untouched.
5. **`#75`** → recon: G2's semantics, re-derived — the corpus is frozen. No hardware; one target.
6. **`#66`** → hardware: A4's acceptance run. Batches with nothing above.

And one repo-side housekeeping order — **`H4`'s change-set**, which the dispatcher lands:

7. **`opencode.json` + the tier fix** → dispatcher: add the repo-root `opencode.json`
   (`compaction.auto: true`, `keep.tokens: 60000`; no mode change, no `buffer`) and add
   `opencode.json`/`opencode.jsonc` to `scripts/verify-pr.py`'s agent tier **in the same PR**, so
   the config that steers every agent here does not auto-merge with no judgment pass. **Wall:** a
   new root file plus `scripts/**` — agent-tier, so it needs a verifier verdict.
   The `## Context` section for the **global** `dispatch.md` is **not** part of that PR: the repo
   does not carry agent definitions, so whoever owns the harness applies it, **editing the agent
   file before the next compaction** (a compaction re-baselines the session against current
   instructions).

## Superseded plans

- **2026-10-18 rev 13 → rev 14 — the objective moves to the running app, plus `H4`.** `A1`/`A2`
  landed, so "ship the shell" is done; what is open is the defects *running* it exposed
  (`#72`/`#73`/`#74`), and `#73` blocks `A3`. Rev 14 also adds **`H4`** — the measured compaction
  cycles, the `opencode.json` decision, the `verify-pr.py` tier fix, and the cycle-3 test — and
  replaces Risk 8's prose with a pointer to it. Every row claim was re-read against the board first
  (`H3`), because rev 13's were stale by 61 commits. Reversal reason: the app moved from "built" to
  "run", which changed what the next work is.
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
