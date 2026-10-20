- 2026-10-20 — **the dispatcher's integration step is a script**: `scripts/pr-integrate.py <PR>` takes the PR's
  changed files verbatim, merges `PLAN.md` **row-aware and append-only** (a changed row keeps its title/status and
  appends its note; other rows are never reverted; new rows are inserted where the PR put them), builds **Release**
  (catching an API mismatch like the #359 rebuild's `EndTick` break), and leases the push. `--dry-run` validates
  without pushing. It is why the dispatcher's rebuild pass stops being hand work — and the builder rule
  (`git fetch && git rebase origin/main` before the final push) is now in `builder.md`.
- 2026-10-20 — **the note merge is three-way** (the verifier's blocking finding on #364): a row both sides moved
  keeps **base + main's addition + the PR's addition**, main first. The two-way test it replaces took main's note
  alone in that case and dropped the PR's addition — and a row's accumulated notes are the board's memory, so a
  dropped note is a dropped record. Nothing is dropped and nothing is written twice: one side already a prefix of
  the other, or the same text appended on both sides, is recognised; a row absent from the base keeps main's title.
  `merge_note` states the rule and `python3 scripts/pr-integrate.py --selftest` pins it (9 cases; no git, no `gh`,
  no network).
