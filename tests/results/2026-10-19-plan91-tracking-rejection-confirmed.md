# PLAN #91 — G3 per-pose tracking-rejection counts (status JSON + teardown histogram)

**Date**: 2026-10-19
**Worktree**: `Open3DScanner-wt-hw`, branch `hw/next`, HEAD `51e7960`.
**Target**: `build/tools/scan_server/scan_server --live`
**Operator**: hw-test subagent.

## Why this run

Row #91 (`docs/recomp/PLAN.md` line 101): PR #243 (`0fa1677`) delivered the wire side
— `"trackingCounts"` + `"trackingName"` in the status JSON, pinned names in the
FRAME_READY log and the teardown histogram — but the observation on real hardware was
owed. This run supplies it. Read-only observation of existing data (not an env-var
toggle comparison), so it does not carry #71/#77's scene-variance confound.

## Binary freshness

`tools/scan_server/main.cpp` and `tools/scan_server/tracking_status.h` last changed at
`ba33c54`/`0fa1677` respectively, both ancestors of current HEAD `51e7960`; `git status`
and `git diff HEAD -- tools/scan_server/main.cpp` showed no local changes. Ran `cmake
--build build --target scan_server` immediately before testing — every dependency
reported `Built target …`, no rebuild fired, confirming the binary already matched
source (checked per #107). Confirmed the field names exist in the binary via `strings
build/tools/scan_server/scan_server | grep -i trackingCounts` → found
`,"trackingCounts":{` and `,"trackingName":"`.

## Preconditions checked

- `pgrep -x scan_server` / `-x raptor_client` / `-x OpenScanner`: all empty before
  starting both runs — no conflicting SDK user (rule 13: name-anchored).
- Checked `~/Library/Logs/DiagnosticReports/` before starting: most recent
  `scan_server-*.ips` timestamped Oct 19 14:59:54, well before this session (15:23+).
- Device count: not probed as a separate dry run — `scan_server --live` itself prints
  `obscan_device_management_get_device_num` at startup before streaming begins, so it
  doubled as the step-3 check in both runs: `count=1` both times (confirmed below).

## Procedure

Per `docs/recon/findings/...` and `tracking_status.h`: the status JSON tail format is
`,"tracking":<n>,"trackingName":"<NAME>"` plus a separate
`,"trackingCounts":{"SUCCESS":n,"LOST":n,"AMBIGUOUS_PLANE":n,...}` object (one key per
pinned enum name, `TrackingCountsJsonTail`), broadcast every ~10 ticks (~1s) over the
websocket's text channel (`BuildStatusJson` → `server.BroadcastText`, `main.cpp:1276`).
This is **not** printed to stdout — it only reaches a connected client. No GUI/browser
was available this session, so a minimal stdlib-only (socket/base64/hashlib/struct)
Python WebSocket client was written to the scratchpad (not the repo) to connect and
capture the text frames; it performs the RFC6455 handshake and decodes frames, nothing
SDK-specific — purely an observer of the already-running server's public localhost
socket. No device motion/occlusion was deliberately staged (no live operator this
session); rejections, where they occurred, arose from ordinary setup/connect jitter.

**Run 1** (`obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1`):
```
./build/tools/scan_server/scan_server --live 8787 "" 25
```
Raw log: `2026-10-19-plan91-tracking-rejection.log`. Exit 0. Interposer confirmed loaded
(`[vulkan_disable_interpose] loaded`, line 2). No client connected this run
(`clients=0` in tick lines), so the wire-side status JSON was not observed here — only
the FRAME_READY stdout lines and the teardown line were. All 858 FRAME_READY samples
were `tracking_status=0 (SUCCESS)`. Teardown: `tracking_status samples=858 histogram:
SUCCESS=858` — a clean, rejection-free session.

**Run 2** (`obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1`):
```
./build/tools/scan_server/scan_server --live 8787 "" 25
```
launched in the background; after confirming `obscan_scan_start` and "Streaming live
scan frames" in the log (~6s in), the probe script connected and read frames for 20s,
then the server finished its own session + teardown on its own (never pkilled, per the
"don't kill mid-scan" rule in `main.cpp`'s own comment). Raw logs:
`2026-10-19-plan91-tracking-rejection-run2.log` (server) and
`2026-10-19-plan91-ws-probe.log` (probe). Server exit: `[launcher] Worker exited
normally with code 0.` Interposer confirmed loaded (line 2 of the server log).

**Status JSON observed live over the wire** (verbatim, first and last captured
samples):
```
{"type":"status","mode":"live","state":"scanning","autoExposure":false,"exposure":900,
"exposureMin":1,"exposureMax":1000,"fov":"normal","fovControllable":false,
"tracking":0,"trackingName":"SUCCESS",
"trackingCounts":{"SUCCESS":332,"LOST":15,"AMBIGUOUS_PLANE":0,
"FAILURE_INSUFF_VALID_POINTS":0,"FAILURE_TEXTURELESS":0,"FAILURE_MARKERLESS":0,
"FAILURE_MOVING_TOO_FAST":0}}
...
{"type":"status", ... "trackingCounts":{"SUCCESS":801,"LOST":15,"AMBIGUOUS_PLANE":0,
"FAILURE_INSUFF_VALID_POINTS":0,"FAILURE_TEXTURELESS":0,"FAILURE_MARKERLESS":0,
"FAILURE_MOVING_TOO_FAST":0}}
```
15 text status frames captured across the ~20s probe window, each with both
`trackingName` (the current-frame reason, always `"SUCCESS"` once stable) and
`trackingCounts` (the running per-reason tally) present and well-formed JSON. The
`LOST` bucket is non-zero and **fixed at 15 for the entire captured window** — the
rejections all happened early (before/during the probe's connection), then the device
tracked cleanly (`SUCCESS` climbing: 332 → 801) for the rest of the run. All other
reason buckets (`AMBIGUOUS_PLANE`, `FAILURE_INSUFF_VALID_POINTS`,
`FAILURE_TEXTURELESS`, `FAILURE_MARKERLESS`, `FAILURE_MOVING_TOO_FAST`) stayed at 0
throughout — only `LOST` fired as an actual rejection reason this session.

**Teardown histogram** (stdout, after the normal pause/stop/destroy chain, clean exit
0 — no `context_destroy` hang this run):
```
[worker] obscan_context_destroy: 1 poll(s), 0 refusal(s), 0.9s, final OB_SCAN_SUCCESS
[worker] tracking_status samples=847 histogram: SUCCESS=832 LOST=15
```
Matches the live-observed running tally's shape (`LOST=15` fixed, `SUCCESS` the
remainder) — the final `trackingCounts` the UI would have seen just before teardown is
consistent with this line.

## Checks

| # | Check | Result |
|---|-------|--------|
| 1 | Interposer loaded (banner present), both runs | [PASS] |
| 2 | Binary fresh against source | [PASS] — rebuild no-op, `strings` confirms field names present |
| 3 | Device count > 0 before full run, both runs | [PASS] `count=1` |
| 4 | Status JSON contains `trackingName` (current-frame pinned name) | [PASS] — e.g. `"trackingName":"SUCCESS"` |
| 5 | Status JSON contains `trackingCounts` (per-reason running tally, pinned names) | [PASS] — all 7 pinned-enum keys present, well-formed |
| 6 | Teardown histogram prints per-reason counts by pinned name | [PASS] — `SUCCESS=832 LOST=15` |
| 7 | At least one actual rejection reason observed (not just SUCCESS) | [PASS] — `LOST=15` in both the live JSON and the teardown line, run 2 |
| 8 | Clean teardown (no vendor `context_destroy` hang) | [PASS] both runs, exit 0 |
| 9 | No crash report generated | [PASS] — no `.ips` newer than 14:59:54 for this 15:23–15:26 window |

## Verdict

**PASS**. Both the per-tick/periodic status JSON (`trackingCounts` + `trackingName`,
captured live over the websocket with a minimal hand-rolled client since no GUI/browser
was available) and the final teardown histogram are confirmed on real hardware, in the
exact shape PR #243 describes. A genuine rejection reason (`LOST=15`) appeared
unprompted in run 2 (no deliberate occlusion/fast motion was staged — there was no live
operator this session) and is visible identically in both surfaces; run 1 by contrast
was rejection-free end to end (`SUCCESS=858`, no `LOST`), confirming the histogram is
real per-session data, not a hardcoded/always-same value. Other rejection reasons
(`AMBIGUOUS_PLANE`, `FAILURE_INSUFF_VALID_POINTS`, `FAILURE_TEXTURELESS`,
`FAILURE_MARKERLESS`, `FAILURE_MOVING_TOO_FAST`) were not exercised this session — only
`SUCCESS` and `LOST` were observed.

## Open questions

- Only `LOST` was observed as a rejection reason. The other five pinned reasons were
  never exercised (no deliberate occlusion/fast-motion/textureless-surface staging this
  session — no live operator present). If #91's "DONE" bar requires evidence of more
  than one rejection *kind*, a follow-up session with a human operator deliberately
  occluding/moving the device fast would be needed; this run only confirms the
  mechanism and format, not every reason code's live occurrence.
- The hand-rolled WebSocket probe is scratchpad tooling, not a repo artifact — if
  recurring observation of this JSON is wanted, consider whether a `websockets`-based
  probe belongs in the repo's test tooling (not done here, since editing/adding test
  code is out of scope for this run).

## Board move

#91: `HW` → `DONE` — the owed observation (status JSON `trackingCounts`/`trackingName`
+ teardown histogram, with at least one real non-SUCCESS reason, `LOST`, confirmed
live) is complete.

## Files

- `tests/results/2026-10-19-plan91-tracking-rejection-confirmed.md` (this file)
- `tests/results/2026-10-19-plan91-tracking-rejection.log` (raw log, run 1 — no client connected)
- `tests/results/2026-10-19-plan91-tracking-rejection-run2.log` (raw log, run 2 — server side)
- `tests/results/2026-10-19-plan91-ws-probe.log` (raw log, run 2 — WebSocket client capture of status JSON)
