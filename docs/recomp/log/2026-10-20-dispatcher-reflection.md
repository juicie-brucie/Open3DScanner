# Dispatcher reflection — 2026-10-20 (end of the first long-lived session)

**Session facts.** ~55 h open, **23.4 M cumulative input**, 1.6 B cache reads; compacted (the model-side
history is a summary; the record is not). The board, the logs and the PRs carried the memory — which is
the design working, not a failure.

## What the workload actually was

1. **Board and landing work** — every landing is: read the record/PR, check the gate, often rebuild on a
   moved `main`, re-point a verifier, merge. The rebuilds were the single largest avoidable cost:
   **seven in one night**, all from branches cut before a later merge. Two fixes landed: the builder
   rebase rule (`builder.md`) and `scripts/pr-integrate.py` (row-aware, append-only, builds before push).
2. **Record intake** — reading operator/HW records (some 60 KB, raw logs over 1 MB) and transcribing a
   capped-out session's work. This is the largest *context* spend, and the least delegable-looking — but
   it is delegable (see below).
3. **Verification churn** — verdicts expire when the head moves, and a rebuild moves the head. The
   re-reads are cheap *if* the scope is stated (code byte-identical, only the row changed); they are
   expensive if the verifier re-reads blind.
4. **Orchestration** — up to eight builders and two verifiers in parallel, each a session with an id,
   each needing a scoped order and a landing. The queueing is real work; the sessions outlive the
   dispatcher, which is why the handoff is a list of ids and states.

## Would spawned subagents for specific PRs help?

**Yes — for reading, not for deciding.** The dispatcher's scarce resource is context, and the two big
context sinks are record intake and PR triage. Both can be delegated:

- **Record intake subagent** (highest value): given a record path, return a structured summary — the
  numbers, the proposed board move, the caveats, and any claim that needs the operator's word. The
  dispatcher then checks two or three numbers against the record and lands. The subagent pays the read;
  the dispatcher keeps the judgment.
- **PR triage subagent** (read-only): for each open PR — head, verdict + whether it names that head, gate
  state, dirty/clean, and the one action it needs. The dispatcher acts. This replaces the manual
  sweep-before-a-round.
- **Rebuild/integration**: `pr-integrate.py` already automates the *work*; a subagent would only save the
  invocation. The build is CPU, not context. Keep it in the dispatcher's turn so the failure is seen.

**What should not be delegated**: the merge decision, the board moves, the walls (vendor binary, recon
findings, hardware records), and the verdict gate. A subagent that merges is a merge by proxy; the
verifier rule exists for the same reason.

## Model and role

The dispatcher is **process, judgment and bookkeeping**, not throughput-sensitive coding. Its failure
mode is *drift* on the rules (append-only rows, exact-head verdicts, walls), not slowness. That favours a
model that holds the rules steadily over a faster one; the agent file carries the rules and the board
carries the memory precisely so the model does not have to. A faster model would help the work orders and
the triage; it would not fix the rebuild churn, which is a process problem (now scripted).

## What to keep, what to change

1. **Keep** the handoff-as-queue and the board as the authoritative memory; a fresh session per round is
   viable when the handoff names the sessions, the PRs and the next three dispatches.
2. **Keep** the scope statement in every re-read ("code byte-identical, only the row changed") — it turns
   a re-verification into a check.
3. **Change** the landing shape: batch board rows with their records so one PR carries the move and the
   evidence, cutting the rebuild churn that dominated this session.
4. **Add** the two subagents above (intake, triage) before the next long round; they address the actual
   scarce resource, which is context.
5. **Watch** the first item in the handoff: **#157** (the live `start_stream` suffix) — it unblocks the
   next hardware round; the replay half can run without it.
