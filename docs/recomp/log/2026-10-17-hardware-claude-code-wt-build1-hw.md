- 2026-10-17 — hardware (Claude Code, `wt-build1` → `hw/plan8-plan10`): PLAN #8's run
  landed — **all 7 checks pass**; #1, #8, #10 → `DONE`. The run's `SIGSEGV` was first
  written up as the known libomp crash in `obscan_context_destroy()`, then **retracted**:
  the crash report (archived with the run) shows a NULL deref in the vendor's
  connect-callback on the watcher thread, right after the OQ-3 re-add → new **#11**.
