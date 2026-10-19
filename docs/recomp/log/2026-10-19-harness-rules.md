- 2026-10-19 — the **pipeline's stability rules** are written down (`ARCHITECTURE.md`, "The loop's own stability
  rules") and the **hw tester's instructions** (`.claude/agents/hw-test.md`) now carry the three rules its runs
  needed: a **fresh-binary check** (#107 — a stale bundle read as a regression), a **name-anchored process check**
  (a `sync-to-main.sh` false positive on a path containing `scan_server`), and **no edits to `PLAN.md`** (report the
  move; the dispatcher is the board's single writer). Each rule cites what enforces it; the incident record is the
  wind-down log.
