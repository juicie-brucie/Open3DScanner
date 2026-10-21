# Three newly-found ungated hardware-free tests — first runs under their own name — PASS

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: `71aadd5` (Merge pull request #374 from juicie-brucie/build/plan152-perf-contract)
- Preflight/postflight: not run — all three explicitly hardware-free by
  their own `CMakeLists.txt` comments (no vendor SDK, no device, mock
  vendor/loopback peers only). Standing verify-don't-assume policy
  applied: each classification was re-checked against current source
  before running, not assumed from the earlier enumeration pass.

## 1. ui_frame_age_test (row #142 — the HUD's frame-age arithmetic)

- Location: `ui/frame_age_test.js`, run via Node (`find_program(NODE_EXECUTABLE node)`, conditional in CMake — confirmed `node` present: `/opt/homebrew/bin/node`)
- Command: `node ui/frame_age_test.js build/tools/scan_server/scan_server` (and via `ctest -R ui_frame_age_test`)
- Exit code: `0`
- Output: 9 checks + a synthetic HUD line, all `PASS`: `[frame-age] ok (0 failure(s))`, `[frame-age] synthetic HUD line: p50 0.7 / p95 0.7 ms`
- Verdict: **PASS**

## 2. plane_toggle_test (row #158 — the runtime plane-removal command) — closes the mid-session-toggle gap found before #71 attempt 5

- Location: `tools/scan_server/plane_toggle_test.cpp` (new file)
- Command: `build/tools/scan_server/plane_toggle_test` (and via `ctest -R plane_toggle_test`)
- Exit code: `0`
- Output: 42 checks, all `ok`, across startup-env-seed, runtime apply/revert, a new manual plane, a rejected-then-retried vendor call, malformed/unrelated commands, and a real WebSocket round trip.
- Verdict: **PASS**

**Investigation (per the coordinator's request): does this close the
mid-session-toggle gap reported before #71 attempt 5?** Before #71
attempt 5, this agent found that `apply_plane_config()` only read fixed
process-environment variables and that `HandleControlMessage` had no
runtime command to change plane-removal state — meaning no single
`scan_server --live` session could toggle plane-removal on/off partway
through.

**That gap is now closed, confirmed in source (not just this selftest's
output):**
- `tools/scan_server/main.cpp` now includes `control_state.h` and
  `plane_toggle.h` ("the runtime plane-removal command (row #158)") and
  uses `openscanner::control::HandleControlMessage` from there — the
  **same function** `plane_toggle_test.cpp` exercises — at **both**
  WS-message call sites (`main.cpp:489` synthetic mode, `main.cpp:1021`
  live mode). This is not a mock-only addition; it's wired into the real
  `--live` path.
- The new command is `{"cmd":"set_plane_removal", "auto":bool,
  "turntable":bool, "manual":"a,b,c,d"|""}` (per the comment at
  `main.cpp:20` and the test's own calls, e.g.
  `HandleControlMessage(R"({"cmd":"set_plane_removal","auto":true})", state)`).
- The selftest's "runtime command: apply and revert" section demonstrates
  exactly the missing capability: a toggle applied, then reverted, **within
  one session**, with each transition logged carrying `tick=` and
  `points=` ("so a point-count series can be aligned") — i.e. the
  mechanism needed for a genuine before/after same-scan comparison, which
  attempt 5's env-var-only approach could never do.

**This means a future #71 attempt could use a real mid-session toggle via
this WS command**, rather than the two-separate-process-launches approach
every prior attempt (1–5) used — worth noting for whoever picks up #71
next, though this agent did not attempt that rerun here (out of scope for
this dispatch).

## 3. raptor_client_live_suffix (row #157 — the live path's interface suffix)

- Location: `tools/raptor_client/main.cpp` (`--selftest-live`)
- Command: `build/tools/raptor_client/raptor_client --selftest-live` (and via `ctest -R raptor_client_live_suffix`)
- Exit code: `0`
- Output: 9 route/suffix-resolution checks + a full IR run (3/3 frames pulled, correct call sequence: suffixed `start_stream` → suffixed `get_frame`/`send_data` → suffixed `stop_stream`, no bare method name) + the same for depth + a check that a bare method name is refused the way the daemon does. `[live-selftest] ok (0 failure(s))`.
- Verdict: **PASS**

**Investigation (per the coordinator's request): has #157's fix landed in
raptor_client's actual live path, or only in this selftest?** Confirmed
in source — **the fix is in the real path, not a selftest-only
reimplementation**:
- The real `Live()` function (`main.cpp:1110`, the same function the
  `--live` CLI entrypoint calls) takes an `InterfaceRoute route`
  parameter and builds method names via `MethodFor(route,
  "ob_rpc_get_frame")` — i.e. the interface suffix is baked into the
  production call path, not bolted onto a separate test harness.
- `LiveSuffixSelftest()` (what `--selftest-live` runs) **calls that same
  `Live()` function directly** (`main.cpp` inside `LiveSuffixSelftest`:
  `int rc = Live("127.0.0.1", daemon.port(), *live_route, 5, path, 0, 3,
  false, nullptr, &stats);`) against a loopback mock daemon — it is not a
  parallel/duplicate implementation.
- The real `--live` CLI entrypoint (`main.cpp:2931`) resolves the given
  port to a route via `RouteForPort(routes, port)` and passes that route
  into the same `Live()` call.

**This means #127 and #132's earlier blocker (`ob_rpc_start_stream`
rejected with an argument-count mismatch) is plausibly resolved now** —
the suffix fix that #157 names is confirmed present in the exact code
path those two runs exercised. **Not retried in this run, per
instruction** — flagging this finding for a check-in before any retry.

## Crash sweep

Not run — all three are hardware-free, no vendor SDK, no device I/O.

## Open questions

- Whether #127/#132 would actually succeed against the real daemon now
  that #157's fix is confirmed in the live path — not tested here, held
  per instruction pending a check-in.
- Whether a #71 rerun using the new `set_plane_removal` runtime command
  (true single-session before/after) would produce a clearer result than
  attempt 5's env-var-only approach — not attempted here, noted for
  whoever picks up #71 next.

## Board

- Row move: none proposed directly from this batch. This supplies
  first-time coverage for three selftests (#142, #158, #157) and two
  substantive findings (the #158 mid-session-toggle capability now exists
  in the real live path; the #157 suffix fix is confirmed in the real
  live path, not just its selftest) that bear on #71 and #127/#132
  respectively, for the dispatcher to act on.
