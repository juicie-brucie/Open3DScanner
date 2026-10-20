# frame_bins_test (row #123 — the frame-bin extractor's CI test) — first run under its own name — PASS

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: `2e94bba` (Merge pull request #379 from juicie-brucie/docs/row-148-scale-architecture)
- Target: `tools/frame_bins/test_frame_bins.py` (Python, no build artifact — run via ctest/`python3 -I`)
- Other copy: n/a
- Preflight/postflight: not run — hardware-free by design (re-verified fresh
  this run, not assumed from the earlier enumeration pass): `CMakeLists.txt`
  states "Row #123: the frame-bin extractor's CI test (hardware-free,
  Python)"; the source imports only `hashlib`, `os`, `subprocess`, `sys`,
  `tempfile` (test file) and `argparse`, `collections`, `os`, `struct`,
  `sys` (`frame_bins.py` itself) — no vendor SDK, no socket/network calls;
  `subprocess` is only used to invoke `frame_bins.py` itself as a local
  CLI tool. No daemon, no device, no interposer, nothing in this run
  touches hardware.
- Interposer: n/a
- Device: n/a — no device used or required
- One-SDK-user check: n/a for this test (no daemon/device dependency), but
  checked anyway per the standing verify-don't-assume policy —
  `pgrep -x scan_server` found a process (PID 6667), identified via
  `ps -p 6667 -o pid,comm,args` as running from a **separate build tree**
  (`/private/var/folders/.../opencode/pr374/src/build/tools/scan_server/scan_server`),
  not this worktree — unrelated to this run and not a conflict for a
  hardware-free test either way.

## Command

```
python3 -I tools/frame_bins/test_frame_bins.py
```
(also confirmed via `ctest --test-dir build -R frame_bins_test`)

- Raw log: 2026-10-20-frame-bins-test.raw.log (same directory)

## Result

- Exit code: `0` (both the direct run and via ctest)
- ctest: `Test #5: frame_bins_test ... Passed 0.22 sec`
- Output: `[frame_bins_test] PASS: tsv top-level + array-shaped (slots 4/6) bins, order/bytes/index/determinism; pcap 3-segment nested reassembly + gapped message dropped`
- Verdict: **PASS**

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | TSV-format bin extraction: top-level bins | **[PASS]** (per summary line) |
| 2 | TSV-format bin extraction: array-shaped bins (slots 4/6) | **[PASS]** |
| 3 | Order/byte-count/index correctness | **[PASS]** |
| 4 | Deterministic output | **[PASS]** |
| 5 | Pcap-format 3-segment nested reassembly | **[PASS]** |
| 6 | Gapped/incomplete message correctly dropped | **[PASS]** |

(The test prints a single summary line rather than per-check lines; the
six items above are the distinct behaviors the summary line itself names.)

## Crash sweep

Not run — hardware-free, no vendor SDK, no device I/O; nothing could
produce a device-process crash report from this command.

## Open questions

- None raised by this run — clean pass, no failures, no ambiguity in the
  output.

## Board

- Row move: none proposed — no PLAN.md row was found (during the earlier
  enumeration pass) asking specifically for a standalone run of this
  selftest separate from whatever row #123 itself tracks. This is a
  coverage record: `frame_bins_test` had never been run under its own
  name before this; it now has been, and passes.
