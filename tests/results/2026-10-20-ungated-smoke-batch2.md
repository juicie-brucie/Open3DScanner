# Five newly-found ungated hardware-free tests — first runs under their own name — PASS

Found during a dry-run enumeration pass (worktree fast-forwarded to
`origin/main` `2e94bba` first, per the task's request to check against
current HEAD). All five are new since the last enumeration pass and are
explicitly marked hardware-free by their own CMake comments; run per the
coordinator's "run it and record as usual" instruction for anything new
and clearly hardware-free.

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next, fast-forwarded to `2e94bba` immediately before this pass
- Preflight/postflight: not run — none of these five link a vendor SDK or
  touch a device; all five are explicitly commented hardware-free in their
  respective `CMakeLists.txt`.
- `exposure_apply_test` required a CMake reconfigure (`cmake -S . -B build`)
  and a fresh build (`cmake --build build --target exposure_apply_test`)
  before it could run — it's a brand-new target landed in the just-merged
  `tools/scan_server/CMakeLists.txt`/`exposure_apply.h`/`exposure_apply_test.cpp`,
  and the existing `build/` tree hadn't been reconfigured for it yet. The
  other four built/ran against the already-current build tree.

## 1. perf_window_test (rows #111/#139 — the per-tick timing fields)

- Location: `tools/scan_server/perf_window_test.cpp`
- Command: `build/tools/scan_server/perf_window_test` (and via `ctest -R perf_window_test`)
- Exit code: `0`
- Output: `[perf_window_test] PASS: 4 work segments unchanged; wait/exposure/release separate; overshoot = wait - nominal; residual = period - sum`
- Verdict: **PASS**

## 2. exposure_apply_test (row #153 — the exposure/laser apply decision)

- Location: `tools/scan_server/exposure_apply_test.cpp` (new file, new target)
- Command: `build/tools/scan_server/exposure_apply_test` (and via `ctest -R exposure_apply_test`, after reconfigure/build)
- Exit code: `0`
- Output: `[exposure_apply_test] PASS: the config is pushed on change only; auto mode never pushes; a resume invalidates what was pushed; a refused push is retried`
- Verdict: **PASS**

## 3. raptor_client_reply_dump (row #127 — the reply-structure dump, loopback mock)

- Location: `tools/raptor_client/main.cpp` (`--selftest-dump`)
- Command: `build/tools/raptor_client/raptor_client --selftest-dump` (and via `ctest -R raptor_client_reply_dump`)
- Exit code: `0`
- Output: 8 checks, all `PASS`, plus the mock run's byte-exact dump output
  (an 11-element array reply, a band(3) reply, and a bare `Bin(3)` reply —
  all decoded and printed correctly against a loopback mock of the
  daemon's reply side). `[dump-selftest] ok (0 failure(s))`.
- Verdict: **PASS**
- Note: this is the same `--dump-replies` machinery that failed against
  the real daemon today in `2026-10-20-plan127-live-reply-dump.md`
  (blocked on the `ob_rpc_start_stream` argument-count mismatch before
  any real dump could happen). This selftest only exercises the
  dump-formatting logic against synthetic/mock replies, so it passing
  doesn't speak to that live blocker — consistent, not contradictory.

## 4. raptor_client_capture (row #140 — the capture recorder)

- Location: `tools/raptor_client/capture.cpp` (`--selftest-capture`)
- Command: `build/tools/raptor_client/raptor_client --selftest-capture` (and via `ctest -R raptor_client_capture`)
- Exit code: `0`
- Output: 27 checks, all `PASS` — sha256 implementation against standard
  test vectors, manifest contents (branch/commit/binary hash/record
  counts/per-port file hashes), round-trip read-back byte-for-byte for
  three reply shapes, refusal to write a bundle inside a repository (incl.
  through a symlink), and the byte-bound stopping the recording.
  `[capture-selftest] ok (0 failure(s))`.
- Verdict: **PASS**
- Note: same relationship to today's live `raptor_client_capture` run as
  above — `2026-10-20-plan132-gap-free-capture.md` showed the capture
  *tooling* itself (manifest, hash chain, bounds) worked correctly even
  though streaming never started; this selftest's own sha256/manifest/
  round-trip/refusal checks corroborate that the tooling is sound in
  isolation, independent of the live blocker.

## 5. frame_decode_test (row #125 — the band renderer's offline regression)

- Location: `tools/frame_decode/test_frame_decode.py` (new directory,
  `tools/frame_decode/` — `frame_decode.py`, fixtures, README)
- Command: `python3 -I tools/frame_decode/test_frame_decode.py` (and via `ctest -R frame_decode_test`)
- Exit code: `0`
- Output: `[frame_decode_test] PASS: depth+ir bands render to the recorded PGMs, dims/byte accounting/stats exact, band-offset hook preserves the band, output deterministic`
- Verdict: **PASS**

## Crash sweep

Not run — all five are hardware-free, no vendor SDK linked, no device I/O;
nothing could produce a device-process crash report from any of them.

## Open questions

- None raised by these five runs — all clean, no failures.

## Board

- Row move: none proposed beyond noting these are now confirmed running
  cleanly under their own name. No PLAN.md row specifically asks for a
  standalone run of these five selftests separate from the feature rows
  they already serve (#111/#139, #153, #127, #140, #125) — this is a
  coverage/housekeeping record, not new evidence for any of those rows'
  verdicts.
