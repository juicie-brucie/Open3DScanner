# PLAN #112 — conditions 4-6 rerun to capture HUD dataFps/renderFps — addendum to the clean 6/6 baseline

Per `tests/hardware/README.md` ("Results are written to `../results/`, one
file per run, never edited in place"), this is a separate record, not an
edit to `2026-10-19-plan112-tick-latency-baseline.md`. Conditions 1-3 are
**not** rerun here and stay as already recorded in that file; this covers
only conditions 4-6, specifically to capture the HUD's client-side
`dataFps`/`renderFps` (the one field that record was missing, since it's
painted client-side only with no server endpoint).

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server (same binary as the 6/6 baseline — build-tree copy, mtime 18:41:20, source main.cpp 14:59:05)
- Preflight: pwd OK; no conflicting process; run start 2026-10-19 21:04:43
- Interposer: `[vulkan_disable_interpose] loaded` expected/printed, same as baseline
- Device: Creality Raptor Pro, scene/target positioned, same setup as the 6/6 baseline
- Three independent data sources for this run, all folded in below: (1) this agent's server-side raw log + tick log, (2) the coordinator's own browser-pane HUD samples at ~5s intervals (quoted below, attributed), (3) the user's screen recording at `/Users/brucemackay/Desktop/Comp-Recording-2026-10-19-at-21.13.13.mp4` (320px wide, 1fps, no audio, exported from a 4x-speed capture per the user's `ffmpeg` export command — see the dedicated section below for how that file's timestamps were actually read).

## Commands

```
scripts/hw-preflight.sh scan_server
./build/tools/scan_server/scan_server --live 8787
```

Two false starts this session (both discarded, no condition run): the
server was started and torn down cleanly twice while waiting for the
coordinator's/user's readiness confirmation, before this final attempt.
Both intermediate shutdowns were clean (`SIGINT`, `Worker exited normally
with code 0`, confirmed via `scripts/hw-postflight.sh` — no crash report
either time). This record covers only the final attempt, run start
2026-10-19 21:04:43.

## Condition 4 — (b) UI open, STILL

- Window: 21:05:56–21:06:26
- Server-side: tick p50 ~115.1–122.7 ms, p95 ~137–264 ms, max ~170–404 ms (several single-tick outliers within this condition, not diagnosed); points/frame ~11,990→16,602 (still building up from this run's own cold start, since conditions 1-3 weren't rerun before this); rate ~6.8–8.4/s
- **Coordinator's independent HUD samples** (t=0,5,10,15,20,25,30s): data=7.7,7.7,6.0,7.8,8.0,6.0,8.0 fps; render=60.0,59.9,60.0,60.1,59.9,60.0,59.9 fps; frames #797,#907,#1005,#1097,#1185,#1319,#1423
- Cross-check: the HUD's `data` fps (6.0–8.0) and this run's server-side `rate=` (6.8–8.4/s) are in the same range, consistent with each other given they're different measurement points in the same pipeline. `render` fps steady at ~60 throughout, as expected for a GPU-vsync-bound render loop independent of data arrival rate.

## Condition 5 — (b) UI open, MOVED BY HAND, normal sustained speed (not the fast movement from the earlier, discarded attempt)

- Window: 21:06:30–21:07:00
- Server-side: tick p50 ~115.6–130.1 ms, p95 ~137–350 ms, max ~192–433 ms; points/frame ~17,423→19,775 (climbing, consistent with real movement/coverage growth); rate ~6.1–8.5/s
- **Coordinator's independent HUD samples** (t=35..60s): data=10.0,6.0,8.0,9.7,5.8,9.6 fps; render=59.9,59.9,60.0,60.0,60.1,59.5 fps; frames #1563,#1649,#1749,#1849,#1951,#2039; points climbed 16614→19791 over the window per the coordinator's own HUD read — consistent with this run's server-side points/frame trend above.

## Condition 6 — (b) UI open, PAUSED-RESUMED — **genuine UI click this time, confirmed three independent ways**

- Announced window: 21:07:08–21:07:59
- **This agent's server-side tick-log confirmation** (ground truth for the mechanism): `tick 1150/3000: color_cloud num_points=0 ..., clients=1, paused=1` (pause registered) → `tick 1200/3000: color_cloud num_points=906303 ..., clients=1, paused=0` (resumed). No WS-script fallback was used or needed. Bracketing SDK log timestamps for this transition: `21:07:32.106866` (just before) and `21:07:36.258224` (at/after) — so the pause/resume exchange happened within that ~4-second window, confirmed by this agent polling the log in real time during the condition and reporting it live once seen.
- **Coordinator's independent confirmation**, watching the live browser page state: at their t=65s sample, `scan="paused (live)"`, button showed "Start", `data=0.0fps`; at t=70s, `scan="scanning (live)"`, button back to "Pause", `data=9.7fps`. Full samples: t=65 (paused): data=0.0, render=60.0, frame=#2121, points=19564; t=70 (resumed): data=9.7, render=59.9, frame=#2215; t=75,80,85,90: data=7.7,8.0,8.0,8.0; render=60.0,60.0,59.9,60.0; frames #2285,#2383,#2483,#2575.
- **Third independent confirmation — the user's screen recording** (see dedicated section below): frame 114 (21:07:34 PM on the recording's own visible clock) shows `scan=paused (live)`, button changed to "Start", with the cursor visibly on the Start/Pause button — the click itself, captured on screen.
- **Timing cross-check, as understood at the time of the original fold-in:** the screen recording's clock matched this agent's own announcement timestamps exactly (frame 88 = 21:07:08 PM = this agent's logged Condition 6 announcement) and the recording's frame 114 (21:07:34 PM) appeared to match this agent's SDK-timestamp bracket (21:07:32.11–21:07:36.26) for the one `paused=1` tick-log line found at the time.
- **Correction, found during the deeper tracking-fault/latency pass below: there were actually *two* pause/resume cycles in condition 6, not one.** A first cycle happened at approximately 21:07:23–27 — recording frame 103 (21:07:23 PM) shows `scan=paused (live)`, `frame #2121`, **exactly matching the coordinator's own t=65s sample** (which this agent had been treating as loosely/approximately matched to the *second* cycle); frame 107 (21:07:27 PM) shows `scan=scanning (live)` again, `frame #2147`. This first cycle was **not caught by the server's periodic tick-log checkpoint** (which only prints every 50 ticks, ≈5.5–6s) — tick 1050 (before, paused=0) and tick 1100 (after, paused=0) both bracket it without ever printing the pause state, so the only direct server-side evidence of this first cycle is the tracking-status LOST cluster it produced (see below), not a `paused=1` tick-log line. The *second* cycle — the one with full tick-log and SDK-timestamp confirmation above — followed about 9 seconds later. **Both cycles were genuine UI clicks** (confirmed by the two independent HUD reads matching frame counters); this agent cannot determine from the available evidence why there were two (e.g., a double-click, or the user pausing, un-pausing, then pausing again) — not investigated further, flagged for whoever looks at this next.

## The screen recording — how its timestamps were actually read

The user's export command (`ffmpeg -i Recording-... -vf "scale=320:-2,fps=1" -vcodec libx264 -crf 30 -an Comp-Recording-...`)
only applies a scale + 1fps filter; the "4x speed" came from the original
screen-recording capture, before this export. That raised the question of
whether reading 1 video-frame-second = 4 real seconds for mapping purposes.

**The original, un-sped-up file does not exist on disk** — checked via
`ls`/`find` on `~/Desktop` (only an unrelated 2026-10-18 recording and the
compressed file from this session were present); nothing beyond that
listing was done with/to the original, since it isn't there. Per
instruction, only the compressed file was used throughout —
`/Users/brucemackay/Desktop/Comp-Recording-2026-10-19-at-21.13.13.mp4`,
154 frames extracted via `ffmpeg -vf fps=1`, 320x634, no audio.

**Legibility: good.** The macOS menu-bar clock is clearly readable in every
sampled frame at this resolution.

**Empirically, reading the visible clock directly across the 154 frames
shows a precise 1:1 ratio between video-frame-seconds and real elapsed
seconds** — frame 1 reads "Mon Oct 19 9:05:41 PM", frame 154 reads "Mon Oct
19 9:08:14 PM": 153 video-frame-seconds = 153 real seconds. No 4x
correction was needed or applied; each frame's visible clock was read
directly rather than relying on the assumed math from the export command's
description. Specific frames checked:

| Frame | Clock (recording) | What's visible | Cross-check |
| --- | --- | --- | --- |
| 1 | 9:05:41 PM | Browser pane still "connecting..." | Before Condition 4 (21:05:56) |
| 10 | 9:05:50 PM | `data=7.7fps`, `points=16142`, `frame #797` | Exact match to the coordinator's Condition 4 t=0 sample |
| 88 | 9:07:08 PM | On-screen agent-transcript pane shows this agent's own Condition 6 announcement text | Exact match (to the second) to this agent's logged Condition 6 announcement (21:07:08) |
| 103 | 9:07:23 PM | `scan=paused (live)`, button→"Start", `frame #2121` | **Exact match to the coordinator's t=65s sample** — the first, previously-undocumented pause cycle (see Condition 6 correction above) |
| 107 | 9:07:27 PM | `scan=scanning (live)`, `frame #2147` | Resume from the first cycle |
| 112 | 9:07:32 PM | `scan=scanning (live)` — not yet paused again | Matches the "before" edge of this agent's SDK-timestamp bracket (21:07:32.11) for the second cycle |
| 114 | 9:07:34 PM | `scan=paused (live)`, button→"Start", **cursor on the button**, `frame #2243` | The second pause click, inside this agent's SDK-timestamp bracket |
| 118 | 9:07:38 PM | `scan=scanning (live)` — resumed | Resume confirmed for the second cycle |
| 154 | 9:08:14 PM | This agent's final "ALL 6 CONDITIONS COMPLETE" / shutdown text visible on screen; HUD shows `Brightness: 1`, `frame #-1` (stale, post-shutdown) | End of recording |

## One odd note from the coordinator's independent sampling — now also confirmed on screen, with precise timing

At their t=90s sample, the browser page's **"Brightness" field** (the UI's
label for the exposure slider — confirmed in `ui/index.html:85-89`: `<label>Brightness <input id="exposure" min="1" max="1000" value="900"></label>`, matching `tools/scan_server/main.cpp:398`'s `kDefaultExposure = 900`) read **"1"** instead of the steady "900" seen throughout. The coordinator flagged this as possibly a UI artifact or accidental slider nudge, not independently investigated by them. **The screen recording's frame 154 independently confirms the same reading** (`Brightness: 1`, same stale/post-shutdown frame showing `frame #-1`). Reading additional frames pinned the decline precisely: **frame 130 (21:07:50) still shows `Brightness: 900`; frame 138 (21:07:58) shows `Brightness: 81`; frame 145 (21:08:05) and frame 154 (21:08:14) both show `Brightness: 1`.** So the value declined **smoothly/progressively from 900 to ~1 over roughly 15 real seconds** (21:07:50→21:08:05), not a single instantaneous jump or one accidental slider nudge — it was still at 81, mid-decline, while the browser page's own transcript showed the condition-6 close-out text ("Resume confirmed... Close out condition 6 window and confirm final client state"), i.e. the decline was already underway *before* this agent's shutdown sequence began appearing in the transcript (that text appears starting around frame 145). This agent traced the field to its exact source (the exposure control, not a true "brightness" measurement). **Resolved by the operator (2026-10-19):** the decline was the operator **pushing the exposure down manually at the very end of the session** — the smooth 900→81→1 progression was their own slider drag, not a code path, a UI artifact, or vendor behaviour. No investigation is owed; row **#137** is closed.

## Visual analysis of the full 154-frame sequence

**This agent's own observations**, from visually inspecting a sampled subset
of the 154 extracted frames (not all 154 individually) spanning the full
recording. **Caveat, stated plainly: this is a 1fps/320px/compressed
sample of the full session — a coarse visual record, not a substitute for
the quantitative telemetry already captured above.** Nothing below should
be read as more precise than what a human skimming a low-res slideshow
could judge; it's observational, not diagnostic.

- **Point cloud growth — condition 4 (still):** visually flat. Comparing
  frames 16 (21:05:56, 16188 pts), 25 (21:06:05, 16334 pts), 35 (21:06:15,
  16402 pts), and 46 (21:06:26, 16525 pts) — the rendered point cloud's
  shape, extent, and orientation look essentially unchanged across the
  entire 30s condition despite the numeric point count ticking up slightly.
  Consistent with "still."
- **Point cloud growth — condition 5 (moved by hand):** not visually
  uniform across the 30s window. Frames 50 (21:06:30) and 65 (21:06:45)
  still show the same sparse, scattered-line pattern as condition 4. A
  visibly dense, recognizable blocky/object silhouette appears abruptly
  between frame 65 (21:06:45) and frame 72 (21:06:52) — frame 68
  (21:06:48) shows it mid-formation — and is essentially complete by frame
  80 (21:07:00, condition 5's end). So the visible "coverage growth" this
  condition produced looks concentrated in roughly a 7–15 second span in
  the back half of the window, not spread evenly across the full 30s, going
  by the frames sampled.
- **Point cloud growth — condition 6 (post-resume):** continues
  growing gradually and steadily from frame 100 (21:07:20, 19636 pts)
  through frame 130 (21:07:50, 19900 pts) — no visible gaps, freezes, or
  sudden jumps in this later stretch.
- **Condition-boundary transitions (4→5 at frame ~46/50, 5→6 at frame
  ~80/88):** no visible glitches, flashes, black frames, or rendering
  artifacts at either boundary in the sampled frames — the point cloud and
  surrounding UI panels render continuously across both transitions.
- **Camera/orbit behavior:** no visible rotation or reframing of the point
  cloud across any sampled frame, despite a "Camera: orbit" button being
  present in the UI. The cloud's on-screen position and orientation look
  identical throughout the entire recording — no evidence of active orbit
  manipulation during this session.
- **Coverage pattern vs. claimed movement:** broadly consistent — "still"
  (condition 4) shows no visible growth, "moved by hand" (condition 5) shows
  a visible surge. The one caveat already noted above: the surge isn't
  visually spread evenly across condition 5's 30s, it's concentrated later
  in the window per the sampled frames.
- **Visual vs. the HUD's reported 60fps render:** not assessable from a
  1fps sample — nothing about smoothness/choppiness of rendering can be
  judged from one frame per second, so this agent is **not** drawing a
  conclusion here either way. The HUD's own reported `render` fps stayed in
  a tight 59.5–60.1 band across every sampled frame, for what that's worth
  as a separate (non-visual) data point.
- **Pause/resume moment (frames 112–118), beyond the HUD text:** no
  visually obvious change in the point cloud's own rendered density/shape
  between frame 112 (pre-pause, 19847 pts) and frame 118 (post-resume,
  19614 pts) beyond the HUD label changes already documented above — the
  already-rendered cloud appears to simply remain on screen through the
  pause rather than visibly clearing or freezing in an obvious way at this
  sampling rate. (The same holds for the first, newly-identified pause
  cycle at frames 103–107, per the deeper pass below.)
- **Other anomaly worth flagging:** the Brightness/exposure slider's
  progressive 900→81→1 decline over ~21:07:50–21:08:05, detailed in the
  section above — visually confirmed as a smooth slider-thumb movement
  across frames 130→138→145, not a single jump between two frames.

## Deeper pass: tracking faults + latency drops, correlated with the recording and this agent's own inference

This section responds to a follow-up request to correlate every
tracking-status transition and every notable latency/rate drop in the
#112 logs against the recording, then synthesize what the combined
evidence suggests about the pipeline. **All inference below is this
agent's own hypothesis, clearly labeled as such — not a diagnosis, and not
something this agent has the authority or tooling to confirm. Flagged for
a human or recon/builder agent to validate.**

### 1. Every tracking-status transition away from SUCCESS

**This run (HUD-capture, conditions 4-6, has video coverage):**
`tracking_status samples=4749, SUCCESS=4721, LOST=28` (no
FAILURE_MARKERLESS this time — real tracking throughout). All 28 LOST
frames fall into exactly two clusters:

- **Cluster A** — raw-log lines 12212–12262 (15 frames), bracketed between
  21:07:23.665593 and 21:07:27.093087 — this is the **first,
  previously-undocumented pause/resume cycle** (see the Condition 6
  correction above). A vendor "frame will be dropped... size is larger
  than expected" warning (`VideoSensor.cpp:450`, `OB_SENSOR_IR_LEFT`,
  size=3249252 vs expected 2304000) printed at line 12209, one line before
  this cluster starts — **this agent initially read that as a likely
  cause of the tracking loss, but given the cluster's timing lines up
  exactly with the newly-found first pause click instead, the more
  likely explanation is that tracking simply reports LOST while the scan
  is paused (no new frames being matched), and the frame-drop warning's
  adjacency is probably coincidental, not causal.** Flagged as an open
  question below rather than asserted either way.
- **Cluster B** — raw-log lines 12992–13017 (13 frames), bracketed right
  at/after the second (tick-log-confirmed) pause cycle, ending by
  21:07:36.258224. Same likely explanation as Cluster A: tracking reports
  LOST around the pause transition itself.
- Tracking recovers to `SUCCESS` immediately after both clusters end — no
  lingering degradation observed in either case.
- **No tracking-LOST cluster was found in this run that is clearly
  independent of a pause event** — i.e., this agent did not find a
  transient tracking-loss burst during active (unpaused) scanning in this
  particular run, for whatever that's worth.

**The original 6/6 baseline run (conditions 1-6, no video coverage at
all — the recording only covers the separate HUD-capture rerun above, not
this run):** `tracking_status samples=8119, SUCCESS=7904, LOST=215`
(reported previously), in **18 separate clusters** spread across the run
(raw-log line ranges, roughly ordered: 5239–5263, 5286–5333, 9146–9188,
16309–16348, 18703–18808, 19621–19664, 23613–24359 — several
sub-clusters in that last span — and 24608–24658). Several of these
align roughly with known condition boundaries/pause events in that run
(e.g. 9146–9188 sits right at condition 3's pause window; 24608–24658
sits right at condition 6's fallback pause), but several others (e.g.
5239–5333, 16309–16348, 18703–19664) do **not** obviously correspond to
any pause event in that run's own record and would need the same kind of
line-by-line bracketing done here to characterize — **not done in this
pass, noted as a gap.** No visual correlation is possible for any of
these — flagging per the task's own scope note, this is log evidence
only.

### 2. Every notable latency/rate drop

**This run (HUD-capture):** the most notable `[perf] window` tick-latency
outliers (tick `max` ≥ 400ms), all well short of the baseline run's
extreme case below:

| Raw-log line | tick p50/p95/max | rate | Approx. time | Condition |
| --- | --- | --- | --- | --- |
| 630 | 117.3/241.1/**446.1**ms | 7.2/s | ~21:05:03 (early, pre-condition-4 warm-up) | n/a |
| 1423 | 118.7/258.8/**403.6**ms | 7.3/s | ~21:05:17 | n/a (warm-up) |
| 2736 | 116.0/145.6/**416.6**ms | 7.9/s | ~21:05:32 | n/a (warm-up) |
| 3168 | 116.4/222.2/**485.7**ms | 7.5/s | ~21:05:37 | Condition 4 |
| 7451 | 117.9/246.9/**410.3**ms | 7.4/s | ~21:06:16 | Condition 4/5 boundary |
| 9104 | 119.6/224.1/**732.7**ms | 6.8/s | ~21:06:43 (interpolated — no SDK timestamp within 36s either side; see below) | Condition 5 |
| 10333 | 121.5/299.2/**429.7**ms | 6.8/s | ~21:06:55 | Condition 5 |
| 11578 | 124.7/298.5/**433.5**ms | 6.1/s | ~21:07:14 | Condition 5/6 boundary |

The line-9104 window (732.7ms, the largest in this run) has **no SDK
timestamp line within 36 seconds on either side** (the surrounding
stretch is dense `FRAME_READY` output with no vendor log noise), so its
time was estimated by proportional interpolation between the nearest
timestamped lines before and after — stated explicitly as an estimate,
not a direct read.

**The original 6/6 baseline run:** 5 windows with tick max ≥ 400ms total,
including the two extremes already on record: **6410.48ms** at line 16662
(~19:55:13–25, condition 5 — the outlier already investigated in a
separate addendum) and **1373.16ms** at line 17948 (condition 5, already
noted in that record). **Neither has any video coverage** — the recording
analyzed in this addendum is from the separate HUD-capture rerun, not
this baseline run. Per the task's scope note, this is log evidence only.

### 3-4. Frame-level visual correlation for the HUD-capture run's events

| Event | Time | Nearest frame(s) | What's visible |
| --- | --- | --- | --- |
| Latency spike, 732.7ms (line 9104) | ~21:06:43 (estimated) | 65 (21:06:45) | Still the sparse/pre-densification cloud pattern (see visual-analysis section above); `data=6.0fps` (a visible dip from the ~8fps elsewhere in this condition), `points=16882`. No freeze, corruption, or missing-frame artifact visible in the rendered cloud itself at this resolution — the dip shows up in the fps counter, not as a visible gap in the point cloud. |
| Latency spike, 433.5ms (line 11578) | ~21:07:14 (estimated) | 94 (21:07:14 exactly) | `data=5.8fps` (modest dip), `points=19708`, cloud shape stable and consistent with neighboring frames — again, no visible artifact in the rendered cloud. |
| Tracking LOST cluster A / first pause (21:07:23–27) | 21:07:23–27 | 103 (paused, frame #2121), 107 (resumed, frame #2147) | HUD text changes (`scanning (live)` ↔ `paused (live)`, button toggle) are the only visible change; the point cloud's own rendered density/shape does not visibly clear, freeze-frame, or jump between these two frames — it looks like the same accumulated cloud, simply not growing while paused. |
| Tracking LOST cluster B / second pause (21:07:32–36) | 21:07:32–36 | 112 (pre-pause), 114 (paused, cursor on button), 118 (resumed) | Same as above — HUD-only visible change, no visible cloud artifact. |

**Overall for this run: no case was found where a numeric latency spike or
tracking-LOST event produced a visually obvious artifact in the rendered
point cloud at 1fps/320px** — in every instance, the only visible change
was in the HUD's text fields (fps counters, status, pause/resume state),
not in the 3D cloud rendering itself. Stated as what was (not) observed,
not as proof nothing happened at full frame rate/resolution — this
sampling could simply be too coarse to catch a single-frame glitch.

### 5. Synthesis — this agent's own hypothesis, not a diagnosis

Based only on the evidence gathered in this run (plus general familiarity
with the codebase from earlier tasks this session, not new code-reading
done specifically for this synthesis):

- **Point cloud behavior during tracking loss/pause looks like a simple
  freeze, not prediction or smoothing.** In every tracking-LOST/pause
  instance observed here, the already-rendered cloud appears to just hold
  its last state rather than being cleared, interpolated, or extrapolated
  forward. This is consistent with (not proof of) the pipeline having no
  visible temporal smoothing or dead-reckoning during a gap — it simply
  stops adding data and resumes adding data later, with nothing happening
  to the display in between. Worth validating against the actual
  rendering code (`ui/app.js`'s render loop and whatever buffers the
  accumulated cloud) rather than taken as confirmed from frames alone.
- **Latency spikes in this run don't visibly correlate with lost/skipped
  frames in the rendered cloud** — the two spikes checked here (732ms,
  433ms) show up only as a modest fps-counter dip, with the cloud
  continuing to accumulate normally around them. This suggests (does not
  confirm) these spikes are processing-time delays on an otherwise-kept
  frame, rather than events that visibly discard content — though a
  single slow tick wouldn't necessarily be visible at 1fps/320px even if
  it did drop something, so this is a weak inference.
- **The much more frequent "frame will be dropped" vendor SDK warnings
  (14+ instances across this run, scattered roughly every 5-25 seconds)
  appear to be absorbed without visible or even numeric impact most of the
  time** — only two of these many drop-warning instances coincide with a
  tracking-LOST cluster, and both of those coincidences are better
  explained by the nearby pause events than by the drops themselves (see
  above). This suggests the pipeline's handling of routine vendor-side
  frame-size mismatches is decoupled from both the rendered output and
  tracking stability in the common case — i.e., it looks like these are
  already being filtered/discarded cleanly upstream of anything visible,
  which is a mild positive signal about robustness to this *particular*
  class of noise, though again inferred from a coarse sample, not
  confirmed.
- **No visible sign of outlier rejection, interpolation, or recovery
  animation was found** — transitions (sparse→dense cloud formation,
  pause→resume) look like direct, un-smoothed accumulation/resumption in
  every sampled frame. This reads as a comparatively "raw pass-through"
  visualization rather than one with active filtering/smoothing logic
  visible in its output, but this agent has not read the rendering code
  specifically to confirm or rule that out — it's an inference from
  appearance only.
- **Overall**, nothing in this pass contradicts the "clean run" verdict
  already on record, but it also doesn't provide positive visual evidence
  of any active recovery/smoothing sophistication beyond straightforward
  pause-and-resume and steady accumulation. Whether that's a gap worth
  addressing (vs. simply not needed) is a product/design question outside
  this agent's role.

## Bracket-sampling experiment (±3 frames around one point of interest)

**Chosen point of interest:** the first pause/resume cycle's tracking-LOST
cluster (Cluster A, ~21:07:23–27) — picked because it ties together a
button click, a frame-counter freeze, and a tracking-status change in one
compact sequence, so it seemed likely to reward closer sampling.

**The bracket: frames 100–106 (21:07:20–21:07:26, ±3s around frame 103).**

| Frame | Clock | scan | button/cursor | frame # | fps (data) | tracking |
| --- | --- | --- | --- | --- | --- | --- |
| 100 | 21:07:20 | scanning (live) | — | #2095 | 4.0 | SUCCESS (0) |
| 101 | 21:07:21 | scanning (live) | cursor hovering **on Pause** | #2111 | 7.9 | SUCCESS (0) |
| 102 | 21:07:22 | **paused (live)** | button→"Start", cursor still near it | #2121 | 4.0 (mid-transition) | SUCCESS (0) |
| 103 | 21:07:23 | paused (live) | "Start", cursor on it | #2121 (frozen) | 0.0 | SUCCESS (0) |
| 104 | 21:07:24 | paused (live) | "Start", cursor moved away | #2121 (frozen) | 0.0 | SUCCESS (0) |
| 105 | 21:07:25 | paused (live) *(stale label)* | button shown as "Pause", cursor on it — **the resume click** | #2121 (frozen) | 0.0 | SUCCESS (0) |
| 106 | 21:07:26 | **scanning (live)** (resumed) | "Pause" | #2131 (jumped +10) | 3.9 | **LOST (1)** |

**What the bracket revealed that frame 103 alone did not:**
- **The actual pause click** happened between frames 101 and 102 (cursor
  visibly moves onto the Pause button at 101, state flips by 102) —
  invisible from frame 103 alone.
- **The frame counter freezes exactly at #2121** across frames 102–105 (4
  consecutive seconds), only moving again at 106 — confirms the pause
  genuinely halts frame production rather than just hiding it.
- **The resume click is visible at frame 105** — the button's displayed
  label flips to "Pause" a moment before the `scan` status text itself
  updates to "scanning (live)" at frame 106, a small UI-update-ordering
  detail only visible by having both frames adjacent.
- **Most importantly: the HUD's own `tracking` field explicitly reads
  `LOST (1)` at frame 106** — the resume moment — directly matching the
  tracking-status LOST cluster this agent found in the raw log (Cluster
  A). **Frame 103 alone showed `tracking: SUCCESS (0)`**, which on its own
  would have suggested (incorrectly) that this tracking-LOST event left no
  visible trace in the HUD at all. The bracket caught the one frame
  (106, the resume instant) where it actually shows on screen.

**Honest assessment of the ±3 bracket vs. a single frame or ±1:** for
*this specific event*, the ±3 bracket was clearly worth it, not marginal —
a single frame (103) or even a ±1 bracket (102–104) would have **missed
the frame 106 `LOST (1)` HUD text entirely**, since the pause held steady
at frames 102–104 and the tracking-LOST readout only appeared at the
resume moment, 3 seconds after the pause began. A narrower bracket would
have led this agent to (wrongly) report that the tracking-LOST cluster had
no HUD-visible counterpart. That said, this is **not a blanket
recommendation for ±3 everywhere**: the extra value here came specifically
from the event spanning multiple seconds with a delayed visible
consequence (the LOST readout at resume, not at pause). For a brief,
instantaneous event — e.g. the single-tick latency spikes checked earlier
in this addendum, where the surrounding frames 65–68, 94 etc. already
showed no change across much wider gaps than ±3s — a ±1 or even single
frame likely would have told the same story, since nothing in those cases
was observed to change frame-to-frame nearby. **Suggested rule of thumb,
not validated beyond this one case:** bracket wider (±3 or more) around
state-change events with a plausible delayed/propagating effect (pause,
resume, mode transitions); a single frame or ±1 is probably sufficient for
a momentary numeric spike with no visible state transition attached.

## IMU data recon check

Targeted `grep -ni` across both raw logs (`2026-10-19-plan112-conditions-4-6-hud-capture.raw.log`
and `2026-10-19-plan112-tick-latency-baseline.raw.log`) for IMU-related
terms, plus this project's own clean-room source (`tools/scan_server/`,
`engine/`) — explicitly staying off vendor headers and `.recon-cache/`.

**Found, in the vendor SDK's own log output (both runs, same pattern each
session):**
- `create client port cost:101-105ms, port name:Creality Raptor Pro IMU, client port:10011` — an IMU-named RPC port is opened every session.
- `AccelSensor.cpp:22 AccelSensor created` / `ScannerAbstractDevice.cpp:483 Accel sensor has been created!` at session start; `AccelSensor.cpp:30 AccelSensor destroyed!` at teardown.
- `GyroSensor.cpp:23 GyroSensor created!` / `ScannerAbstractDevice.cpp:519 Gyro sensor has been created!` at session start; `GyroSensor.cpp:31 GyroSensor destroyed!` at teardown.
- `RPCClientPort.cpp:381 Stop imu stream error: std::bad_cast` — a recurring warning (once per run near session start) when something tries to stop the IMU stream; this agent did not investigate what triggers it or what it implies, just noting it's there.
- `rotate pose for model.ply` followed by a static 4x4 matrix
  (`-0.000000044, -1.000000000, 0, 0 / 1, -0.000000044, 0, 0 / 0, 0, 1, 0 / 0, 0, 0, 1`),
  printed once per run during final mesh export/teardown (between
  `obscan_session_destroy` polls). This is a fixed ~90°-about-Z rotation
  matrix, not a per-frame/live value — reads like a one-time
  coordinate-system alignment applied to the exported `.ply`, not a
  continuous IMU pose stream. Flagging it since it's the only "pose"-like
  numeric data found, but it does not look like live orientation tracking
  output.

**Not found anywhere in either log:** "orientation", "angular", "quaternion",
"magnetometer", "tilt" — zero matches for any of these terms in either raw
log.

**Not found in this project's own clean-room code** (`tools/scan_server/*.cpp`,
`*.h`, `engine/`): no reference to IMU/accel/gyro/orientation/quaternion
anywhere — this project's own server code does not appear to read, surface,
or forward any IMU data from the vendor SDK today.

**Summary for the user's question:** the vendor SDK clearly constructs and
tears down IMU-related objects (an `AccelSensor`, a `GyroSensor`, and an
"IMU" RPC port) every session, so the *hardware channel* for IMU data
exists and is being opened. But **no actual IMU values (acceleration,
angular velocity, orientation, quaternion) appear anywhere in either raw
log**, and this project's own code doesn't reference or surface any —
so if IMU data is being used anywhere in this pipeline, it isn't visible
in anything captured by this run's logs, and this agent found no evidence
it's currently read, logged, or exposed. This can be treated as "not found
in the logs this run produced," not as "proven absent from the SDK or the
device" — a targeted look at whether the vendor API even exposes a
pollable IMU value (as opposed to just opening the port) would be a
recon-level question, not something resolvable from these logs alone.

## Full-sweep bracketing (±4 frames, all 9 frames read per point of interest)

Per the coordinator's correction, each bracket below is the **full 9-frame
window** (point of interest ± 4 frames at the recording's native 1fps),
every frame individually viewed — not a sampled subset. Same caveat as
before: 1fps/320px, observational only.

### Bracket 1 — Cluster B, the second pause/resume cycle (frames 110–118, 21:07:30–21:07:38)

| Frame | Clock | scan | button/cursor | frame # | data fps | tracking |
| --- | --- | --- | --- | --- | --- | --- |
| 110 | :30 | scanning (live) | — | #2191 | 9.7 | SUCCESS |
| 111 | :31 | scanning (live) | — | #2205 | 6.0 | SUCCESS |
| 112 | :32 | scanning (live) | — | #2219 | 3.9 | SUCCESS |
| 113 | :33 | scanning (live) | cursor nearing Pause | #2231 | 4.0 | SUCCESS |
| 114 | :34 | **paused (live)** | "Start", cursor on it — **the click** | #2243 (frozen) | 0.0 | SUCCESS |
| 115 | :35 | paused (live) | "Start" | #2243 (frozen) | 0.0 | SUCCESS |
| 116 | :36 | paused (live) *(stale label)* | "Pause" shown, cursor on it — **the resume click** | #2243 (frozen) | 0.0 | SUCCESS |
| 117 | :37 | **scanning (live)** (resumed) | "Pause" | #2253 | 7.8 | **SUCCESS** |
| 118 | :38 | scanning (live) | — | #2269 (approx.) | — | SUCCESS |

**The highlighted finding: unlike Cluster A's resume frame (106), which
showed `tracking: LOST (1)` in the HUD, Cluster B's equivalent resume
frame (117) shows `tracking: SUCCESS (0)`** — no visible LOST readout at
all, even though the raw log recorded a genuine tracking-status LOST burst
for this cluster too (lines 12992–13017, bracketed by this agent earlier
against the SDK timestamps). The full 9-frame sweep did not catch a LOST
reading anywhere in this bracket. The likely explanation (inference, not
confirmed): at 1fps, a LOST burst that resolves within under a second can
simply fall in the gap between two sampled frames — Cluster A's LOST
reading being caught was evidently a matter of timing luck, not something
that reliably shows up on screen. **This means the earlier single-frame
observation (no LOST text visible around the pause) cannot be read as "no
visible tracking disruption happens" — it can just as easily mean "this
particular burst didn't line up with a sampled frame."** Worth noting as a
real limit of 1fps sampling, not resolved by more bracketing at this frame
rate.

**Frame counter freeze confirmed again**: #2243 held constant across all
of frames 114–116 (3 seconds), consistent with Cluster A's behavior.

**Worth it?** Yes, clearly — this bracket is what surfaced the
LOST-vs-SUCCESS asymmetry between the two clusters, which a single frame
(114, matching the earlier partial read) would have completely missed.

### Bracket 2 — Condition 4→5 boundary (frames 44–52, 21:06:24–21:06:32)

| Frame | Clock | scan | frame # | points | tracking |
| --- | --- | --- | --- | --- | --- |
| 44 | :24 | scanning (live) | #1295 | 16504 | SUCCESS |
| 45 | :25 | scanning (live) | #1313 | 16517 | SUCCESS |
| 46 | :26 (Condition 4 end) | scanning (live) | #1325 | 16525 | SUCCESS |
| 47 | :27 | scanning (live) | #1339 | 16537 | SUCCESS |
| 48 | :28 | scanning (live) | #1355 | 16547 | SUCCESS |
| 49 | :29 | scanning (live) | #1373 | 16551 | SUCCESS |
| 50 | :30 (Condition 5 start) | scanning (live) | #1407 | 16555 | SUCCESS |
| 51 | :31 | scanning (live) | — | 16576 | SUCCESS |
| 52 | :32 | scanning (live) | #1423 | 16584 | SUCCESS |

Fully smooth across all 9 frames: steady ~1.3–2 fps worth of frame-counter
advance per second, point count climbing by single-digit-to-teens per
frame, identical sparse cross-hatch cloud pattern throughout, no gap, no
flash, no stall. The agent-transcript pane's own text (visible in the
screenshots) transitions through "Confirmed, client connected... Starting
Condition 4" around frame 44–46 and "Client still connected... Condition 5
— normal sustained movement" by frame 49–52, consistent with/confirming
this agent's own announcement timing.

**Worth it?** No — this fully confirms what the original reduced
pass (frames 42/46/50/52) already suggested. The extra 5 frames (44, 45,
47, 48, 49, 51) added confirmation but no new finding. A ±1 or even
single-frame check at the boundary would have told the same story here.

### Bracket 3 — Condition 5→6 boundary (frames 80–88, 21:07:00–21:07:08)

| Frame | Clock | scan | frame # | points | tracking |
| --- | --- | --- | --- | --- | --- |
| 80 | :00 (Condition 5 end) | scanning (live) | #1813 | 19495 | SUCCESS |
| 81 | :01 | scanning (live) | #1825 | 18856 | SUCCESS |
| 82 | :02 | scanning (live) | #1839 | 19497 | SUCCESS |
| 83 | :03 | scanning (live) | #1855 | 19084 | SUCCESS |
| 84 | :04 | scanning (live) | #1871 | 19811 | SUCCESS |
| 85 | :05 | scanning (live) | #1887 | 19687 | SUCCESS |
| 86 | :06 | scanning (live) | #1905 | 19804 | SUCCESS |
| 87 | :07 | scanning (live) | #1921 | 19024 | SUCCESS |
| 88 | :08 (Condition 6 announced) | scanning (live) | — | — | SUCCESS |

No visible glitch at the boundary itself, consistent with Bracket 2. One
mild point worth noting: the point count is **not monotonically
increasing** frame-to-frame here (e.g. 19497→19084→19811→19687→19804→19024)
— it fluctuates up and down by a few hundred points between consecutive
seconds, unlike the steady single-digit/teens increases seen in Bracket 2
(condition 4, "still") or the smooth climbs documented earlier in
condition 5's densification phase. This is plausibly just the normal
effect of continued hand movement changing what's in view frame-to-frame
(not a bug signal) — flagged as an observation, not interpreted further.

**Worth it?** Marginal — no new qualitative finding (no glitch), but the
full sweep did surface the frame-to-frame point-count fluctuation above,
which the sparser original pass (80/84/88 only) wouldn't have shown
clearly. Call it a soft "yes, mildly" rather than a strong one.

### Bracket 4 — Latency spike ~21:06:43 (frames 59–67, 21:06:39–21:06:47)

| Frame | Clock | data fps | points | tracking |
| --- | --- | --- | --- | --- |
| 59 | :39 | 8.0 | 16590 | SUCCESS |
| 60 | :40 | 5.8 | 16601 | SUCCESS |
| 61 | :41 | 8.0 | 16607 | SUCCESS |
| 62 | :42 | 5.8 | 16614 | SUCCESS |
| **63** | **:43 (estimated spike center)** | **1.9** | 16622 | SUCCESS |
| 64 | :44 | 5.8 | 16565 (down from 63) | SUCCESS |
| 65 | :45 | 6.0 | 16882 | SUCCESS |
| 66 | :46 | 7.7 | **18238** (+1356 vs. 65) | SUCCESS |
| 67 | :47 | 8.0 | 18770 | SUCCESS |

**The interpolated timestamp estimate for this spike (21:06:43) landed
almost exactly on frame 63's sharp fps dip (1.9fps, the lowest fps value
seen anywhere in this agent's sampling of this run)** — stronger
confirmation of the estimate than this agent had going into the bracket.
Two additional details the full sweep surfaced beyond the earlier partial
set:
- **Frame 64's point count (16565) is lower than frame 63's (16622)** —
  a small backward step, right after the fps dip. Minor, but the only
  other place in this agent's sampling where points visibly decreased
  frame-to-frame (besides the condition 5→6 boundary fluctuations above).
- **Frame 66 shows a disproportionately large point-count jump (+1356,
  vs. typical single/double/triple-digit frame-to-frame deltas elsewhere)
  right as fps recovers to near-normal.** Read together, this looks like
  (inference, not confirmed): a slow tick around frame 63 delayed some
  processing, and the catch-up produced a larger-than-usual point
  addition once it cleared — consistent with queued/batched work being
  flushed, rather than data simply being lost. Not confirmed against the
  code.

No visible cloud corruption, freeze, or rendering artifact at any frame in
this bracket — the sparse-to-dense transition continues smoothly through
and across the dip.

**Worth it?** Yes — the full sweep both nailed the spike's timing more
precisely (frame 63 exactly) and surfaced the point-count dip/rebound
pattern around it, neither of which the earlier partial set (62, 65, 68,
72, 75) made as clearly visible.

### Bracket 5 (control case) — Latency spike ~21:07:14 (frames 90–98, 21:07:10–21:07:18)

| Frame | Clock | data fps | points | tracking |
| --- | --- | --- | --- | --- |
| 90 | :10 | 5.8 | 19802 | SUCCESS |
| 91 | :11 | 5.8 | 19889 | SUCCESS |
| 92 | :12 | 7.7 | 19463 | SUCCESS |
| 93 | :13 | 5.8 | 19447 | SUCCESS |
| **94** | **:14 (estimated spike center)** | **5.8** | 19708 | SUCCESS |
| 95 | :15 | 7.7 | 19855 | SUCCESS |
| 96 | :16 | 5.9 | 19791 | SUCCESS |
| 97 | :17 | 6.0 | 19918 | SUCCESS |
| 98 | :18 | 7.8 | 19858 | SUCCESS |

This agent predicted, before reading the bracket, that this spike (433ms —
notably smaller than Bracket 4's 732ms) would show nothing distinctive,
and read the full 9-frame sweep anyway per instruction rather than
skipping it. **The prediction held**: fps stays in the same 5.8–7.8 band
seen throughout this condition, no frame shows an outlier dip like frame
63's 1.9fps, points fluctuate mildly up and down (±400ish) the same way
the condition 5→6 boundary bracket already showed, and nothing in the
cloud's appearance changes across any of the 9 frames.

**Worth it? No** — now backed by a full sweep rather than a guess. This is
the useful negative data point: a 433ms spike (vs. 732ms) apparently falls
below whatever threshold produces a visibly-sampleable effect at 1fps, so
for smaller/borderline latency numbers, bracketing doesn't appear to pay
off. Combined with Bracket 4, this suggests the bracketing technique's
payoff scales with the *size* of the underlying event (a 732ms outlier
showed clear effects; a 433ms one didn't), not just its presence in the
numeric log.

### Revised rule of thumb after the full-sweep pass

Refining the single-case heuristic from before: **bracket state-change
events (pause/resume, mode transitions) and the largest latency outliers**
— both paid off here. **Skip or keep narrow for condition boundaries with
no accompanying state change** (Bracket 2 confirmed nothing new) **and for
smaller/mid-size latency spikes** (Bracket 5 confirmed nothing). The two
pause/resume brackets also surfaced a real limitation: at 1fps, a
transient (<1s) tracking-status change can simply be missed by any bracket
width if it doesn't line up with a sampled frame — Cluster A's LOST
reading showing up and Cluster B's not showing up, despite both clusters
being real LOST bursts in the logs, is best explained by sampling luck,
not a real difference between the two events.

## Correction: actual visual geometry review of the two "worth it" brackets

**Self-correction, prompted by the user's question.** The two bracket
write-ups above (Cluster B and the 21:06:43 latency spike) leaned on the
numeric `points=` field and generic phrases like "no visible cloud
corruption" rather than describing what the rendered point-cloud
*geometry/shape* actually does frame-to-frame. That was a real gap — the
frames were viewed, but not described at the level of "where did density
appear, what shape changed." Re-reviewed both brackets below with that
specifically in mind.

### Cluster B (frames 110–118) — geometry, not just the count

Across all 9 frames, the rendered scene has two components: a **dense
white/gray blob** (roughly a tall rectangle with a notch, centered
horizontally-left-of-center, spanning about a third of the panel) and a
**sparser red diagonal-streak region** to its right, extending from
mid-height toward the top.

- **Frames 110–113 (pre-pause):** the dense blob's outline is static and
  identical across all four frames — same boundary, same internal
  speckle density, no new region appearing. The only thing visibly
  changing is the red streak region, which gains a small amount of
  additional density near its upper-right tip from frame to frame (subtle,
  not a new cluster — more fill-in of the existing streak shape).
- **Frames 114–116 (paused):** the geometry is **visually frozen** — not
  just the point *count* (already established from the tick-log), but the
  rendered shape itself is pixel-for-pixel indistinguishable across these
  three frames. No streak growth, no blob change, nothing moves.
- **Frame 117 (resumed):** the dense blob's outline is still unchanged
  from before the pause. The red streak region shows a small, localized
  increase in density in its upper-middle band (thicker/more filled than
  114–116) — consistent with new data resuming, but it's a modest,
  localized change, not a dramatic redraw.
- **Frame 118:** similar incremental density change to 117, same overall
  shape.

**Bottom line for this bracket:** the geometry genuinely does freeze
during the pause and resumes incrementally adding density to the same
existing streak region afterward — no visible jump, no new region
appearing elsewhere, no discontinuity in the blob's shape at any point
across the full 9 frames. This supports (more concretely than the
original write-up did) the "simple freeze, not prediction/smoothing"
read in this addendum's synthesis section — now based on an actual
frame-by-frame shape comparison, not just the frame counter/point count.

### Latency spike ~21:06:43 (frames 59–67) — geometry, not just the count

This bracket spans the sparse-to-dense transition already noted elsewhere
in this addendum, and re-reviewing it frame-by-frame for *where* the
geometry changes (not just when the count jumps) gives a sharper picture:

- **Frames 59–65 (including frame 63, the 1.9fps dip):** the rendered
  scene is a **loose criss-cross lattice of red/white/tan line segments**
  — no filled/solid region anywhere — occupying roughly the middle third
  of the panel. Across all seven of these frames (59, 60, 61, 63, 64, 65),
  this lattice is **visually static**: same segment positions, same
  density, same extent. Despite the severe fps dip at frame 63 (1.9fps,
  the lowest value seen anywhere in this run's sampling) and the numeric
  point count climbing modestly underneath (16590→16882), **nothing about
  the rendered shape itself changes or degrades at frame 63** — no
  dropout, no freeze-looking artifact, no visible sign of the stall at
  all in the geometry.
- **Frame 66 — the first visible geometry change in the whole bracket:**
  a **new, solid gray/white clump appears** in the lower-middle of the
  lattice region, in a spot that had only sparse crossing lines in every
  prior frame. This is the frame where the point count also jumps by
  +1356 (far larger than any other frame-to-frame delta in this run's
  sampling) — the visual and the scalar evidence agree: this is a real,
  localized new cluster, not just a count artifact.
- **Frame 67:** that same clump (same location) grows slightly denser/
  larger, while the surrounding red lattice lines stay in their existing
  positions — i.e. the growth is localized to the one new region, not a
  general density increase across the whole scene.

**Bottom line for this bracket:** the latency spike's aftermath produces
one specific, localized, newly-forming solid region (not visible before
frame 66, clearly visible and growing by frame 67) rather than a general
or diffuse change — a more precise finding than the original "a
disproportionate point-count jump" framing gave. Whether the delayed tick
at frame 63 is mechanically responsible for *this specific* clump forming
two frames later, or coincidentally lines up with it, is not something
this agent can determine from the frames alone — flagged as inference,
same as the earlier synthesis section.

## Baseline/control brackets: what normal accumulation looks like (±4 frames, full 9-frame sweep)

Two control brackets, picked deliberately away from all 6 already-bracketed
points of interest (the two pause clusters, the two condition boundaries,
the two latency spikes), to see what ordinary point-cloud growth looks
like visually vs. the fault-adjacent moments above. One from condition 4
(still), one from condition 5 (moved by hand), per the coordinator's
request. Same visual-geometry-first method as the correction pass.

### Baseline A — condition 4 (still), frames 26–34 (21:06:06–21:06:14)

| Frame | points | Visual |
| --- | --- | --- |
| 26 | 16361 | sparse red/white/tan criss-cross lattice, same region as every condition-4 frame sampled elsewhere in this addendum |
| 27 | 16368 | — |
| 28 | 16402 | — |
| 29 | 16420 | — |
| 30 | 16444 | — |
| 31 | 16448 | — |
| 32 | 16469 | — |
| 33 | 16477 | — |
| 34 | 16480 | — |

**Points climbed steadily, +119 over 8 seconds (consistent with the slow
climb documented elsewhere for condition 4). But visually, all 9 frames
are indistinguishable from each other** — same lattice segment positions,
same density, same extent, no new region, no visible thickening anywhere.
This is a real, notable finding on its own: **during "still," the numeric
point count's steady growth produces no detectable visual change in the
rendered geometry at this resolution.** The most likely explanation
(inference, not confirmed): the added points are refining/densifying
*within* the same already-covered sparse region at a sub-pixel level that
320px rendering simply can't show, rather than extending coverage to any
new area — consistent with "still" meaning the sensor keeps resampling
the same surface instead of seeing anything new.

### Baseline B — condition 5 (moved by hand), frames 71–79 (21:06:51–21:06:59)

| Frame | points | Visual |
| --- | --- | --- |
| 71 | 18448 | central clump already forming (elongated vertical shape, lower-middle), sparse lattice above it |
| 72 | — | clump visibly denser than 71 |
| 73 | 18533 | clump denser/taller again; **new sparse diagonal-line region appears on the left side**, not present in 71 |
| 74 | 19587 (**+1054** vs. 73) | clump visibly grows taller/wider — the largest single-frame size change in this bracket |
| 75 | — | clump holds size from 74; left region persists |
| 76 | 19943 | clump steady; top lattice visibly thicker/more filled than before |
| 77 | 18024 (**−1919** vs. 76) | visually almost no change from 76 despite the large numeric drop — clump and lattice look the same size |
| 78 | 18927 | clump/lattice visually same as 77 |
| 79 | 19993 | top lattice now visibly denser/more filled-in than any earlier frame in this bracket; clump and left region hold steady |

**This is visibly and qualitatively different from Baseline A.** Three
distinct kinds of change are visible across these 9 seconds: (1) the
central clump genuinely growing larger (71→74), (2) a **new region
appearing** on the left that wasn't there two frames earlier (73), and (3)
the red lattice region progressively filling in/thickening over the later
frames (76→79) — i.e. both *new coverage* and *densification of existing
coverage* are happening, not just one or the other. The numeric point
count is also far less well-behaved here than Baseline A's steady climb —
it swings by over a thousand points in either direction frame-to-frame
(−1919 at frame 77 is the largest single-frame drop recorded anywhere in
this agent's sampling of this run) **without a correspondingly large
visual change** — frame 77 looks almost identical to frame 76 despite
"losing" nearly 2000 points numerically. This reinforces a point already
raised elsewhere in this addendum: the `points=`/`num_points` scalar and
the rendered geometry do not move in lockstep, and large swings in the
number don't necessarily correspond to a visible change (and vice versa —
Baseline A's visually-static lattice still had steadily climbing points).

### Comparison: normal accumulation vs. the fault-adjacent brackets

| Bracket | Numeric behavior | Visual behavior |
| --- | --- | --- |
| Baseline A (still, normal) | steady small climb | **no visible change at all** |
| Baseline B (moved, normal) | large, noisy swings (±1000s) | **real, multi-region change**: existing region grows, new region appears, lattice densifies |
| Cluster B (pause, fault-adjacent) | frozen exactly | **frozen exactly** — geometry and count agree |
| Latency spike (fault-adjacent) | static, then one large jump at one frame | static, then **one new localized clump appears**, nowhere else |

The two normal-accumulation brackets bracket (so to speak) the two
fault-adjacent ones in character: Baseline A's invisible-but-real growth
and Baseline B's multi-region visible growth are **both** different from
what the latency spike produced (a single, abrupt, localized new clump
appearing at one specific frame rather than smooth/gradual growth in any
region), and obviously different from the pause's total freeze. This is
this agent's own observation from comparing the four brackets side by
side — not confirmed against the actual accumulation/rendering code,
flagged the same way as the rest of this addendum's synthesis.

## IMU data recovery feasibility — investigated, strong positive finding

**Protocol capture (`/Users/brucemackay/DevReposiMac/raptor-protocol-capture/`),
read-only, clean-room-compliant (recon-domain pcapng/tsv files, no vendor
headers or `.recon-cache` touched):**

- `tshark -Y "tcp.port==10011"` against all seven available capture files
  found **zero** port-10011 packets in the five `crealityscan-session_0001{9,20,21,22,23}...pcapng`
  files, but **real traffic** in `session2/session2.pcapng`,
  `session3/session3.pcapng`, and (the big one) `session4/session4.pcapng`
  — **66,276 packets** on port 10011 in `session4` alone.
- `session4/small_payloads.tsv` (already-extracted small payloads, tab-separated: timestamp, srcport, dstport, hex) has **33,130 lines** on port 10011.
- Decoded a sample using the **already-existing `msgpack_decode.py`** in
  that directory (no new tooling written, per instruction) — the first
  messages on this port are:
  ```
  [0, 1, b'ob_rpc_start_imu_stream0-2-16', []]       <- the vendor client explicitly starts an IMU stream
  [1, 1, None, True]                                  <- ack
  [0, 2, b'ob_rpc_get_device_report_infomation0-2-16', []]   <- polling call
  [1, 2, None, b'']                                   <- empty response (stream not yet producing data)
  ```
  repeating `ob_rpc_get_device_report_infomation` polls at first return
  **empty** (`b''`) responses for the first ~25 seconds of the capture.
- **Starting at t=25.37s, the responses stop being empty.** Scanning all
  33,130 port-10011 lines: **8,643 of the responses carry a substantial
  binary payload — every single one of them exactly 248 bytes**, spanning
  from **t=25.37s to t=111.40s** (≈86 seconds of continuous data) at a
  sample rate of **8,643 samples / 86s ≈ 100.5 Hz** — a rate squarely in
  the range typical of IMU accelerometer/gyro streaming (not of depth
  frames, which this run's own telemetry shows arriving at ~8–9/s, two
  orders of magnitude slower).

**This agent did not decode the internal byte layout of the 248-byte
blobs** (that would mean reverse-engineering an undocumented binary
format — a recon-level task, out of scope here and not attempted). But
the combination of (a) an explicit `ob_rpc_start_imu_stream` call, (b) a
fixed-size, high-rate (~100Hz), non-empty binary payload immediately
following it, and (c) the payload's size/cadence being distinct from every
other known data type in this capture, is strong circumstantial evidence
that **this capture already contains real IMU sensor data**, not just
empty framing.

**Today's own run logs** (both #112 raw logs) were also checked —
confirmed earlier in this addendum's IMU-grep section: they show the
`AccelSensor`/`GyroSensor`/IMU-RPC-port objects being created and
destroyed each session, and a recurring `Stop imu stream error:
std::bad_cast` warning, but **no raw IMU values or packet-level capture**
— `scan_server` itself never requests or logs IMU data, so nothing in
today's own log files (as opposed to the separate protocol-capture
directory) contains recoverable IMU values.

**Answering the user's question directly:**

- **Is IMU data recoverable without a hardware rework/retest? Likely
  yes, from the existing protocol capture** (`session4`, which already
  has 8,643 × 248-byte samples spanning 86 seconds, decodable with the
  tooling already present in that directory) — **no new hardware run
  needed** to get *a* dataset; what remains is decoding the 248-byte
  record's internal field layout (byte offsets, scaling, axis order),
  which is a protocol-reverse-engineering task, not a data-collection one.
- **Minimal-effort path to a *live*, labeled IMU stream from `raptor_client`
  going forward** (if wanted in addition to/instead of decoding the old
  capture): this agent did not implement anything, but scoping it —
  `raptor_client` already implements the same msgpack-RPC + GVCP pattern
  used here (`tools/raptor_client/main.cpp`), so adding a call to
  `ob_rpc_start_imu_stream` followed by polling
  `ob_rpc_get_device_report_infomation` on port 10011 looks like it would
  follow the exact same shape as the client's existing `start_stream`/
  `get_frame` calls on the depth/color ports — i.e. **this reads as a
  small, scoped addition to an existing tool, not new vendor SDK or
  protocol work**, since the RPC method names and the port are already
  known from this capture. Decoding the 248-byte payload's actual fields
  would still be needed either way, live or from the old capture — that
  part is the same amount of work regardless of which data source is
  used.

## Result

- Exit code: `0` (`[launcher] Worker exited normally with code 0.`, clean `SIGINT` teardown)
- Verdict: **PASS.** `tracking_status samples=4749 histogram: SUCCESS=4721 LOST=28`; `Frame protocol V1 counters: cloud_frames=1321 full_detail_chunks=82`.
- Raw log: `2026-10-19-plan112-conditions-4-6-hud-capture.raw.log` (same directory)

## Crash sweep

`scripts/hw-postflight.sh 2026-10-19-plan112-conditions-4-6-hud-capture '2026-10-19 21:04:43' scan_server`:

> hw-postflight: no crash report for 'scan_server' at/after 2026-10-19 21:04:43 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.

## Open questions

- Whether the two tracking-LOST clusters in this run are genuinely caused
  by the pause state itself (tracking naturally reporting LOST when no
  new frames are being matched) vs. something to do with the adjacent
  vendor frame-drop warning — not resolved here, flagged as the more
  likely but unconfirmed explanation.
- Why condition 6 had two separate pause/resume cycles rather than one —
  not investigated (could be a double-click, user confusion, or something
  else).
- The 18 LOST clusters in the original 6/6 baseline run that don't
  obviously align with a known pause event (several listed above) —
  not bracketed/characterized in this pass.
- The "Brightness"/exposure-value decline from 900 to 1 over ~15s
  (21:07:50–21:08:05, confirmed by three frames mid-decline) — not
  investigated. It was already underway before this agent's shutdown
  sequence appeared in the transcript, so it doesn't look like a shutdown
  artifact, but the cause is unknown.
- Why condition 5's visible point-cloud densification was concentrated in
  roughly the back half of its 30s window rather than spread evenly — not
  investigated; could be a real feature of how/when the user moved the
  scanner, or something about the reconstruction pipeline's own
  accumulation behavior.
- The user's audio call-outs (condition boundaries) were not available in
  this recording (`-an` strips audio entirely per the export command) —
  all cross-checks above rely entirely on the visible clock and on-screen
  HUD/agent-transcript state, not audio markers.
- This agent's synthesis section above (point-cloud freeze-not-smooth
  behavior, latency spikes not visibly dropping frames, no visible
  outlier rejection) is inference from a coarse 1fps/320px sample and
  general codebase familiarity, not confirmed by reading the relevant
  rendering/pipeline code for this specific question — flagged for
  recon/builder validation, not asserted as fact.
- Whether Cluster A's HUD-visible `LOST (1)` reading and Cluster B's
  absence of one reflect a real difference between the two pause events,
  or are purely an artifact of 1fps sampling missing a sub-second LOST
  burst in Cluster B's case — not resolved, flagged in the full-sweep
  bracketing section above.
- The 248-byte IMU binary payload's internal field layout (byte offsets,
  scaling, axis order for accel/gyro) has not been decoded — this agent
  confirmed the payload's existence, size, and rate as strong evidence of
  real IMU data, but did not attempt to reverse-engineer its contents.

## Board

- No row move proposed beyond what the original 6/6 baseline record already proposes (#112 ready for `HW` → `DONE`). This addendum supplies the previously-missing HUD `dataFps`/`renderFps` field for conditions 4-6, cross-checked **three** independent ways (this agent's server-side data, the coordinator's browser-pane samples, and the user's screen recording), with the pause/resume mechanism for condition 6 confirmed genuine (UI click, not the WS-script fallback) by all three — refined during a deeper pass to reveal **two** genuine pause/resume cycles rather than one — the earlier-flagged ~31-35s timing discrepancy resolved as a mapping-assumption error rather than a real clock skew, a supplementary qualitative visual-analysis pass added, and a further correlated tracking-fault/latency-drop analysis added with this agent's own hypotheses clearly flagged for human/recon validation.
- **IMU data note for whoever picks this up next:** the existing protocol capture at `/Users/brucemackay/DevReposiMac/raptor-protocol-capture/session4/` appears to already contain ~86 seconds of real IMU stream data (8,643 fixed-size 248-byte binary records at ~100Hz, following an explicit `ob_rpc_start_imu_stream` call), decodable with the `msgpack_decode.py` tooling already in that directory — this looks recoverable without a new hardware run, pending someone reverse-engineering the 248-byte record's field layout.
