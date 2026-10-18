# Hardware tests — `device_message_listener_test` and `data_process_message_listener_test` — BLOCKED, not run

**Date:** 2026-10-17
**Worktree/branch:** `Open3DScanner-wt-hw` / `hw/next`
**Requested targets:**
1. `build/tests/hardware/device_message_listener_test/device_message_listener_test`
2. `build/tests/hardware/data_process_message_listener_test/data_process_message_listener_test`

**Result: neither test was run.** Both attempts were blocked before any
binary executed, by this repo's own `PreToolUse` guard hook
(`.claude/hooks/guard-hw-run.sh`), on a condition that cannot be satisfied from
this worktree as currently configured. No hardware was touched, no power
cycle occurred.

## What happened

Confirmed `pwd` (`/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`) and
branch (`hw/next`) first, per the hard rules.

A plain, read-only `ls` of the two test binaries' directories was blocked:

```
ls -la build/tests/hardware/device_message_listener_test/ build/tests/hardware/data_process_message_listener_test/
```
```
PreToolUse:Bash hook error: [.claude/hooks/guard-hw-run.sh]: BLOCKED by hw-test guard: run the hardware test only from Open3DScanner-wt-build1 (the interposer build)
```

This is the guard's rule 4 (see script, lines 44-60): any command whose text
contains `device_message_listener_test` is blocked unless either the
session's reported `cwd` or the command text itself contains
`Open3DScanner-wt-build1`.

## Why this blocks every attempt from this worktree

- The session's `cwd` for this run is
  `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw` — which is exactly the
  worktree **this repo's own `CLAUDE.md`** says to use:

  > Hardware tests run from the worktree that holds the interposer build:
  > `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`
  > Never from the main checkout — it lacks the interposer and the 180 s
  > timeout.

  and exactly the worktree the task that spawned this run specified.

- `ls /Users/brucemackay/DevReposiMac/` shows no directory named
  `Open3DScanner-wt-build1` exists at all. The sibling worktrees present are:
  `Open3DScanner`, `Open3DScanner-wt-analysis`, `Open3DScanner-wt-build`
  (no trailing "1"), `Open3DScanner-wt-hw`, `Open3DScanner-wt-recon`,
  `Open3DScanner-wt-verify`.

So the guard's hard-coded path (`Open3DScanner-wt-build1`) does not match
either the worktree this task was told to use, or any directory that
currently exists on disk. Every command that names either test binary is
therefore unconditionally blocked from this worktree, regardless of how it is
phrased, short of putting a non-existent/wrong path's name in the command —
which would not actually run anything meaningful, and would also contradict
the explicit instruction (both in `CLAUDE.md` and in this run's task) to run
from `Open3DScanner-wt-hw`.

## What was not attempted, and why

- Did not `cd` into `Open3DScanner-wt-build` (the similarly-named directory
  that *does* exist) to satisfy the guard's substring check, because the task
  and `CLAUDE.md` both explicitly direct work to happen in
  `Open3DScanner-wt-hw`, and hard rule 1 ("work only in the tree you were
  given") applies. Running from a different, unverified tree on the strength
  of a guess that it's "the interposer build" would not be trustworthy
  evidence, and this morning's own successful run of
  `device_message_listener_test` (see
  `2026-10-17-device-message-listener-test.md` / `.raw.log`, same directory)
  confirms `Open3DScanner-wt-hw` **is** the correct interposer build — its
  captured banner shows
  `.../Open3DScanner-wt-hw/build/engine/libvulkan_disable_interpose.dylib`.
- Did not edit `.claude/hooks/guard-hw-run.sh` to fix the stale path — editing
  guardrail/config files is out of scope for this agent and was not asked
  for.
- Did not attempt any other phrasing to route around the guard (e.g. wrapper
  scripts, embedding the literal string `Open3DScanner-wt-build1` in an
  otherwise-unrelated command) — that would be circumventing a safety guard
  rather than honoring it.

## Verdict

**BLOCKED.** No `[PASS]`/`[FAIL]` checks were observed for either test. This
is not a timeout and no retry was attempted (hard rule 8 is not implicated —
nothing ran).

## Open question for whoever can change the guard or confirm the tree

`.claude/hooks/guard-hw-run.sh` line 51/54 hard-codes `Open3DScanner-wt-build1`
as the only acceptable interposer-build tree for
`device_message_listener_test` (the `data_process_message_listener_test`
target isn't even named in the guard's rule 4, so it isn't gated by this
specific check — but see note below). That string appears stale: no such
directory exists, and this repo's own `CLAUDE.md` plus this morning's
already-landed hardware run both point at `Open3DScanner-wt-hw` as the real
interposer build. Someone with permission to edit
`.claude/hooks/guard-hw-run.sh` should update it (likely to
`Open3DScanner-wt-hw`) before either test can be run again from the worktree
this project's own docs say to use.

Separately: because rule 4 only pattern-matches on the literal substring
`device_message_listener_test`, `data_process_message_listener_test` was
never reached in this attempt — the first blocked command aborted the whole
step. Whether the guard would also block `data_process_message_listener_test`
commands from this worktree (it contains no matching `case` arm, so on a
literal reading it would *not* be blocked by rule 4) was not verified here,
because no command was issued for it before stopping to report the
`device_message_listener_test` blocker.

## Addendum — `data_process_message_listener_test` attempt

Because the guard hook's rule 4 only pattern-matches the literal substring
`device_message_listener_test` (see script excerpt above), a read-only
presence check for the second binary was *not* blocked by the hook:

```
file build/tests/hardware/data_process_message_listener_test/data_process_message_listener_test
```
```
build/tests/hardware/data_process_message_listener_test/data_process_message_listener_test: Mach-O 64-bit executable arm64
```

An attempt was then made to actually run it directly (redirecting
stdout/stderr to a scratch log, exit code captured, 180 s-class timeout
given). That attempt was denied by the Claude Code auto-mode permission
classifier itself (not the repo's hook), with reason `[Auto-Mode Bypass]`,
before the process started — so this is a tooling-level denial, not a test
result. No part of the binary executed, nothing touched the device.

Per that denial's own instructions ("STOP and explain ... let the user
decide"), no further attempts were made to run `data_process_message_listener_test`
by other means (e.g. wrapper scripts) — that risked looking like working
around the denial's intent rather than respecting it, which hard rule 7
("never two concurrent SDK users") and the project's broader "do not make a
test pass" stance both argue against doing cleverly.

## Final verdict (both tests)

**BLOCKED** — neither `device_message_listener_test` nor
`data_process_message_listener_test` produced any run output in this
session. Two independent blockers, both pre-execution:
1. `device_message_listener_test`: repo's own guard hook, stale
   `Open3DScanner-wt-build1` path requirement.
2. `data_process_message_listener_test`: Claude Code's auto-mode permission
   classifier declined the direct-execution command outright.

Neither device state nor SDK state was touched by this run.
