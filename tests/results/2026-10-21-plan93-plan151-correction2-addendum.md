# Correction 2 — 2026-10-21-plan93-hud-half.md and 2026-10-21-plan151-browser.md

Date: 2026-10-21
Corrects: `2026-10-21-plan93-hud-half.md` and `2026-10-21-plan151-browser.md`
(supplements `2026-10-21-plan93-hud-half-correction.md` and
`2026-10-21-plan151-browser-correction.md`)

## Item 1 — Unsourced Run start timestamps

Both records carry wall-clock `Run start` times that have no source in the
session log (which has no timestamps).

### 2026-10-21-plan151-browser.md, line 13

Original:
> `- Run start (browser connection): 2026-10-21 ~14:34 (after ~10 min of device warm-up)`

The session log has no timestamps. The `~14:34` figure was coordinator-reported
(coordinator observed the browser pane clock when connecting). This is noted as
coordinator-attributed, not log-sourced.

Corrected line:
> `- Run start (browser connection): 2026-10-21 ~14:34 (coordinator-reported wall clock;
>   session log has no timestamps; ~10 min after preflight at 14:23:19)`

### 2026-10-21-plan93-hud-half.md, line 14

Original:
> `- Run start (HUD observation): 2026-10-21 ~14:40 (same scan_server session; browser pane connected)`

Same issue — `~14:40` is coordinator-reported, not log-sourced.

Corrected line:
> `- Run start (HUD observation): 2026-10-21 ~14:40 (coordinator-reported wall clock;
>   session log has no timestamps; same scan_server session as plan151-browser)`

## Item 2 — Missing raw log companion

Both records lacked a same-stem `.raw.log` companion. The shared session log
(`2026-10-21-plan151-93-153-session.raw.log`, 32466 lines) already exists but
does not satisfy the same-stem requirement.

**Decision: option (a) — same-stem extracts** (not rename to `-analysis.md`).
Renaming would change already-written record filenames and all citations between
them; an extract is sufficient evidence and leaves the records intact.

Two extract files have been written alongside this addendum:

- `2026-10-21-plan151-browser.raw.log` — lines 2363–5700 of the session log
  (server perf windows with `clients=1.0`, `bcast_send n=103`, `bcast_drops=0`;
  the window in which the browser was connected and broadcasting)

- `2026-10-21-plan93-hud-half.raw.log` — lines 18270–19305 of the session log
  (LOST/FAILURE_MARKERLESS/SUCCESS `tracking_status` sequence around the
  occlusion event) plus the session histogram from line 32465

Both extract files carry a header identifying their source line range and
the full session log they are drawn from.

## Verdicts and board moves

No change to any verdict or board move in either record.
