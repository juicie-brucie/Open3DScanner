# 2026-10-20 — dispatcher handoff (new session)

Written by the outgoing dispatch session
(`ses_ea7857cafffeHu7Fnrmx1bQFxj`). The new dispatcher takes the **dispatch
slot** (`~/DevReposiMac/Open3DScanner-wt-dispatch`, base `dispatch/next`).

## Read first

1. `docs/recomp/PLAN.md` — the board; this handoff only points at it.
2. `docs/recomp/ROADMAP.md` — rev 25, the priority map.
3. `docs/recomp/PR-QUEUE.md` — the landing queue (updated in this PR).
4. `docs/recomp/HW-TEST-QUEUE.md` — the hardware backlog (updated in this PR).
5. `python3 scripts/wt-roster.py check` — 12 slots, 0 legacy; `report` for the swarm.

## Live layout and sessions

12 worktrees, 0 legacy. **Do not interrupt** the active sessions:

- **plan** — `Plan — idle`; the plan slot is the main repo on `planner/next`.
- **recon1** — `Recon #161 — fix #414 scope`; active: restoring `PLAN.md` and
  rebasing the #414 branch (the finding is sound; only the stale board was the
  defect).
- **build3** — `Build #161 — live RPC timeout`; active: the timeout fix and its
  PR.
- **verify2** — `Verify PR #414 — #161 depth`; idle after the `NEEDS CHANGE`;
  re-read #414 at the new head after recon1 pushes.
- **build1/build2, recon2, verify1/3/4** — idle; verify1/3/4 are free; build1/2
  are the next builder slots. recon2's session was created with the now-missing
  `qwen3.7-plus#medium`; create any new recon session at `#high` explicitly.
- **hw** — external Claude Code, user-driven; dirty; untouched.

## Landed this round (recent)

#399–#413, #415. The ones that matter for the next round:

- **#392** (`969cb1a`): the 2026-10-20 records repair — #71 attempt 5 (NOT
  SUPPORTED), #123 frame-bins + raw log, ungated batch 2 as `_` support.
- **#412** (`87345fe`): the board/trust pass — #134/#155/#138 `DONE`, P9–P12
  (#162–#165) and the parked OQ (#166) filed, the OQ-register rule, the
  ARCHITECTURE roles table.
- **#413** (`8d377fe`): the coverage update fix (row #160) — `ui/coverage.js` +
  test; #160 `BUILD` -> `HW` in this PR.
- **#415** (`813adfe`): the queue refresh.
- Earlier: #399 (#93), #400/#401 (roster/tooling), #402/#404/#406 (roadmap),
  #403/#407 (board/salvage), #405 (#144), #408/#410 (HW intake), #409 (test
  queue), #411 (#127 live retry).

## Open / in flight

- **#414** — the depth-hang finding (`docs/recon/findings/depth-live-hang.md`).
  The verifier's only objection was the branch's stale `PLAN.md` (it reverted
  #412's rows and deleted #162–#166). Recon1 is fixing the scope; the finding
  itself is sound: **the live path's RPC calls lack a timeout**. Re-read at the
  new head; arm; merge. **Land this handoff/board PR only after #414**, so the
  #414 branch merges finding-only.
- **#161 build** — build3's timeout fix; the PR is pending. Verify and land;
  the depth retry is the hardware card.
- **This handoff/board PR** — the #160/#161 row moves, #167/#168 filed, the
  blocker notes, the queue and test-queue updates. Script-tier; land after #414.

## The new blocker from Claude's latest batch

- **#167 — the device-enumeration race.** `scan_server --live` logged
  `DeviceManager list: (1)`, then `get_device_num -> count=0`, then **SIGSEGV
  11**, 4-for-4 across #151 (2 runs), #71 attempt 6 and #93 markerless. **Every
  device-touching live start is blocked**, including the T2 A/B. The fix is a
  bounded retry/backoff on `get_device_num` in `scan_server`; **dispatch #167
  next**, to build1 or build2.
- **#168 — the `--capture` path hangs** the live client (IR, 2-for-2, zero
  output; `--dump-replies` removed and it still hung). A builder read of
  `tools/raptor_client/capture.cpp`; #132's IR/depth capture is blocked on it.
- **#161 — the depth timeout**; the build is running.
- The new crash/hang records are **not landed**. Provenance gaps: the 0-byte
  logs are the hang observation; `plan132-gap-free-capture-retry` has no raw
  log; the audit/ungated batches have no raw logs; no record carries a binary
  hash. They need `_` support or a repair; P12 (#165) is the systemic fix.

## Next actions, in order

1. `wt-roster.py check`; read the board/roadmap/queue/test queue; do not
   interrupt recon1/build3/verify2.
2. **#414**: recon1's scope fix -> verify2 re-read -> arm -> merge.
3. **This PR**: land the board/queue/test-queue updates after #414.
4. **#161 build**: build3's PR -> verify -> land; #161's depth retry card.
5. **#167**: dispatch the device-enumeration race fix (build1/build2). It is the
   gate on all remaining device tests.
6. **#168**: the `--capture` path fix (after #167 or in parallel if a builder is
   free); #132.
7. **T2 hardware** once #167 lands: the UI-open A/B closes #153's picture, #93,
   #141, #151's fused rate and #71; #76's coverage check rides it (#160 landed).
8. **T3 depth** once #161/#168 land; the #132 capture.
9. **The new records**: repair the provenance gaps and land them; P12 (#165) is
   the template/citation fix.
10. **P9–P12 / #166**: dispatch the device-free capacity set.
11. **T4/T5/T6** operator items: #145 + #150's captures; #126's known scene;
    the Windows #115.

## Decisions waiting on the user

- The **intake permission**: `dispatch.md` allows only `openscanner/verifier` as
  a subagent; the dedicated `openscanner/intake` is denied. Add
  `action: subagent`, `resource: "openscanner/intake"`, `effect: allow` to the
  agent file (outside the repo), or keep the verifier-with-intake-brief
  fallback. A new dispatch session picks up the permission at creation.
- The **recon agent file** pins `qwen3.7-plus#medium`, which the provider no
  longer offers; the live recon sessions run `#high`. One-line fix by the owner.
- **#167 vs #168 priority**: the race first; the capture path second.
- **The new records' provenance**: repair vs `_` support vs drop; the P12.
- The **`hw` room** is dirty (user-owned); the **Windows lane** and the operator
  captures are the operator's schedule.

## Queue

One landing at a time; no auto-merge is armed. Order: #414 (fix) -> this PR ->
#161 build -> #167/#168 -> the records -> P9–P12/#166. The T2/T3 hardware runs
wait on #167/#161/#168.

## Walls

No vendor binary, `.recon-cache/`, or decompiled output; the operator owns the
scanner and its timing; hardware records are the tester's; the dispatcher is the
board's single writer; one SDK user per run.
