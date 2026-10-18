# PLAN #56 — the stall test (acceptance) + V5's page-level visual (#53)

**Goal**: two acceptances in one session —

> **#56**: "repeat the stall-the-client test — the stalled client may disconnect, but the
> server process and every other client's session must survive."
>
> **#53**: the page-level visual test — WebGPU where available, the forced `?backend=webgl2`
> path, with the HUD's `backend` line recorded.

## Why this run exists

The Test A rerun
([`../results/2026-10-18-scan-server-backpressure-follow-smoke-rerun.md`](../results/2026-10-18-scan-server-backpressure-follow-smoke-rerun.md)
+ its addendum) confirmed follow mode and then **crashed the whole server**: stalling the
tab's JS thread for 3 s made a 100 ms `SO_SNDTIMEO` send abort mid-frame, and the next write
to the reset peer raised `SIGPIPE` — exit 2, zero teardown, **every** client's session lost.
The fix (#146) suppresses `SIGPIPE` (process-wide + per-socket) and prunes the stalled client
instead of dying. This procedure re-runs the identical stall against the fix.

## Preconditions

1. In the hardware room (`Open3DScanner-wt-hw`, branch `hw/next`): `./sync-to-main.sh` — then
   check `git log --oneline -5` **actually shows the #146 merge**. A room without it tests
   nothing; stop if it is absent.
2. No other SDK user (rule 7):
   `ps aux | grep -iE "scan_server|live_capture_test|smoke_test|_message_listener_test|obscan" | grep -v grep`
   — read the output; it should show nothing but this run's own launcher/worker.
3. Serve the UI so the HUD can be read: `./ui/watch.sh` (serves `ui/` on :8788; the page
   connects to `ws://127.0.0.1:8787`). Open **two** browser tabs — one is the stall subject,
   one is the control that must keep receiving.

## Run

```
./build/tools/scan_server/scan_server --live 8787 "" 300
```

## Procedure

1. **Tab A** (stall subject): open the page — **hard-refresh it** (or use the `?t=` URL
   `watch.sh` prints) so a pre-V5 cached `index.html` cannot serve the old script chain;
   confirm it connects and the HUD counts up. **Record the HUD's `backend` line** (the V5
   visual, first half — it names WebGPU or WebGL2 as live).
2. **Tab B** (control): open `http://127.0.0.1:8788/?backend=webgl2` (hard-refresh if it was
   visited before) — confirm it renders, and **record its `backend` line** (the V5 visual,
   second half — the forced WebGL2 path).

2b. **The rejection path** (#53's third item): read tab A's `backend` line. If it already
   reads `webgl2` with **no** query parameter, that is the fallback (the browser gave no
   usable WebGPU) — record it as such. Otherwise disable WebGPU in the browser
   (`chrome://flags` → WebGPU → Disabled, relaunch), reload tab A's default URL, and confirm
   the page still renders on `webgl2` — a rejecting `requestAdapter`/`requestDevice` must
   reach the WebGL2 path. If instead the page shows "Neither WebGPU nor WebGL2 is
   available", that is a **FAIL** of the rejection path: record it, and continue with the
   stall test. Record the `backend` line after this step.
   **Then restore the room before the stall.** A relaunch drops **both** tabs, and a
   page with no renderer never connects — so: if the flag was toggled, restore it
   (`chrome://flags` → WebGPU → Default, relaunch), then reopen **both** tabs (tab B
   with its forced `?backend=webgl2` URL) and confirm both are live before step 3. Left
   disabled, the flag would make later sessions' default tabs read `webgl2` and
   silently degrade the visual.
3. Let both stream a few seconds. Optional sanity: toggle follow once in tab A.
4. **Stall tab A** with the same busy loop the rerun used — 3 s blocking the main JS thread
   (the rerun's route: the browser tooling's `javascript_tool`, or the tab's own console):

   ```js
   const end = performance.now() + 3000; while (performance.now() < end) {}
   ```

5. **Observe, over the next ~30 s:**
   - **Tab A** may show its socket drop (`disconnected`, possibly `Invalid frame header` —
     the aborted mid-frame send). A dropped/disconnected tab A is **acceptable**; that is
     the new intended behaviour.
   - **Tab B must keep receiving**: its HUD keeps counting, no freeze — record that it did.
   - **The server must survive**: the tick lines keep printing past the stall with no gap;
     the log contains **no** `Broken pipe` / `signal 13`; the process is still alive (`ps`)
     when checked. The tick lines' `clients=` may drop (2 → 1) when the server prunes the stalled tab —
     that is the intended drop; record it (the old run held `clients=2` right up to the crash).
   - If tab A reconnects (reload, or its own re-connect), it receives frames again — record
     what it took.
6. End the session and **tear down normally**: the pause → mesh → stop → destroy sequence,
   all three teardown lines present, `[launcher] Worker exited normally with code 0`, no
   `child timed out after` / `force-killed` line.

## Verdict

- **PASS** = the server survived the stall (no `Broken pipe`, tick lines continuous), tab B
  never lost its stream, tab A's drop/reconnect behaved as "that client got dropped", clean
  teardown with exit 0 — plus both `backend` lines recorded and the rejection path exercised (#53's visual).
- **FAIL** = the process died, or tab B lost its stream, or a desynced tab A silently kept
  "receiving". Keep the log; do not retry without direction. If it died to a signal, also
  check rule 11 (`~/Library/Logs/DiagnosticReports/`).

## Record (in `../results/`, one file per run, never edited in place)

- `2026-10-18-stall-client-and-v5-visual.md` — the record: the command; the room's HEAD;
  **both tabs' `backend` lines**; the server tick continuity around the stall (quote the
  tick lines either side); the absence of `Broken pipe`; tab B's continuity; tab A's
  disconnect/reconnect behaviour; the teardown lines and the exit code.
- `2026-10-18-stall-client-and-v5-visual.log` — the tool's full stdout/stderr, unedited.
- If anything out-of-band happens, add an operator note alongside (additive, naming the
  record it supplements).

This spec is the acceptance for **#56**; the scale bar stays
[`plan32-scale-rerun.md`](plan32-scale-rerun.md). If this session happens to re-reach ≥1M
points, that re-confirms #32/#43 but is not required here.
