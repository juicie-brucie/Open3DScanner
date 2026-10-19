- 2026-10-19 — **Windows lane round 1**: twelve merges, board `win #24` @ `a37205b`, none merged around a `NEEDS CHANGE`.
  The parent sync is **landed** (fork PR #17 @ `f2e9003`), contract pin `1ace2dc8` on both sides, `--selftest` 52 cases.
  `win #119` is the first real exercise of the `_WIN32` loader split: `LoadLibraryA(full path)` does not search the DLL's
  own directory; `LoadLibraryExA(..., LOAD_WITH_ALTERED_SEARCH_PATH)` fixes it. The fork's gate note is adopted here: a
  leading UTF-8 BOM defeats the anchored-line verdict parser, and `verdict-gate.yml` now strips one. The scanner is still
  not attached on Windows; the handover waits on the #105/#127 live diagnostics and stays this side's call.
