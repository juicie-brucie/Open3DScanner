# Exposure sweep against the new, flatter physical setup

Run with `OPENSCANNER_EXPOSURE_SWEEP=1` (see `tools/live_capture_test/main.cpp`)
against a deliberately reworked physical scene — flatter/wider, with
better marker coverage on/around the object itself, specifically to
test the "object shape and marker density, not software, was the
limiting factor" theory from `logs/creality_scan_comparison_20261016/`.

## Result: the theory held, decisively

`obscan_scan_get_realtime_model_mesh`, read right after
`obscan_scan_pause` (same pattern as `logs/stop_abi_fix_validation/`):

- **42,224 vertices, 34,844 triangles** — roughly **7x** the previous
  best (5,900-6,239 vertices, from the tall-cup-stack setup).

`obscan_scan_stop` and `obscan_session_destroy` both returned
`OB_SCAN_SUCCESS` again, consistent with the restored ABI fix holding.
905 real nonzero `"lasere total_points_num"` lines in the native log,
confirming the laser-detection pipeline itself stayed healthy
throughout.

## The exposure sweep itself: inconclusive for `raw_cloud`

The sweep stepped `obscan_scan_config_combined_exposure_and_laser_current`
across its full `[100, 1000]` range (10 steps, ~3s each), polling
`obscan_scan_get_raw_cloud` at every step. Every single poll at every
exposure value returned `num_points=0` — consistent with this
project's standing, still-unresolved finding that `raw_cloud` never
reflects live data *during* active scanning, regardless of exposure;
only `realtime_model_mesh`, read after pause, has ever shown real
data. So this run doesn't tell us anything new about *which* exposure
value is best (all of them showed nothing via this getter) — but it
does conclusively rule exposure out as the explanation for the
previously-sparse results, now that the same full sweep, on a much
better physical setup, produced 42k+ real vertices once read correctly
(after pause).

## Known issue unaffected by this fix

`obscan_context_destroy()` still exceeded the 180s worker timeout here
too (force-killed cleanly, no manual recovery needed) — the separate,
still-open issue documented in `logs/stop_abi_fix_validation/README.md`.
