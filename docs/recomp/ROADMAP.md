# Recomp roadmap

Forward strategy for the recon → build → hw → reconcile loop. Where
[`PLAN.md`](PLAN.md) is the live **queue** (one line per task, move the status as you
go), this file is the **plan**: the critical path, what is blocked and on what, the
risks, and the objective of the next round.

Owner: the `openscanner/planner` agent. The dispatcher consumes it and applies the row
moves it proposes. Statuses, claims and evidence live on the board — this file points at
them and never restates them.

_Last planned: 2026-10-17._

## Objective for the next round

**Drain the hardware queue, and open the scan family in parallel.**

Three rows are `HW` and ready to run (`#2`, `#17`, `#19`), and they share one manual,
serialized resource — the `wt-hw` room and the scanner box. Running them is the cheapest
form of progress available: every one of them is a code-complete change waiting only on
evidence, and each clears or contradicts a spec claim that other rows lean on. While that
runs by hand, recon is free and should start `#23`, the first target in a family with no
findings doc at all.

## Critical path

```
#19 (wire launcher) ─┐
#2  (data/process) ──┼─► hardware runs ─► OQ-P1…P4, #2 claims settled
#17 (OQ-4 thread) ───┘

#15 (causality + re-run design)
      └─► #11 (controlled re-run, remove-side race) ─► #20 (re-read .ips vs #5)
                                                            └─► #5 (narrow or drop "reproducibly")

#23 (scan family: get_raw_cloud) ─► first scan findings ─► #3, #4, OQ-5 (feeds PLAN #3)
```

The `#15 → #11 → #20 → #5` chain is the highest-leverage non-hardware work: `#15`
already decided the re-run must target the **remove-side race, not the re-add**, so
`#11` is runnable by design — but `#20` shows all three archived crash reports with the
main thread inside `context_destroy`, which contradicts `#5`'s "it never reached
`obscan_context_destroy()`". **Resolve `#20` before spending a hardware run on `#11`:**
a re-run designed against a causal story `#20` may overturn is a wasted serialized run.

## Ready now

| Row | Role | What it needs |
| --- | --- | --- |
| `#19` | hardware | Run `data_process_message_listener_test`; launcher wiring landed (PR #53). |
| `#2` | hardware | Same run settles the four loader exports + the six process ids; OQ-P1…P4 stay open. |
| `#17` | hardware | Per-phase thread lists landed in #40; a run settles OQ-4's async half. |
| `#20` | analyze / dispatch | Re-read the three archived `.ips` against `#5` and the crash analysis; cheap, no hardware. |
| `#23` | recon | Map `obscan_scan_get_raw_cloud`'s native body; deliverable is the first scan-family findings doc. |
| `#7` | recon / build | Follow-ups OQ-7b/c/d. |

## Blocked, and on what

| Row | Blocked on |
| --- | --- |
| `#11` | `#20`'s re-read (design validity) — then the serialized hardware room. |
| `#5` | `#11`/`#20`; a `[hw-contradicted]` tag is cleared by a run, not by argument. |
| `#3`, `#4` | Recon capacity; both are TODO with no spec yet. |
| `#21` | A user decision on tiering added run records; it is a gate-policy change, not an agent task. |

## Orphaned open questions

An OQ with no task row is unowned work — the spec claims that lean on it are load-bearing
on nothing.

- **OQ-P1…P4** — tracked in prose under `#2`, but `#2` is `HW`; if the run does not settle
  them, they need their own rows or they go quiet. Propose adopting them under `#2`'s
  outcome.
- **OQ-5** — `tips`/`control`/`calib` `*_name` jump tables; stated to feed PLAN #3, but no
  row exists. Propose a recon row.
- **OQ-1** — part `[inferred]` (`this+0x167`); the NULL/non-NULL half is HW-confirmed. The
  residual sits under `#1` (`DONE`) — flag it or it is lost.
- **OQ-6** — accepted as-is by decision, no task. Correct to leave unrowed.

## Risks

1. **One serialized hardware resource.** `#2`, `#17`, `#19` and later `#11` all queue
   behind one manual session in one room. Batch them per session and order them
   cheapest-first (`#19`/`#2` share a test; `#17` is a re-run of an existing program).
2. **Evidence provenance (`#21`).** Added records and logs under `tests/results/**` are
   script-tier, so a fabricated or mis-transcribed record auto-merges with no judgment
   pass. Records are the highest-stakes artifact class; decide before the hardware queue
   widens.
3. **A live contradiction on the board.** `#5` and `#20` disagree about whether the main
   thread reached `obscan_context_destroy` during the crashes. Until `#20` reads, any plan
   built on `#5`'s wording is built on a disputed claim.
4. **Verification debt.** Spec claims tagged `[inferred]` that code already depends on are
   real but unproven risk; the verifier flags them, and the hardware queue retires them.
   Keep the queue moving so the debt does not compound behind `#23`'s new family.
5. **Recon step budget.** `recon` is capped at `steps: 40`; one board row per session. A
   `#23` order that also asks for `#3`/`#4` context comes back empty — split it.

## Next round, as work-order seeds

For the dispatcher to expand — not dispatches in themselves.

1. `#20` → analyze/dispatch: re-read the three archived `.ips` against `#5` and
   `2026-10-17-plan8-device-message-listener-crash-analysis.md`; correct whichever is
   wrong. No hardware.
2. `#19` + `#2` → hardware: one session, one room, run `data_process_message_listener_test`;
   archive the raw log and any crash report per the record rules.
3. `#17` → hardware: re-run against the per-phase instrumentation; settle OQ-4's async half.
4. `#23` → recon: map `obscan_scan_get_raw_cloud`; deliver the first scan-family findings
   doc. Single row, single target.
5. `#21` → user: decide the tiering of added run records before the queue widens.

## Superseded plans

_None yet._
