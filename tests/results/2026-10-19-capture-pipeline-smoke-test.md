# capture_pipeline_smoke_test — capture-to-mesh-tools data path (synthetic, no hardware) — PASS

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/capture_pipeline_smoke_test/capture_pipeline_smoke_test (build-tree copy (fixed order: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; freshness: binary 2026-10-18 21:46:44 >= newest source main.cpp 2026-10-17 18:49:32
- Interposer: preflight's generic expectation is `[launcher] DYLD_INSERT_LIBRARIES=...`, but this target's own header states it deliberately needs no physical scanner and no vendor SDK I/O — consistent with no launcher/interposer line appearing in the output (see below).
- Run start: 2026-10-19 16:10:22
- Device: not applicable — this test constructs synthetic vendor-struct-shaped data; no device connection required.

## Command

```
build/tools/capture_pipeline_smoke_test/capture_pipeline_smoke_test
```

## Result

- Exit code: `0`
- Verdict: **PASS**
- Raw log: `2026-10-19-capture-pipeline-smoke-test.raw.log` (same directory)
- Interposer confirmation: ABSENT — no `[launcher]`/`[vulkan_disable_interpose]` line printed. Consistent with this binary not linking the vendor SDK (per its own header comment: "Verifies the capture -> mesh-tools data path without needing a physical scanner").

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | mesh path: box -> vendor-struct bridge -> boolean union -> round-trip | **[PASS]** `mesh path OK: captured 8 verts/12 tris -> bridged -> boolean union -> 8 verts/12 tris` |
| 2 | point-cloud path: vendor-struct bridge round-trip | **[PASS]** `point cloud path OK: 4 captured points bridged intact` |
| 3 | bridge rejects malformed vendor meshes/clouds | **[PASS]** `validation OK: bridge rejects malformed vendor meshes/clouds` |
| 4 | overall | **[PASS]** `OK: capture -> mesh-tools data path verified (synthetic data, no hardware)` |

## Crash sweep

`scripts/hw-postflight.sh 2026-10-19-capture-pipeline-smoke-test '2026-10-19 16:10:22' capture_pipeline_smoke_test`:

> no crash report for 'capture_pipeline_smoke_test' at/after 2026-10-19 16:10:22 — safe to record 'no crash report (checked DiagnosticReports for capture_pipeline_smoke_test at/after the run start)'.

## Open questions

- None raised by this run — output and exit code both clean, no crash, no
  device dependency to question.

## Board

- Row move: none proposed. No PLAN.md row references `capture_pipeline_smoke_test` by name; this run found none to update.
