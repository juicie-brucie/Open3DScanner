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
