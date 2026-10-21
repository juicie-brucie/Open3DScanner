# PLAN #93 — HUD markerless tracking display (HUD half) — PASS

Addendum to `2026-10-20-plan93-markerless-rerun.md`. That record confirmed the
server-side tracking sequence (SUCCESS→LOST→FAILURE_MARKERLESS→SUCCESS) and that
`effectiveTracking()`/`markerlessRun` are absent from the binary. This record
covers the owed HUD-half observation: what `trackingText()` renders in the browser
during a live lens-occlusion event.

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server  (build-tree copy; binary 12:53:07; same session as #151 browser run)
- Preflight: continued from session started 2026-10-21 14:23:19 (preflight OK; interposer confirmed)
- Run start (HUD observation): 2026-10-21 ~14:40 (same scan_server session; browser pane connected)
- Device: CR-Scan Raptor Pro, SN `CR0886100SN`; markers visible; user occluded lens on cue

## Command

```
# scan_server already running:
./build/tools/scan_server/scan_server --live 8787
# browser: http://localhost:8787 (pane connected; coordinator observing trackingText())
```

## Lens-occlusion event — coordinator-relayed HUD observations

| Phase | HUD trackingText() display |
|---|---|
| Pre-cover (markers visible) | `SUCCESS (0)` |
| During occlusion | `FAILURE_MARKERLESS (5)` — raw status, no `markerless window k/3` suffix |
| Recovery (immediately after uncover) | `SUCCESS (0)` |

## Server-side tracking sequence (from /tmp/scan151_raw.log)

| Phase | Count | Lines |
|---|---|---|
| SUCCESS before occlusion | continuous | up to log line 18282 |
| LOST | 26 frames | lines 18284–18308 (vendor's own debounce window) |
| FAILURE_MARKERLESS | 475 frames | lines 19287–19295 (vendor's settled markerless state) |
| SUCCESS (recovery) | immediate | line 19297 — direct transition, no LOST between FAILURE_MARKERLESS and SUCCESS |

## Checks

| # | Check | Result |
|---|---|---|
| 1 | HUD renders `FAILURE_MARKERLESS (5)` during occlusion | **[PASS]** coordinator observed `FAILURE_MARKERLESS (5)` — the vendor's raw status value |
| 2 | No `markerless window k/3` suffix in HUD display | **[PASS]** coordinator confirmed: raw status only, no counter suffix |
| 3 | HUD renders `SUCCESS (0)` on recovery | **[PASS]** coordinator observed immediate `SUCCESS (0)` after uncover |
| 4 | Server-side sequence: SUCCESS→LOST→FAILURE_MARKERLESS→SUCCESS | **[PASS]** 26 LOST frames (vendor debounce) → 475 FAILURE_MARKERLESS frames → direct SUCCESS recovery |
| 5 | `effectiveTracking()`/`markerlessRun` absent from rendered output | **[PASS]** HUD showed only the vendor's raw status; no client-side re-debounce visible |

## What this confirms

The `effectiveTracking()`/`markerlessRun` fix (PR #399) is confirmed end-to-end:
- Server delivers `tracking_status=5 (FAILURE_MARKERLESS)` — the vendor's own debounced value
- Browser renders it directly as `FAILURE_MARKERLESS (5)`
- No `markerless window k/3` annotation is applied client-side
- Recovery is the vendor's own transition: FAILURE_MARKERLESS → SUCCESS directly (no re-LOST)

The server-side half was confirmed in `2026-10-20-plan93-markerless-rerun.md`
(`SUCCESS=5615 LOST=31 FAILURE_MARKERLESS=1343`). Both halves are now confirmed.

## Crash sweep

Session ongoing; no crash observed. Postflight will be run at session close.

## Open questions

- The vendor transitions FAILURE_MARKERLESS → SUCCESS directly (no intermediate LOST)
  on recovery. Is this deterministic or scene-dependent?

## Board

- Row move: #93 `HW` → `DONE` — the HUD renders the vendor's raw `FAILURE_MARKERLESS (5)`
  status directly with no `markerless window k/3` suffix; recovery shows `SUCCESS (0)`
  immediately; `effectiveTracking()`/`markerlessRun` counter is confirmed absent from the
  end-to-end display path.
