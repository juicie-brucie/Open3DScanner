- 2026-10-19 — `verify-pr.py` now treats **`_`-prefixed files under `tests/results/`** as support files, not run records:
  they need no companion raw log, so a template (`_TEMPLATE.md`) can sit beside the records it describes. The rule lives
  in one place (`is_run_record()`), and the selftest pins three cases: a plain record is a record, a `_`-prefixed file is
  not, a `-correction.md` is not. This lands first because CI runs the gate from `origin/main` — the PR that adds the
  template cannot pass until the convention is on main.
