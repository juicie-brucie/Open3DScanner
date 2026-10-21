# Four newly-found ungated hardware-free tests — first runs under their own name — PASS

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: `fc6a081` (Merge pull request #407 from juicie-brucie/dispatch/cleanup-log)
- Preflight/postflight: not run — all four hardware-free by their own `CMakeLists.txt` comments (row #151's Node load harness, row #159's "device-free and dependency-free" confidence/retention harness). No vendor SDK, no device, no daemon dependency.

## 1. ui_frame_age_load_test (row #151 — the publish-rate load harness)

- Location: `ui/frame_age_test.js --synthetic-load 50 20000 6.5`, via `node`
- Command: `node ui/frame_age_test.js build/tools/scan_server/scan_server --synthetic-load 50 20000 6.5` (and via `ctest -R ui_frame_age_load_test`)
- Exit code: `0`
- Output: all load-harness checks `PASS` — `hz=50 points=20000 window=6.5s frames=236 published=236 missing=0 drop_rate=0.00%`, HUD line `p50 1.4 / p95 2.9 ms`, server-side `[perf] window` shows `tick p50=23.72ms` against `nominal=20.00ms` at 1 client. `[frame-age] ok (0 failure(s))`.
- Verdict: **PASS**

## 2. confidence_sweep_test (row #159 — the confidence/retention harness self-test)

- Location: `tools/confidence_sweep/sweep_test.cpp`
- Command: `build/tools/confidence_sweep/confidence_sweep_test` (and via `ctest -R confidence_sweep_test`)
- Exit code: `0`
- Output: ~50 `[ ok ]` checks covering the synthetic scene, the confidence estimator, the retention sweep beating random (20/20 rows, mean improvement 78.3%), the negative control (a shuffled confidence field sitting on the random baseline, not beating it), PLY/binary-PLY round trips, session recording round trips, and the absolute-fraction sweep. `[ ok ] the confidence/retention harness's self-test passed`.
- Verdict: **PASS**

## 3. confidence_sweep_cli_test (row #159 — the CLI's own exit-code acceptance)

- Location: `tools/confidence_sweep/main.cpp` (`--synthetic --expect-beats-random --quiet`)
- Command: `build/tools/confidence_sweep/confidence_sweep --synthetic --expect-beats-random --quiet` (and via `ctest -R confidence_sweep_cli_test`, with `WORKING_DIRECTORY` set per the CMakeLists)
- Exit code: `0` (no output expected — `--quiet`)
- Verdict: **PASS**

## 4. confidence_sweep_artifacts_test (row #159 — CSV/SVG artifact determinism)

- Location: `tools/confidence_sweep/test_artifacts.py`
- Command: `python3 -I tools/confidence_sweep/test_artifacts.py build/tools/confidence_sweep/confidence_sweep` (and via `ctest -R confidence_sweep_artifacts_test`)
- Exit code: `0`
- Output: `sha256 csv 43cd65b8…`, `sha256 plot 4f98e4f9…`, `the artefact test passed` — confirms the CSV and SVG are written and identical between two runs (deterministic harness).
- Verdict: **PASS**

## Crash sweep

Not run — all four are hardware-free, no vendor SDK, no device I/O.

## Open questions

- None raised by these four runs — all clean, no failures.

## Board

- Row move: none proposed beyond confirming these four now run cleanly
  under their own name. No PLAN.md row asks for a standalone run of these
  separate from the feature rows they serve (#151, #159).
