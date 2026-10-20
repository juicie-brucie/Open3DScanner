# 2026-10-20 — board and trust pass (rev 25's P5)

Dispatcher session: `ses_ea7857cafffeHu7Fnrmx1bQFxj`. The swarm is unchanged
from `2026-10-20-roster-adoption.md`; the layout is 12 worktrees, 0 legacy.

## Rows closed

- **#134 — the #112 tick-latency audit** `SPEC-READY` -> `DONE`. The answer is the
  dated audit (`log/2026-10-19-row-134-tick-audit.md`) and the #147 baseline: no
  code cause; the instrument mismatch and the sleep-bound tick.
- **#155 — the full-pipeline stage map** `SPEC-READY` -> `DONE`. The map is
  `STAGES.md` (PR #396): every stage, its data, cost and owner.
- **#138 — IMU data nothing reads** `SPEC-READY` -> `DONE`. The record is decoded
  in `findings/imu-stream.md` (PR #356); the axis/temperature/gyro residuals are
  #150.

## Rows filed

Rev 25's proposals now have board rows:

- **#162 — P9, the flash-write falsifier** (`RECON`): a static read that can
  disprove #102's "no required persistent writes" claim; gates #100.
- **#163 — P10, the broadcast counters** (`SPEC-READY`): per-client send time,
  queue depth, drops and timeouts behind the `WsBroadcastServer` pimpl; retires
  #141's proxy and feeds #152.
- **#164 — P11, the S1 encoder** (`SPEC-READY`): the per-float encoder is the
  tick's hot path; a measured, allocation-free replacement.
- **#165 — P12, the record factory** (`SPEC-READY`): the generated citation line
  and a template that requires it; the condition on the room batch being citable.
- **#166 — the parked OQ pair (OQ-89a–d + OQ-S4)** (`TODO`): one adopting row
  beside #83–#88.

## Register rule

`PLAN.md`'s **Open questions** section now states the rule: **an OQ with a task row
is owned by that row; the register holds only the row-less ones.** Rev 25's
orphan 6 (the register was a subset of the truth) is closed by that sentence.

## ARCHITECTURE

`ARCHITECTURE.md`'s role table now has **eight rows** — the seven OpenCode agents
(`dispatch`, `planner`, `recon`, `recon-sweep`, `builder`, `verifier`, `intake`)
plus the external hardware tester — and its **Model** column points at the
agent file rather than restating stale IDs
(`~/.config/opencode/agents/openscanner/<agent>.md`). The "Why these models"
section stays as the cost-tier rationale, not a model-ID list. The dispatch
writes now include `scripts/**`; the intake row records its one approved write.

## Queue

- **#392** is the only open PR: draft HOLD, 59 behind; the three-record repair
  (frame-bins raw log, ungated batch raw log or `_` support, #71 archive
  citations) is with the hardware tester. Then rebase, undraft, re-gate, verify.
- **#160** (build1) and **#161** (recon1) are in flight: the coverage update
  trigger and the depth-hang diagnosis. Their PRs join the queue when they open.
- P9–P12 / #166 are queued behind them.

## Hardware state

- **T1b (live IR) is empirically green**: #157 DONE; `get_frame=33 data=30
  empty=3 error=0`, `Bin(2304000)`.
- **T3 (depth) is blocked** by the 10010 hang, now #161.
- **T2 (UI-open A/B)** is next for #153's picture, #93, #141, #151 and #71; its
  #76 coverage check waits on #160.
- The recon agent file still pins `qwen3.7-plus#medium`, which the provider no
  longer offers; the live recon sessions run `#high`. The file needs a one-line
  fix by its owner.
