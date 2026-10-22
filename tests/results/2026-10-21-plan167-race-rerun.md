# PLAN #167 — device-enumeration-race fix (5-start rerun) — PASS

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server  (explicit argument (the binary this run will execute))
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (12:53:07) — refreshed only on an app build (#107)
- Preflight: pwd OK; no conflicting process; daemon: RPCServer(61699,) running (process proxy: a vendor RPC host process exists); freshness: binary 12:53:07 >= newest source main.cpp 12:52:55
- Interposer: expect line 2: [vulkan_disable_interpose] loaded  (scan_server sets DYLD_INSERT_LIBRARIES itself)
- Run start: 2026-10-21 21:28:49
- HEAD SHA: 442f8b075a2babdb6316d0f75b940bc336940906
- Binary sha256: e29f45b9b4a1e353a7207784f8656b4a3c7635c223e72a18f401790d8da5ed14

## Command

```
OPENSCANNER_PLANE_REMOVE=0 build/tools/scan_server/scan_server --live 8787
```

(run 5 sequential times; each start killed with SIGKILL after 60 s once obscan_scan_start confirmed)

## Result

- Exit code: `137` (SIGKILL) for all 5 starts — no abnormal exit
- Verdict: **PASS**
- Raw log: `2026-10-21-plan167-race-rerun.raw.log` (same directory)
- Raw log sha256: `9fb86d34950e10c9413e8160bcdfb1ae419e4e5442ede068faac6a8ac7b26189`
- Interposer confirmation: `[vulkan_disable_interpose] loaded` present as line 2 in all 5 starts

## RPCServer port checks

All three ports (10010, 10011, 10013) were OPEN before start 1 and before each of starts 2–5.

## Per-start observations

| Start | Wall time (device list) | device(s) list count | get_device_num count | settled after N | scan_start result | exit |
|---|---|---|---|---|---|---|
| 1 | 21:29:02.797658 | 1 | 1 | — (count=1 on first probe) | OB_SCAN_SUCCESS | 137 (SIGKILL) |
| 2 | 21:30:32.514149 | 1 | 1 | — (count=1 on first probe) | OB_SCAN_SUCCESS | 137 (SIGKILL) |
| 3 | 21:31:42.562292 | 1 | 1 | — (count=1 on first probe) | OB_SCAN_SUCCESS | 137 (SIGKILL) |
| 4 | 21:32:51.313644 | 1 | 1 | — (count=1 on first probe) | OB_SCAN_SUCCESS | 137 (SIGKILL) |
| 5 | 21:34:00.075748 | 1 | 1 | — (count=1 on first probe) | OB_SCAN_SUCCESS | 137 (SIGKILL) |

## Checks

| # | Check | Result |
|---|---|---|
| 1 | Zero SIGSEGV across all 5 starts | **[PASS]** no signal exit observed in any start |
| 2 | Transient count=0 either recovers or exits 2 | **[PASS]** count=0 not observed; device found on first probe each start |
| 3 | obscan_scan_start reached in all 5 starts | **[PASS]** `obscan_scan_start -> OB_SCAN_SUCCESS (0)` in all 5 starts |
| 4 | Interposer loaded in all 5 starts | **[PASS]** `[vulkan_disable_interpose] loaded` on line 2 in all 5 starts |

## Crash sweep

no crash report for 'scan_server' at/after 2026-10-21 21:28:49 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.

## Open questions

- The race occurred only intermittently in historical records. Does the 5-start series provide sufficient coverage, or should a longer soak (e.g. 20 starts) be added to the plan?
- No count=0 transient was observed in any of the 5 starts. Does that mean the device settled before the first probe in all cases, or does the device / network state affect probe timing?

## Board

- Row move: #167 `HW` → `DONE` — zero SIGSEGV across 5 sequential starts; obscan_scan_start succeeded in all 5; acceptance criteria met
