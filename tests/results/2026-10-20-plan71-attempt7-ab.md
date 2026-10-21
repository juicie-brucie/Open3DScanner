# PLAN #71 — attempt 7, single-session A/B (plane removal OFF → ON → OFF) — vendor accepts all toggles; point-count effect not cleanly separable

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (synced to origin/main @ `2174a66`)
- Target: build/tools/scan_server/scan_server (build-tree copy, fixed order 2: build/tools/<target>/<target>)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (22:17:05) — not used this run
- Preflight: `hw-preflight: OK`; pwd OK; no conflicting process; daemon RPCServer(26387,) running; freshness: binary 22:17:05 >= newest source `fast_encode.h` 22:06:30
- Interposer: expected `[vulkan_disable_interpose] loaded` (line 2) — present
- Run start: 2026-10-20 22:47:41
- Device: CR-Scan Raptor Pro, SN `CR0886100SN`; static scene throughout (scanner at rest, no operator movement)

## Pre-check

`set_plane_removal` confirmed wired via `HandleControlMessage` (called at `main.cpp:407`, `main.cpp:533`, `main.cpp:1133`; routed through `control_state.h:85` → `plane_toggle.h:223`). Row #158 is `HW`. Toggle exists; attempt 7 is viable.

## Command

```
./build/tools/scan_server/scan_server --live 8787

# A/B sequence via WebSocket (Python ws client):
# toggle ON:  {"cmd":"set_plane_removal","auto":true,"turntable":true,"manual":"0,0,1,-0.33"}
# toggle OFF: {"cmd":"set_plane_removal","auto":false,"turntable":false,"manual":""}
```

## Result

- Exit code: SIGKILL on worker (launcher SIGTERM'd; worker did not exit within 5 s — SIGKILL applied; `obscan_scan_pause -> OB_SCAN_SUCCESS` logged before kill)
- Verdict: **PASS for the toggle mechanism; INCONCLUSIVE for the per-tick point-count effect**
- Raw log: `2026-10-20-plan71-attempt7-ab.raw.log` (same directory)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` — present, line 2

## A/B timeline

| Phase | tick | `points` per window | Note |
|---|---|---|---|
| Baseline (OFF) | 0–425 | 457 → 789 → 1087 → 1109 → 1125 → 1141 | rising as cloud accumulates; still climbing at toggle |
| Toggle ON applied | 426 | `points=1168` at toggle | all 3 calls accepted with `OB_SCAN_SUCCESS (0)` |
| Post-ON (ON) | 427–742 | 1174 → 1182 → 1200 → 1203 | still rising gradually |
| Toggle OFF applied | 743 | `points=1197` at toggle | all 3 calls accepted with `OB_SCAN_SUCCESS (0)` |
| Post-OFF (OFF) | 744+ | 1195 → 1201 → 1210 → 1214 | continues rising |

## Toggle log lines (verbatim from raw log)

```
[worker] plane_removal: obscan_scan_plane_remove(true) -> OB_SCAN_SUCCESS (0) accepted [auto 0->1 tick=426 points=1168]
[worker] plane_removal: obscan_scan_turntable_plane_remove(true) -> OB_SCAN_SUCCESS (0) accepted [turntable 0->1 tick=426 points=1168]
[worker] plane_removal: obscan_scan_config_manaul_remove_plane(a,b,c,d) -> OB_SCAN_SUCCESS (0) accepted [manual 0->1 plane=0,0,1,-0.33 tick=426 points=1168]
[worker] plane_removal: obscan_scan_plane_remove(false) -> OB_SCAN_SUCCESS (0) accepted [auto 1->0 tick=743 points=1197]
[worker] plane_removal: obscan_scan_turntable_plane_remove(false) -> OB_SCAN_SUCCESS (0) accepted [turntable 1->0 tick=743 points=1197]
[worker] plane_removal: obscan_scan_config_manaul_remove_plane(off) -> OB_SCAN_SUCCESS (0) accepted [manual 1->0 tick=743 points=1197]
```

## Checks

| # | Check | Result |
|---|---|---|
| 1 | Enumeration succeeds (`count=1`) and scan starts clean | **[PASS]** `count=1` on first probe; `obscan_scan_start -> OB_SCAN_SUCCESS (0)` |
| 2 | Toggle ON command is accepted by the vendor SDK mid-session | **[PASS]** all 3 calls (`plane_remove`, `turntable_plane_remove`, `config_manaul_remove_plane`) returned `OB_SCAN_SUCCESS (0)` at tick 426 |
| 3 | Toggle OFF command is accepted by the vendor SDK mid-session | **[PASS]** all 3 calls returned `OB_SCAN_SUCCESS (0)` at tick 743 |
| 4 | Per-tick point count drops detectably when plane removal is ON vs OFF, on a static scene | **[INCONCLUSIVE]** The `points` value was still on a rising trend at the toggle moment (cloud accumulating); it continued rising through all three phases at a similar rate (~20–30 points/window per 5 s window), with no visible step-change or plateau at either toggle boundary. The scene was static (no intentional floor/plane in frame); a plane removal effect that acts on a floor plane would require a floor plane to be present and reliably framed. No confound-free A/B separation is observable in this data. |
| 5 | No crash, no unexpected exit | **[PASS]** tracking 2942 frames, all SUCCESS; `obscan_scan_pause -> OB_SCAN_SUCCESS`; final mesh broadcast (5227 vertices, 3852 triangles); no crash report |

## Crash sweep

`scripts/hw-postflight.sh plan71-attempt7-ab '2026-10-20 22:47:41' scan_server` →
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-20 22:47:41 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

## On the inconclusive result

The toggle mechanism is confirmed working end-to-end on real hardware: both directions accepted by the vendor SDK mid-session with `OB_SCAN_SUCCESS`, the transition log lines appear exactly as the device-free selftest predicted. The gap is observational: the static scene scanned in this run had no prominent flat plane in frame (or the plane coefficients `0,0,1,-0.33` did not intersect the actual scene geometry), so the expected per-tick point-count change was not visible above the natural cloud-accumulation trend. The A/B that #71 needs is: a static scene with a visible floor plane clearly in frame, the `manual` plane coefficients fit to that specific plane, and the toggle flipped once the cloud has stabilised (plateau, not still rising).

## Open questions

- Does the vendor's `obscan_scan_config_manaul_remove_plane` act on a user-supplied plane equation immediately, or does it need the scanner to build an internal model of that plane first? If the latter, the `0,0,1,-0.33` vector used here may not correspond to any plane in this scene.
- Would running with `OPENSCANNER_PLANE_REMOVE=1` at startup (auto plane detection) rather than a manual plane show a clearer effect, since the SDK would fit its own plane to whatever is in frame?

## Board

- Row move: #71 stays `HW` — the toggle mechanism is confirmed working on real hardware (both directions, `OB_SCAN_SUCCESS`, correct log lines), but the per-tick point-count effect (the row's owed observable) is inconclusive in this run. A follow-up A/B with a prominent floor plane in frame is still owed.
