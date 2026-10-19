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
| `Open3DScanner-Win64` (fork) | `dac0e7dedd6674335d7b4abb4e65f9efd540ccbc` | `e6946c5e3110f179d4ac090e2c371d145bd108e2b068557f0ca56bbecd09018c` | not yet wired |

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

- `[mac]` 2026-10-18 — repo `Open3DScanner-Win64` created (standalone, private) and seeded from the
  parent at `dac0e7d`; its copy of this file carries its `[win]` entry, and both copies' §4 now
  carry the room-hygiene rule. The §2 pin row for the fork stays unset until the fork wires its
  check.
- `[mac]` 2026-10-18 — contract pin set: `obscan_api.h` @ `dac0e7d` = 52547 B, sha256
  `e6946c5e3110f179d4ac090e2c371d145bd108e2b068557f0ca56bbecd09018c`. The Windows lane
  computed the same value independently; both agreed to the byte. The fork's CI check is
  still to wire.
- `[mac]` 2026-10-18 — boundary note: everything the parent's side has written in the fork is the
  **operator-directed bootstrap** (creation, seed, `[win]` entry, pin row, bootstrap note).
  From here the parent's dispatcher writes only the parent's repo; cross-repo changes arrive
  as requests over the coordination channel.

- `[mac]` 2026-10-18 — the fork was made **public** (operator's decision; it was created
  private). An unauthenticated check now returns 200, not 404. Its macOS CI is publicly
  visible while unported — a scheduling input for the fork's lane, not a defect.

- `[mac]` 2026-10-18 — header change (row #70): the manual-plane **getter** is declared —
  `ObScanManualRPPoints` (the SDK's point-list view of the getter's 0x18-byte config: `points` at
  +0x08, `count` at +0x10) plus `fn_obscan_scan_get_manaul_remove_plane`, with ABI asserts. Blob
  `fdf01bb11fae7510d87c3e8a4327d9217693d5cb`, sha256
  `6c335a88fff83a54f68e9eadf834532a1cf11c72930ad5fd2b0ab28b1463f178` (parent `main` after #199).
- `[mac]` 2026-10-18 — header change (row #78): the per-frame **tracking-status enum** is declared
  (`OB_SCAN_TRACKING_SUCCESS` = 0 … `FAILURE_MOVING_TOO_FAST` = 6; out of range → `UNKNOWN`), pinned
  from the vendor's own name/desc lookup functions. Blob
  `9c73210f0a6f3868f52079794d4c8e7a30ea7d47`, sha256
  `1ace2dc814acb68e5315aea8421e615d17e3fc776466d6cbd5f7fb6bf7546b2f` (parent `main` after #215).
- `[mac]` 2026-10-18 — **routine agreed with the fork's lane**: from here, one line per header
  change lands in this log as part of the landing itself; the fork syncs at its own cadence,
  consumes the tip at push time, records the blob it consumed in the pin row, and treats a lagging
  pin as expected — the drift alarm is the fork's `--pincheck`.

- `[win]` 2026-10-18 — the fork's CI ran for the first time and came back **green** on both PRs
  (#11 `verify-pr` pass 1m22s, #12 pass 1m35s; `auto-verdict` pass on both) — **no
  local-versus-runner divergence**, so the ported gate agrees with every local run. What executed
  on the runner: the trusted base copy of the script, the selftest on both copies, the pin check
  against the committed header, the tier computation, the build check.
- `[win]` 2026-10-18 — the fork's branch protection now mirrors the parent's shape: required
  contexts `[verify-pr, verdict]`, strict false, no review requirement, force-pushes and deletions
  blocked — checked by read-back, not by the request.
- `[win]` 2026-10-18 — the hardware backstop is **scoped to the room** (PR #12, merge `ed7e467`):
  the two false-positive command shapes (a PATH prepend of tool directories; prose naming vendor
  libraries) now run, verified by re-running them verbatim; the guard still exists, and it is inert
  outside the hardware room.
- `[win]` 2026-10-18 — the fork's **board exists** (its PR #13): rows are numbered **100+**, so a
  bare row number cannot mean different work on each machine. Row #113 is the honest one: **no
  Windows hardware run has ever executed**, so every claim from that lane is a build or recon
  claim, never a hardware one.
- `[mac]` 2026-10-18 — the trail, as agreed and now enforced: a parent header change logs its line
  here in the same PR (the `fork-log` gate check), the fork records the blob it consumes in its pin
  row when it syncs, and its `--pincheck` alarms on drift.

- `[mac]` 2026-10-19 — a **stray write into this repo**, recorded as it happened: the fork lane's
  verifier posted a `VERDICT: MERGEABLE @ 011f2787…` comment on **this** repo's PR #11
  (`2026-10-19T01:16:25Z`) — its own fork's PR #11 head, not ours. It caught the error 29 s later
  and posted the corrected verdict to the right repo. **Inert for the gate** (the named SHA never
  equalled this repo's head, so no status was ever set), and the comment has been **deleted** here
  (the parent's pen). The cause is exactly the bug the fork's #11 fixes: a clone carrying
  `upstream`, `gh` preferring it, and a bare `gh pr comment` resolving to the parent — both
  machines post as the same account, so the artifact was indistinguishable by author.
  **Audit on this side**: every room of the parent's carries only `origin` (no `upstream`), so the
  trap cannot fire here today; and every verifier order now names the repo explicitly
  (`--repo juicie-brucie/Open3DScanner`) so it stays that way.

- `[win]` 2026-10-19 — **the wrong-repo writes' actual trigger is the remote NAME**: `gh` treats a
  remote named `upstream` as a fork's base repo, so with `origin` (the fork) plus `upstream` (the
  parent) every bare `gh` call resolved to the parent — the mechanism behind both incidents (the
  watcher reading the parent's PR 7 as MERGED; the stray verdict on the parent's PR #11). **Proven,
  not inferred**: with the remote named `upstream`, `gh repo view` returns the parent; renamed to
  `parent`, it returns the fork. **Every fork room now carries `origin` + `parent`.** On the
  parent's side no room carries an `upstream` remote (audited), and the rule to carry forward is
  the negative one: if a parent remote is ever added here, do not name it `upstream`. The name is
  now recorded in `WINDOWS-FORK.md` §1/§4 so the exception cannot quietly become the default.
- `[mac]` 2026-10-19 — **Windows lane round 1 accepted.** The fork reports twelve merges, board `win #24` @ `a37205b`,
  none merged around a `NEEDS CHANGE`; the parent sync is **landed** (their PR #17 @ `f2e9003`), the contract pin is
  `1ace2dc8` on both sides (`--pincheck` hashes the committed blob) and `--selftest` is 52 cases. `win #119` is the
  first real exercise of the parent's `_WIN32` loader split: `LoadLibraryA(full path)` does not search the DLL's own
  directory, and `LoadLibraryExA(..., LOAD_WITH_ALTERED_SEARCH_PATH)` fixes it — **if that change lives in
  parent-synced code it comes back here or as a fork→parent PR**, which is the question put to the fork lane with this
  entry. The fork's gate note is **adopted on this side**: a UTF-8 BOM at the head of a verdict comment defeats an
  anchored-line parser, and the parent's `verdict-gate.yml` now strips one; verdicts stay BOM-free on both sides. The
  device remains on the Mac; the handover is this side's call and waits on the #105/#127 live diagnostics.
- `[mac]` 2026-10-19 — **the Windows loader fix is folded into the parent** (row #129), so the next
  `engine/` sync is a no-op rather than a differing tracked file. `engine/src/obscan_loader.cpp` now
  uses `LoadLibraryExA(..., LOAD_WITH_ALTERED_SEARCH_PATH)` (a plain `LoadLibraryA` with a full path
  does not search the DLL's own directory for the SDK's adjacent dependencies), and the Windows
  `LastErrorMessage` names the path it failed on. **Applied verbatim from the fork** (their commit
  `4ed3e40`, merge `bfa3df3`): blob `bc09230` = `bc09230323f6149548033cd4a9a2125a59b7cc27`, byte-for-byte
  identical to the fork's own file (verified against the fork's content, not just the abbreviated
  sha). The Windows-side evidence is the fork's `win #119` run (the SDK loads and its bootstrap
  exports resolve) — **this side compiles only the `#else` branch**, so the macOS build and
  `ctest` prove the non-Windows path is unaffected and do **not** exercise `LoadLibraryExA`.
