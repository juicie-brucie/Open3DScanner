- 2026-10-18 — hardware + browser (main session, `wt-hw`, branch `hw/next`):
  #53's last open item — the WebGPU-rejection path. Forced
  `navigator.gpu.requestAdapter` to reject via a test harness (a
  byte-identical copy of `ui/` with one added inline patch script, served
  separately, pointed at the real running session's WebSocket). Console
  log confirms `createWebGPURenderer`'s `requestAdapter()` rejected,
  `initRenderer`'s catch (app.js:27) caught it, and the page fell through
  to `createWebGL2Renderer`, which rendered correctly — the exact path
  PR #137 claimed to fix. All three paths (default WebGPU, forced
  WebGL2, rejection) now hardware-confirmed; #53 → `DONE`.
