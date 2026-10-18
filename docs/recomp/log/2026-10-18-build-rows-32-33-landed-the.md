- 2026-10-18 — build: rows #32/#33 landed (PR #77) — the live broadcast is
  stride-decimated to a fixed 20k-point budget per tick (the redundant raw poll
  dropped); README's teardown guidance corrected (`pkill` matches the worker;
  SIGTERM runs its teardown; only `kill -9` skips it). #32 → `HW`, #33 → `DONE`.
