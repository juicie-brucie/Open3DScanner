# 2026-10-18 — scan_server live session, support run for PLAN.md #53 (WebGPU-rejection-path visual check)

**Purpose**: provide a stable live `scan_server` session for a browser-side check
(main session disables WebGPU and confirms fallback to WebGL2). No scanner
movement required; this run is idle/static from the test's point of view.

**Tree**: `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`
**Branch**: `hw/next`
**Pre-run check**: `ps aux | grep -E "scan_server|device_message_listener|orbbec"` — no other SDK-using process running. Confirmed per rule 7.

**Command**:
```
./build/tools/scan_server/scan_server --live 8787 "" 90
```

**Start**: 2026-10-18 19:14:28
**End**: 2026-10-18 19:16:32 (wall clock ~124s: 90s session + pause/stop/destroy teardown)

**Exit code**: `0`

**Verdict**: PASS (as a support run — provided a stable, cleanly-torn-down live session; no scanner motion was required or performed)

## Banner / interposer check

Line 2 of stdout:
```
[vulkan_disable_interpose] loaded
```
Confirmed per rule 6 — this is `scan_server --live`, which sets `DYLD_INSERT_LIBRARIES` internally, so the loaded-banner (not the `[launcher] DYLD_INSERT_LIBRARIES=...` line) is the correct observable here. Present.

## Checks

| Check | Result |
|---|---|
| Interposer loaded (`[vulkan_disable_interpose] loaded`) | [PASS] — line 2 |
| SDK context/device/session create succeeded | [PASS] — `obscan_context_create -> OB_SCAN_SUCCESS (0)`, `obscan_context_create_session -> OB_SCAN_SUCCESS (0)`, `obscan_session_create_scan -> OB_SCAN_SUCCESS (0)` |
| Scan started | [PASS] — `obscan_scan_start -> OB_SCAN_SUCCESS (0)` (line 167) |
| Listener started | [PASS] — `[worker] Streaming live scan frames on ws://0.0.0.0:8787 (open ui/index.html)` (line 169). Only one explicit listener-start line was logged; it names the `ws://` scheme and refers to `ui/index.html`, consistent with the PR #61 change that `scan_server` now serves the `ui/` directory itself on the same port as the WebSocket. I did not see a separately labelled "HTTP listener" line distinct from this one — this is the single observable the server printed for listener startup. Not independently verified that the HTTP GET for `/` actually returned `ui/index.html`'s content (that's the main session's browser-side job, per the task). |
| Frames flowing during session | [PASS] — periodic `[worker] tick N/900: color_cloud num_points=... clients=N paused=0` lines throughout, `clients` count rose from 0 to 4, indicating browser connection(s) arrived and were served live frames |
| Clean teardown (pause → stop → destroy) | [PASS] — `obscan_scan_pause -> OB_SCAN_SUCCESS (0)`, `obscan_scan_stop -> OB_SCAN_SUCCESS (0)`, `obscan_session_destroy: 5 poll(s), 4 refusal(s), 0.5s, final OB_SCAN_SUCCESS`, `obscan_context_destroy: 1 poll(s), 0 refusal(s), 0.3s, final OB_SCAN_SUCCESS` |
| Clean process exit | [PASS] — `[launcher] Worker exited normally with code 0.` |
| No stall / no timeout | [PASS] — run completed in ~124s, well inside the worker's internal 390s (session_seconds+300) ceiling; not retried, no hang observed |
| Crash report | Not applicable — exit was clean (code 0). Checked `~/Library/Logs/DiagnosticReports/` for the run window (19:14–19:17); no `scan_server*.ips` file present, consistent with a clean exit. No crash report filed. |

## Notes / incidental observations (not failures)

- Repeated `[error][...][GVCPClient.cpp:355] sendto failed with error:49` throughout — this is the same benign GVCP broadcast-on-loopback error seen in prior runs (errno 49 = `EADDRNOTAVAIL`, from a secondary network interface), not a session-level error; all `obscan_*` API calls returned `OB_SCAN_SUCCESS (0)`.
- Near teardown, two lines from the interposer itself:
  ```
  [vulkan_disable_interpose] intercepted vkGetInstanceProcAddr lookup of a create function
  [vulkan_disable_interpose] blocked vkCreateInstance
  vkCreateInstance failed -3
  ```
  This is the interposer doing its job (blocking a late/stray Vulkan instance creation during shutdown) and did not prevent a clean exit — `obscan_context_destroy` and the worker's normal exit (code 0) both completed after it.
- `clients=0` at tick 0, rising to `clients=3` by tick 50 and `clients=4` by tick 200 — at least one browser connected and stayed connected; this is consistent with (but does not itself confirm) the main session's browser-side WebGPU-rejection check being performed during the window. That check's result (WebGL2 fallback, no error) is the main session's own observation and is not reported here.

## Main-session addendum: the WebGPU-rejection path, confirmed

**Default-path connection** (`http://127.0.0.1:8787/?t=...`, no query, no patch):
`backend: webgpu` — real WebGPU, used live, consistent with prior runs.

**Rejection-path exercise**: patching `navigator.gpu.requestAdapter` after the fact (via
`javascript_tool` right after `navigate`) did not win the race against the page's own
module-loading sequence — the real page had already initialized WebGPU before the patch
landed, so `backend` stayed `webgpu`. To actually exercise the rejection branch, I served
a byte-identical copy of `ui/` (same `app.js`, `frame_protocol.js`, `render/*.js`) from a
separate static server (port 8791, removed after this check), with one addition: an
inline `<script>` at the top of `<body>`, before any of the real loader scripts, that
monkey-patches `navigator.gpu.requestAdapter` to `Promise.reject(new Error("forced
rejection test"))` if `navigator.gpu` exists. This is a test harness, not a change to
`ui/index.html` itself. Pointed at `?port=8787` to connect to this run's real WebSocket.

Result: `backend: webgl2` — connected, `scan: scanning (live)`, render steady at 60fps,
points climbing normally. Console log confirms the exact code path:
```
[warn] WebGPU init failed; falling back to WebGL2: Error: forced rejection test
    at GPU.value (...)
    at createWebGPURenderer (.../render/webgpu_renderer.js:11:41)
    at initRenderer (.../app.js:27:33)
```
`createWebGPURenderer` called `requestAdapter()`, which rejected; `initRenderer`'s
`catch` (app.js line 27, per PR #137's fallback-hole fix) caught it and fell through to
`createWebGL2Renderer`, which rendered correctly. This is precisely the "rejecting
`requestAdapter`/`requestDevice` falls through" path #53's landing note claimed was
fixed — now hardware-confirmed, not just statically verified.

This connection is almost certainly the extra `clients` this record's tick log shows
(3-4, vs. just the one default-path tab) — consistent, not independently provable from
the server side alone.

**#53 is now fully closed**: all three paths (default WebGPU, forced `?backend=webgl2`,
and the rejection path) are hardware-confirmed across this run and the earlier stall-test
run.

## Open questions

- None from this side. The browser-side verification of WebGPU→WebGL2 fallback at `http://127.0.0.1:8787/` is the main session's own observation, not reproduced here.

## Files

- Raw log: `tests/results/2026-10-18-scan-server-v5-rejection-path-support.log`
