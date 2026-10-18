# PLAN #32 — scale acceptance, rerun procedure

**Goal**: demonstrate row #32's acceptance bar on hardware —

> the UI stays live (usable data rate, render 60 fps) at ≥1M accumulated points.

The first run ([`../results/2026-10-18-scan-server-scale-acceptance.md`](../results/2026-10-18-scan-server-scale-acceptance.md))
showed the fix holds — no stall, clean teardown — but never reached the bar: max live
`color_cloud` 306,327, because the session covered too little new geometry. This procedure is
sized to reach it.

## Why 300 s, and what to expect

After the operator began moving the scanner, the first run's series accumulated at roughly
**5–7k points/s** (19,016 → 306,327 in ~40 s). Reaching ≥1M therefore needs ~150–200 s of
*sustained* movement — so run **300 s** and keep moving across new geometry the whole time. If the
count plateaus, change area, angle, or distance: fusion only grows while new geometry enters the
field of view.

## Preconditions

1. In the hardware room (`Open3DScanner-wt-hw`, branch `hw/next`): `./sync-to-main.sh` — it
   refuses if a run is active, fast-forwards to `origin/main`, and rebuilds.
2. No other SDK user (rule 7): `ps aux | grep -iE "scan_server|live_capture_test|smoke_test|_message_listener_test|obscan" | grep -v grep`
   — read the output rather than testing it for emptiness. It should show nothing but this run's own launcher/worker; anything else
   attached to the SDK is a second user and must be stopped first. (`grep -v grep` matters: without it the pipeline matches its own
   argv, and the pattern covers the loader's users — `scan_server`, `live_capture_test`, `smoke_test` — not just `scan_server`.)
3. Serve the UI so the HUD can be read: `ui/watch.sh` (serves `ui/` on :8788; the page connects to
   `ws://127.0.0.1:8787`). Keep it visible — the HUD's `data fps` / `render fps` / `latency` and
   point count are the evidence the bar is about.

## Run

```
./build/tools/scan_server/scan_server --live 8787 "" 300
```

Move the scanner continuously across new geometry (a furnished room; different objects and angles).
Watch the HUD: the count should climb steadily; if it stalls, move to new geometry.

## Record (in `../results/`, one file per run, never edited in place)

- `2026-10-18-scan-server-scale-acceptance-rerun.md` — the record:
  - the command, the room's HEAD, and the binary mtime vs `tools/scan_server/main.cpp`'s mtime;
  - the HUD series — `data fps`, `render fps`, `latency`, point count — sampled about every 15 s,
    and **especially the top end**: the last ~30 s and every reading at or above 1,000,000;
  - the max live `color_cloud` count and the final post-pause mesh count;
  - the teardown: pause → mesh → stop → destroy, the exit code, and whether Ctrl+C was needed.
- `2026-10-18-scan-server-scale-acceptance-rerun.raw.log` — the tool's full stdout/stderr, unedited.
- If anything out-of-band happens (an operator action the log can't show, a crash, a kill), add an
  **operator note** alongside — additive, naming the record it supplements, following the
  `2026-10-18-scan-server-scale-acceptance-operator-note.md` precedent.

## Verdict

- **PASS** = the HUD shows ≥1,000,000 accumulated points with `render` at ~60 fps and a usable
  `data` rate at that scale, no multi-second tick stalls, clean teardown, exit 0.
- **FAIL** = state which condition missed and what the series did. A partial result (points reached,
  render dropped) is a *build* finding — client-side decimation — not another hardware rerun.
