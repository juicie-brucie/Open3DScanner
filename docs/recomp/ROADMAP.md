# Recomp roadmap

Forward strategy for the recon → build → hw → reconcile loop. Where
[`PLAN.md`](PLAN.md) is the live **queue** (one line per task, move the status as you
go), this file is the **plan**: the critical path, what is blocked and on what, the
risks, and the objective of the next round.

Owner: the `openscanner/planner` agent. The dispatcher consumes it and applies the row
moves it proposes. Statuses, claims and evidence live on the board — this file points at
them and never restates them.

_Last planned: 2026-10-19 (rev 17 — **the pivot**: the operator has set full host-side independence
from Creality Scan as the priority target, and the objective becomes its Phase A–D chain. rev 16's
objective landed (`G3`'s build row, `#91`/`#92`); rev 15 closed the walls audit; rev 14 moved the
objective to `G1`'s bind half and added `H4`; rev 13 corrected the `G1` misread and added `H3`; rev 12
recorded the `A2` decision; rev 11 added the app workstream; rev 10 moved the objective;
rev 9/8/7/6/5/4/3/2/1 are superseded — see **Superseded plans**)._

## Objective for the next round

**The pivot: own the host stack. Spec the protocol (Phase A), then the C1 client, then the scanning
math, then the daemon.**

The operator has set **full host-side independence from Creality Scan** as the priority target —
replacing OBScan's algorithms, then the Orbbec device library and the Creality daemon. It is assessed
as feasible and well-bounded in
[`FULL-INDEPENDENCE.md`](FULL-INDEPENDENCE.md), on two enabling findings: the device is **dumb**
(laser sequencing and auto-exposure are *host* loops, ~890 property SETs each, and depth is computed
on the host) and the app↔daemon protocol is **fully observed** on the wire. So the whole effort is
**observation-derived** — the cleanest provenance this project has had, and the reason the wall
posture below does not relax.

The order is fixed by dependency, not preference: **spec → C1 client → own the math → replace the
daemon** (§6, Phases A–D). Each phase leaves a working product, so the pivot is incremental by
construction, and the vendor path stays alive as an A/B reference until Phase D.

**Two rows gate it out of proportion to their size.** `#97` — the macOS privileged-access **decision**
(G-B) — can reshape C2 entirely; it is a decision rather than a build, so take it early and cheaply.
`#95` — the calibration payloads (G-C) — is the difference between a Phase C that uses *our*
parameters and one that uses the vendor's.

## Critical path

```
PHASE A            PHASE B                    PHASE C              PHASE D
#96 protocol spec ─G-A─► #98 C1 RPC client ──► #99 own the math ──► #100 replace the daemon
   (RECON)                    (msgpack+GVCP)     (gated by #95)        (gated by #97)

#95 calibration payloads ──G-C──┘  (gates a CORRECT C, not C's start)
#97 the macOS privileged-access DECISION ──G-B──┘                     (the biggest unknown)
#101 OQ-F2 · #102 OQ-F3 ──► capture-driven recon feeding A and D
```

The gates, and what each one blocks (§8):

| gate | is | blocks | owner |
| --- | --- | --- | --- |
| **G-A** | the protocol spec exists in findings | B — `#98` | recon, `#96` |
| **G-B** | the macOS UVC-claim mechanism decided | D / C2 — `#100` | operator, `#97` |
| **G-C** | the calibration payloads decoded | a *correct* C — `#99` (not C's start) | recon, `#95` |
| **G-D** | the laser + exposure loop inputs fully mapped | B's control half | mostly done; `#101` closes it |

Ranked by what it frees, not by board order:

1. **`#96` (G-A).** The pivot's only hard prerequisite — nothing in A–D moves without the spec, and it
   needs no new capture.
2. **`#97` (G-B).** The **single biggest unknown**, and a *decision*, not a build: own privileged
   helper vs system extension vs a thin remaining daemon. Cheap now, expensive if discovered late —
   it can reshape C2 entirely.
3. **`#95` (G-C).** Does not gate C's *start*; it gates C's *correctness*. Without it the owned
   pipeline runs on the vendor's parameters.
4. **`#98` → `#99`.** The build spine, in that order. At the end of B, OBScan and libOrbbecSDK are
   gone; at the end of C, so is the vendor's scanning math.
5. **`#101` / `#102`.** Capture-driven recon: `#101` closes G-D with three short single-action
   captures (a capture request for the operator), `#102` confirms no flash *writes* are required
   before C2 claims the device.
6. **`#100`.** Last, and gated on both `#98` and `#97`.

## Ready now

| Item | Role | What it needs |
| --- | --- | --- |
| `#96` — Phase A | recon | The protocol spec, from the captures. G-A. **The pivot's first work item.** |
| `#97` — G-B | operator | The macOS privileged-access decision, with reasoning. Cheap; it gates `#100`. |
| `#95` — G-C | recon | The calibration payloads: plane struct, thermal entries, the checksum (not CRC-32 or XOR). |
| `#98` — Phase B | builder | The C1 msgpack-RPC + GVCP client, after G-A. |
| `#101`, `#102` | recon / operator | Single-action captures for `#101`; a read-only capture audit for `#102`. |
| `#90`, `#92`, `#93` | builder | Wall-edit validation; two tracking-status drift fixes (`#93` is a live UI bug). |
| the owed reruns | hardware | `#65`, `#66`, `#71`, `#73`, `#76`, `#77`, `#89` — evidence on already-merged work; one room. |

## Blocked, and on what

| Row | Blocked on |
| --- | --- |
| `#98` | **G-A** — the protocol spec (`#96`). |
| `#99` | `#98`; and **G-C** (`#95`) for correctness rather than for start. |
| `#100` | `#98` **and** **G-B** (`#97`) — the privileged-access decision. |
| `#101` | Operator captures: one pause/resume, one mode switch, one slider. |
| G4, G5 | A fusion feature actually being planned — and **the pivot subsumes this**: Phase C replaces fusion. |
| the Windows lane | Nothing. Handed off by operator rule — see **Windows fork**. |
| G3's build | Nothing — `G2`'s feedback layer exists (`#76` is at `HW`). The row is **unfiled, not blocked**, which is why it is the objective. |
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
| **A3** | Lifecycle: spawn and supervise the worker, surface a crash, and make **quit = graceful `pause`/`stop`/`destroy`** | **`#65`** | **block cleared** — `#72`/`#73` landed; needs the launch rerun |
| **A4** | Settings and failure paths — missing-SDK dialog, port, session length | **`#66`** | **`HW`** — its live check rides the `#65` launch rerun |

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
- **`#94` and `#95` are one subject across two rows.** `#94` (`OQ-S15` — *where* the calibration
  lives) was dispatched to recon and has **no findings doc on main**; `#95` (the payloads decoded)
  draws on its open questions. If `#94`'s locating pass is what produced those questions, `#94` closes
  when `#95` opens its findings — otherwise they read as two open rows doing one job.

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

1. **`#96` — Phase A, the protocol spec** → recon: the RPC method set, the vendor-command framing
   (magic `0x47`/'MG', the 8-byte header, GET/SET_PROPERTY, the v1.1 list ops, memory-read opcode
   25), the property map, and the flash-read transport — from the captures. **G-A**, and the pivot's
   only hard prerequisite. **Wall:** `docs/recon/**`; the captures are evidence, the findings are
   what ships.
2. **`#98` — Phase B, the C1 client** → builder (after G-A): a msgpack-RPC + GVCP client reproducing
   a scan — open streams, pull IR, drive the laser and exposure loops, stop. At the end, OBScan and
   libOrbbecSDK are gone. **Wall:** `tools/`; no header change.
3. **`#95` — G-C, the calibration payloads** → recon: the laser-plane struct, the thermal entries,
   and the checksum (not CRC-32 or XOR — the device's lookup table is the lead). Gates a *correct*
   `#99`. **Wall:** `docs/recon/**`.
4. **`#97` — G-B, the macOS privileged-access decision** → operator: own helper vs system extension
   vs a thin daemon, with reasoning. **The single biggest unknown**, and cheap as a decision. Gates
   `#100`.
5. **`#99` — Phase C, own the math** → builder (after `#98`): laser-line extraction → triangulation →
   fusion → tracking, each A/B-tested against the vendor's output while it still exists.
6. **`#100` — Phase D, replace the daemon** → builder (after `#98` **and** `#97`): UVC + HID + the
   vendor channel, behind `#97`'s chosen shape.
7. **`#101` / `#102`** → operator + recon: three short single-action captures (pause/resume, mode
   switch, slider) to close G-D; a read-only capture audit to confirm no flash *writes* are required.

Also live and small: `#90` (the guard), `#92`, `#93` (a real UI debounce bug). And the **owed
reruns** — one hardware session covering `#65`, `#66`, `#71`, `#73`, `#76`, `#77`, `#89` — which stay
queued because they are the only open items that can still *fail*.

**Ordering note.** A→B→C→D is dependency, not preference. `#97` is deliberately placed early despite
gating D, because a late answer reshapes C2; `#95` runs parallel to A/B because it gates C's
correctness, not its start.

## Superseded plans

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
