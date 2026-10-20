# Dispatcher handoff — 2026-10-20

**Why this exists.** The dispatcher session is ~55 h old and has compacted; its model-side memory of
the project is a summary. The board is the durable memory. This note is the **queue** the next
dispatcher starts from — it is deliberately a list of requests, not a completion of them.

**Snapshot (as written):** `main` = the merge of #381; board rows run to **#159**; the open PRs and
session ids are below. Treat the board, not this note, as authoritative.

## 1. Merge what is green (verdicts at the exact head)

| PR | What | Verdict |
| --- | --- | --- |
| **#384** | row #148 — the architecture note | MERGEABLE @ `e6c0d1e` |
| **#376** | the 2026-10-20 HW batch (#127/#132 blocked, #141 split) | MERGEABLE @ `a7764e0` |

Merge with plain `gh pr merge --merge` after a CLEAN check — **auto-merge is disabled in this repo**;
`--auto` returns an error on a CLEAN PR. Script-tier PRs still need their `verdict` status from
`auto-verdict`.

## 2. One merge that stalls — investigate or fold in

**#383** (row #127's missing closing pipe) passes every check but the merge returns *"administrator
privileges"* (BLOCKED). If it will not merge, the one-character row fix can ride in the next board PR;
do not spend long on it.

## 3. PRs awaiting their next step

- **#382** — #158 runtime plane toggle. The verifier posted a "Re-check of row #158 PR #382" comment;
  read it, and if MERGEABLE, merge. Then #71's attempt 5 can run.
- **#364** — the integration tool. F1 (a split `--force-with-lease`) and F2 (a 3-digit-only row regex)
  are **fixed**; a re-read is already sent (head `b3768cf`).
- **#366** — #151 rate harness. NEEDS CHANGE; a re-read is sent (head `d329cb0`).
- **#375** — recon #150 (IMU unknowns). NEEDS CHANGE at `1380147`; read the finding, then either a
  small fix or a recon re-dispatch.
- **#374** — #152 performance contract. NEEDS CHANGE; **build-4 is actively fixing it** (last tool
  call within two minutes at handoff time — not stuck).

## 4. Sessions (address by id; they outlive this dispatcher)

| Session | id | Doing / for |
| --- | --- | --- |
| build-2 | `ses_eae046b7cffe7iOmFXdEHedrZF` | #151 |
| build-3 | `ses_eaa77bd4effeynzJ7pzx8kkpDH` | #359 (merged) |
| **build-4** | `ses_ea9cb5f84ffeXnSgYqKsilpyk9` | **#152 / #374 fixes — active** |
| build-5 | `ses_ea8dd7046ffehfYk2q44qXYz7P` | #153 |
| **build-6** | `ses_ea8cdc7f8ffeywidD70kT7NpIT` | #148 note — active |
| build-7 | `ses_ea8285cf7ffe48YD4q6v7qvy1f` | #158 |
| **build-8** | `ses_ea8219e6fffejxqZNfPbB28bH9` | **#159 experiment — active** |
| verify-5 | `ses_eaa9a6688ffeUri8vQ2gNKAqX3` | verifier |
| verify-6 | `ses_eaa77bd08ffecRj3iPy1i7CS8o` | verifier |
| recon-8 | `ses_ea97bfca0ffe5I7l5W04X2iT65` | #150 |
| recon-10 | `ses_ea84c6ddeffe5qcNzYA72vCIlD` | #144 |
| Win64 dispatch | `ses_eaa095eddffdunU4lrS1RMQvJA` (remote `--machine Windows11`) | the fork lane |

## 5. The next dispatches

1. **#157 — the live `start_stream` suffix defect. NOT YET DISPATCHED, and it is the key blocker.**
   The live client sends `ob_rpc_start_stream` unsuffixed; the daemon's methods are `…0-2-12` (IR) and
   `…0-2-10` (depth). Fix: route the live path by interface (as the replay already does) and fix the
   stop call at `main.cpp:1045`. This unblocks **#127** and **#132**.
2. **#158** lands with #382 → then #71 attempt 5 (single-session A/B).
3. **#159** (the confidence/retention experiment) is with build-8: design + offline harness + a
   synthetic self-test; the real curve waits on #126.

## 6. Hardware queue (the operator schedules; never gate on it)

- **#127** / **#132** — blocked on **#157**, not on the device.
- **#141** — the UI-open half is owed (no browser in that session); the no-client split is recorded.
- **#71 attempt 5** — after #158; the attempt-4 record says a two-process A/B is confounded, and the
  metric is the per-tick point-count series.
- **#126** — the ground-truth capture (wall 300/400/500 + checkerboard) needs the device **and** #157.

## 7. Rules in force (also in the agent files)

- The **operator owns the scanner** and its placement: no allocation planning, no ownership claims;
  ask for HW time, never gate on the device.
- **Visual analysis of a recording belongs to the hw session**; never dispatch an agent to re-analyse
  frames — take it as attributed evidence.
- Builders: `git fetch && git rebase origin/main` before the push; stop on a conflict.
- **Rebuilds and integrations run the full gate, not `--no-build`** — a hand rebuild once pushed a
  branch that did not compile (#359).
- Board rows are **append-only**; new rows go after the last row; every row ends with ` |`.
- A verdict is valid only at the **exact head** it names.
- `scripts/pr-integrate.py <PR>` is the dispatcher's integration step (row-aware, append-only, builds
  before pushing).
