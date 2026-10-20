- 2026-10-20 — **the dispatcher's integration step is a script**: `scripts/pr-integrate.py <PR>` takes the PR's
  changed files verbatim, merges `PLAN.md` **row-aware and append-only** (a changed row keeps its title/status and
  appends its note; other rows are never reverted; new rows are inserted where the PR put them), builds **Release**
  (catching an API mismatch like the #359 rebuild's `EndTick` break), and leases the push. `--dry-run` validates
  without pushing. It is why the dispatcher's rebuild pass stops being hand work — and the builder rule
  (`git fetch && git rebase origin/main` before the final push) is now in `builder.md`.
