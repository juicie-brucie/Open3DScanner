- 2026-10-19 — correction recorded on **#109**: a session record with `model: null` means the **agent default** is in effect
  (verify-5: `qwen3.7-plus#high`), not a missing model. The `--sessions` view should say "default (agent)" instead of
  "running: <none>" + DRIFT. Drift on sessions created before an agent-file change is expected — a session captures its model
  at creation — so the checker should flag live work, not history.
