# PLAN #71 — plane-removal toggle observation, re-run attempt 4

**Date**: 2026-10-19
**Worktree**: `Open3DScanner-wt-hw`, branch `hw/next`.
**Target**: `build/tools/scan_server/scan_server --live`
**Operator**: hw-test subagent.
**Context**: fourth attempt. Builds on attempt 3's result (device enumerates,
toggle calls bind correctly) and corrects two mistakes found by reading
`tools/scan_server/main.cpp` before this run:

1. **Wrong metric.** Attempt 3 watched `our_plane_removal fits=0 kept=0
   cut=0` as the pass/fail signal for #71. That counter belongs to a
   *different* feature — row #77's own client-side plane fit, gated by
   `OPENSCANNER_OUR_PLANE_REMOVE=1` (never set in any attempt so far, so it
   is correctly always 0 and irrelevant here). Per
   `docs/recomp/PLAN.md` row 71's own text, #71's owed observable is "the
   per-tick point-count series" from the cloud getter — the
   `tick N/M: color_cloud num_points=...` log lines — compared with and
   without the vendor toggles. `our_plane_removal` is not evidence for or
   against #71.
2. **Wrong units/magnitude for the manual plane.** Attempt 3 guessed
   `OPENSCANNER_MANUAL_PLANE="0,0,1,-500"`. A recorded baseline (via the
   documented `--record` flag) and an offline parse of the raw V1 frame
   payload (see below) showed the actual scene coordinates are in
   **meters**, not millimeters, and span roughly `z ∈ [0.15, 0.49]` — a
   plane at `z=-500` could never intersect anything in this scene.

## Preconditions checked

- `pgrep -x scan_server` / `-x raptor_client` / `-x OpenScanner`: empty
  before starting (name-anchored, rule 13). No conflicting process.
- Binary freshness (rule 12): `build/tools/scan_server/scan_server` and
  `tools/scan_server/main.cpp` both mtime Oct 19 14:59; `cmake --build build
  --target scan_server` was a no-op (`Built target scan_server`, no
  recompile) — the already-fresh binary is current against source.
- `~/Library/Logs/DiagnosticReports/`: most recent `scan_server-*.ips` before
  and after this attempt's two runs was still `scan_server-2026-10-19-145954.ips`
  (attempt 2's crash) — no new crash report from either run in this attempt.
- No separate `OpenScanner.app` GUI process was observed or required at any
  point. #71's target is the `scan_server --live` CLI binary directly (same
  as attempts 1–3); a mid-task question about an app-launch issue raised
  during this session was separately cleared by the coordinator as not a
  blocker for this CLI-only row.

## Step 1–2: baseline run, recorded

```
./build/tools/scan_server/scan_server --live 8787 "" 15 --record <baseline.rec>
```
Raw log: `tests/results/2026-10-19-plan71-plane-removal-attempt4-baseline.log`
Raw recording: `tests/results/2026-10-19-plan71-plane-removal-attempt4-baseline.rec`

Interposer confirmed loaded (`[vulkan_disable_interpose] loaded`, line 2).
Device enumerated (`count=1`), clean exit (`Worker exited normally with code 0`).

Per-tick log:

| tick | num_points |
|---|---|
| 0/150 | 0 |
| 50/150 | 194 |
| 100/150 | 201 |

To get real scene geometry (no API for extents directly; the `--record`
flag writes raw V1 frames to disk per the file's own usage comment), the
`.rec` file was parsed offline with a standalone script
(`tests/results/2026-10-19-plan71-plane-removal-attempt4-extents.py`, kept
outside the source tree's build graph — it only reads the already-written
recording, it does not touch or run against the device) against the
documented frame layout in `engine/include/openscanner/frame_protocol.h`
(read-only; not `obscan_api.h`) and the 16-byte `OSCR` record-file header
visible in `tools/scan_server/main.cpp`'s `RecordFrame`. Result:

```
frames=302 cloud_frames=152 total_points_summed=32699 max_points_in_single_frame=2444
x: [-0.0839, 0.1289]
y: [-0.0657, 0.1170]
z: [0.2006, 0.4597]
```

Units read as meters; z (depth from camera) spans roughly 0.20–0.46 m.

## Step 3: plane value chosen

`OPENSCANNER_MANUAL_PLANE="0,0,1,-0.33"` — a plane `z=0.33`, the
approximate midpoint of the observed baseline z-extent (0.20–0.46 m), so it
is expected to actually pass through the scanned geometry rather than sit
far outside it as attempt 3's `-500` did.

## Step 4: plane-removal-enabled run, recorded

```
OPENSCANNER_PLANE_REMOVE=1 OPENSCANNER_TURNTABLE_PLANE_REMOVE=1 \
OPENSCANNER_MANUAL_PLANE="0,0,1,-0.33" \
./build/tools/scan_server/scan_server --live 8787 "" 15 --record <enabled.rec>
```
Raw log: `tests/results/2026-10-19-plan71-plane-removal-attempt4-enabled.log`
Raw recording: `tests/results/2026-10-19-plan71-plane-removal-attempt4-enabled.rec`

Interposer confirmed loaded. Device enumerated (`count=1`). Toggle binding
fired, including the manual-plane setter (new vs. attempt 3, which only
confirmed the first two):

```
[worker] obscan_scan_plane_remove(true)                -> OB_SCAN_SUCCESS (0)
[worker] obscan_scan_turntable_plane_remove(true)      -> OB_SCAN_SUCCESS (0)
[worker] obscan_scan_config_manaul_remove_plane(a,b,c,d) -> OB_SCAN_SUCCESS (0)
```

Per-tick log:

| tick | num_points |
|---|---|
| 0/150 | 0 |
| 50/150 | 104405 |
| 100/150 | 225293 |

Clean exit (`Worker exited normally with code 0`). Offline extent parse of
this run's `.rec`:

```
frames=330 cloud_frames=180 total_points_summed=2907720 max_points_in_single_frame=20000
x: [-0.1403, 0.1868]
y: [-0.0835, 0.1169]
z: [0.1534, 0.4910]
```

**The chosen plane (`z=0.33`) does fall inside both runs' observed z-extent**
(baseline 0.20–0.46; enabled 0.15–0.49) — confirmed it was a geometrically
plausible cut this time, unlike attempt 3's `-500`.

## Comparison

The per-tick `num_points` series is **not comparable** between the two runs:
the enabled run's counts (104405 at tick 50, 225293 at tick 100) are three
orders of magnitude *larger* than baseline's (194, 201) at the matching
ticks — the opposite direction a working plane-removal cut should produce
(fewer, not more, published points). This is the same failure mode attempt
3 hit: the two runs are sequential live scans, not a controlled A/B of
identical scene content. The physical scene was **not confirmed static**
across the two invocations — nothing in this session's tooling held the
subject/scanner fixed between runs, and the explosive growth in the enabled
run (0 → 104k → 225k points in the same 15 s window baseline took to reach
~200) indicates the scanned object/pose differed substantially, most likely
because whatever was in front of the camera changed or the scanner
accumulated a much richer surface on the second pass. No point-count
*reduction* attributable to plane removal can be read out of this data,
because the scene itself wasn't held constant — and a reduction could in
principle be masked by, or confused with, the scene-driven growth either
way.

## Checks

| # | Check | Result |
|---|-------|--------|
| 1 | Interposer loaded (banner present), both runs | [PASS] |
| 2 | Binary fresh against source (rebuild no-op, already current) | [PASS] |
| 3 | No conflicting SDK process before either run | [PASS] |
| 4 | Device enumerated (`count` > 0), both runs | [PASS] `count=1` |
| 5 | Real scene z-extent recovered from recorded frames (not guessed) | [PASS] baseline z∈[0.20,0.46] m; enabled z∈[0.15,0.49] m |
| 6 | Manual plane value (`z=0.33`) geometrically intersects observed scene | [PASS] — inside both runs' z-extent |
| 7 | All three toggle-binding calls fire and succeed (incl. manual-plane setter) | [PASS] |
| 8 | Clean process exit, both runs | [PASS] exit code 0 |
| 9 | No crash in either run | [PASS] — no new `.ips` file appeared |
| 10 | Controlled A/B: same scene/pose held across baseline vs. enabled | **[FAIL]** — enabled run's point counts are ~500-1000x baseline's at matching ticks; scene was not static |
| 11 | Plane-removal point-count reduction observed vs. baseline | **[not resolved]** — no valid comparison possible given check 10's failure |

## Verdict

**BLOCKED** on #71's owed observation. This attempt fixes both problems
identified in attempt 3 (wrong metric, implausible plane value) and adds
real evidence the chosen plane geometrically intersects the scanned scene
in both runs — but the controlled-comparison precondition (same scene held
static across baseline and enabled runs) was not met: the enabled run's
published point counts are vastly larger than baseline's at matching ticks,
which reads as scene-content variation between two independent live scans,
not a plane-removal effect. Toggle-binding mechanics remain confirmed
working on real hardware ([hw-confirmed: this run] + attempt 3). The actual
removal *effect* on point counts remains unconfirmed after four attempts.

## Open questions

- A genuinely controlled test needs either (a) a fixture that reproducibly
  holds the same physical object/pose in front of the scanner across two
  separate process invocations, or (b) a single run that toggles the
  manual-plane config mid-session (the code's `apply_plane_config` lambda
  suggests toggles can be re-asserted after a resume) so one continuous scan
  can be compared against itself before/after enabling, removing the
  cross-run scene-variance problem entirely. The second option looks more
  promising given this session's data and should be tried before a fifth
  attempt repeats the two-separate-runs approach.
- Does `scan_server`'s tick counter reaching only `100/150` before teardown
  (never printing `150/150`) reflect the teardown path pre-empting the last
  tick, or a ~15s duration genuinely completing in slightly fewer ticks?
  Not investigated here; did not affect this attempt's conclusions.

## Board move

#71: recommend staying in `HW` (unchanged). The toggle-binding scope is
fully confirmed on hardware across attempts 3 and 4. The point-count
reduction *effect* is still unconfirmed — not because the plane value or
metric were wrong this time (both are now right), but because a true
same-scene A/B hasn't been achieved. Suggest the next attempt use a
single-session before/after toggle (per the open question above) rather
than two sequential process invocations.

## Files

- `tests/results/2026-10-19-plan71-plane-removal-attempt4.md` (this file)
- `tests/results/2026-10-19-plan71-plane-removal-attempt4-baseline.log` (raw log, baseline)
- `tests/results/2026-10-19-plan71-plane-removal-attempt4-baseline.rec` (raw V1 frame recording, baseline)
- `tests/results/2026-10-19-plan71-plane-removal-attempt4-enabled.log` (raw log, plane-removal enabled)
- `tests/results/2026-10-19-plan71-plane-removal-attempt4-enabled.rec` (raw V1 frame recording, plane-removal enabled)
- `tests/results/2026-10-19-plan71-plane-removal-attempt4-extents.py` (offline parser used to compute real scene extents from the `.rec` files; reads recordings only, never runs against the device)
- Prior records untouched: `tests/results/2026-10-19-plan71-plane-removal-blocked.md`,
  `tests/results/2026-10-19-plan71-plane-removal-blocked-v2-no-device.md` (+ its `.crash.ips`/`.log`),
  `tests/results/2026-10-19-plan71-plane-removal-attempt3.md` (+ its baseline/enabled logs)
