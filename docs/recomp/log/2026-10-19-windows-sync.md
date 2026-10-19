- 2026-10-19 — the **Windows parent sync landed** (fork PR #17, branch `build/win-parent-sync` @ `cb54971`), consuming
  parent main `7537e44`. Judged by the fork's own fresh-build verification, not by repeating the builder: 44/44 objects;
  `GET /` → 200 `text/html` (3593 B — the parent's `ui/index.html` is 3474 B plus 119 CRLF) where the pre-sync tree closed
  with zero bytes; WebSocket 101; guard harness 62/62; `[perf] window` lines. Independently checked here: the header at
  `7537e44` is blob `9c73210f` / sha256 `1ace2dc8` (r4, no pin cycle), and the branch's tree really does carry
  `hw_guard_test/run.mjs` + `perf_window.h` and omit `raptor_client/` + `macos_app/`. **Divergence for the port record**:
  `scan_server` required Windows adjustments (`_open/_write/_getpid`, `GetSystemTimeAsFileTime`, `GetModuleFileNameA`) —
  behavior-preserving, not a pure checkout. The fork also holds the runbook as `tests/hardware/plan-win-live-rate.md`
  (PR #16). Two items left: PR #17's verdict + merge, and the device attach.
