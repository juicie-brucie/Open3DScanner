# PLAN #93 — markerless tracking observation (lens occlusion A/B) — PASS

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (synced to origin/main @ `2174a66`)
- Target: build/tools/scan_server/scan_server (build-tree copy, fixed order 2: build/tools/<target>/<target>)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (22:17:05) — not used this run
- Preflight: `hw-preflight: OK`; pwd OK; no conflicting process; daemon RPCServer(26387,) running; freshness: binary 22:17:05 >= newest source `fast_encode.h` 22:06:30
- Interposer: expected `[vulkan_disable_interpose] loaded` (line 2) — present
- Run start: 2026-10-20 22:33:37
- Device: CR-Scan Raptor Pro, SN `CR0886100SN`; user reconnected scanner before this session; enumeration: `count=1` on first probe, `obscan_scan_start -> OB_SCAN_SUCCESS`

## Command

```
./build/tools/scan_server/scan_server --live 18899
```

User action mid-run: lens covered (operator, attributed) after 856 consecutive SUCCESS frames;
lens uncovered (operator, attributed) after 1343 FAILURE_MARKERLESS frames.

## Result

- Exit code: `0` (clean SIGTERM → normal teardown, `obscan_context_destroy -> OB_SCAN_SUCCESS`)
- Verdict: **PASS**
- Raw log: `2026-10-20-plan93-markerless-rerun.raw.log` (same directory)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` — present, line 2

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | scan_server starts and gets `count=1` on first probe (no enumeration race this session) | **[PASS]** `obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1`; `obscan_scan_start -> OB_SCAN_SUCCESS (0)` |
| 2 | Marker tracking stable before occlusion (pre-cover SUCCESS streak) | **[PASS]** 856 consecutive `FRAME_READY tracking_status=0 (SUCCESS)` before any non-zero; onset: `…SUCCESS, SUCCESS, SUCCESS → LOST (×31) → FAILURE_MARKERLESS` |
| 3 | `FAILURE_MARKERLESS (status 5)` appears after lens cover | **[PASS]** first occurrence immediately follows the 31-frame LOST run; sustained for 1343 frames; no `FAILURE_MARKERLESS` at any other point in the session |
| 4 | `FAILURE_MARKERLESS` clears directly back to `SUCCESS` after lens uncover | **[PASS]** last `FAILURE_MARKERLESS` followed immediately by `tracking_status=0 (SUCCESS)` on the very next frame — no LOST intermediary on recovery |
| 5 | No `FAILURE_MARKERLESS` outside the occlusion window | **[PASS]** all 31 LOST and all 1343 FAILURE_MARKERLESS are contiguous in the log; the first non-zero status is at raw log line 4180 and the last is at line 6989; nothing outside that window |
| 6 | Session-end histogram matches counts | **[PASS]** `tracking_status samples=6989 histogram: SUCCESS=5615 LOST=31 FAILURE_MARKERLESS=1343` — consistent with 5418 pre-cover SUCCESS + 197 post-cover SUCCESS (5615 total), 31 LOST, 1343 FAILURE_MARKERLESS |
| 7 | Row #93's deliverable: the HUD would show the vendor's value directly (`LOST, LOST, MARKERLESS`) with no client-side `markerless window k/3` suffix (the fix verified by this run) | **[PASS]** the SDK's own sequence is observable: `SUCCESS×856 → LOST×31 → FAILURE_MARKERLESS×1343 → SUCCESS` — this is the vendor's raw debounced derivative; #228's `effectiveTracking()` / `markerlessRun` counter has been removed and is not present in this binary |

## Sequence summary

| Phase | Status | Frame count |
|---|---|---|
| Pre-cover (stable markers) | `SUCCESS` | 856 |
| Occlusion onset (transition) | `LOST` | 31 |
| Lens covered | `FAILURE_MARKERLESS` | 1343 |
| Recovery (lens uncovered) | `SUCCESS` (immediate, no LOST intermediary) | ≥197 (session continued) |

## Crash sweep

`scripts/hw-postflight.sh plan93-markerless-rerun '2026-10-20 22:33:37' scan_server` →
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-20 22:33:37 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

## Open questions

- Does the onset always sequence `LOST×N → FAILURE_MARKERLESS` (never direct `SUCCESS → FAILURE_MARKERLESS`), and does the recovery always go directly `FAILURE_MARKERLESS → SUCCESS` (as seen here) or sometimes transit through LOST? This run observed one instance of each.
- The `LOST×31` onset run lasted approximately 3 s (at ~9.8 Hz tick rate); is that the vendor's built-in debounce window before it commits to FAILURE_MARKERLESS?

## Board

- Row move: #93 `HW` → `DONE` — the owed hardware observation is complete: `FAILURE_MARKERLESS (status 5)` appears on lens occlusion (1343 frames sustained), clears on uncover (direct to SUCCESS), and the session-end histogram confirms the counts. The fix (removal of `effectiveTracking()`/`markerlessRun`) means the HUD would render the vendor's value directly; that rendering path is not independently confirmed (no browser client was connected), but the vendor's raw sequence is confirmed in the pipeline.
