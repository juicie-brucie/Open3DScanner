- 2026-10-19 — build-3 (build room, `wt-build3`, branch `build/plan129-loader-fix`): **#129 — the
  Windows loader fix is folded into the parent**, applied verbatim from the fork (their `4ed3e40`,
  merge `bfa3df3`): `engine/src/obscan_loader.cpp` now uses
  `LoadLibraryExA(..., LOAD_WITH_ALTERED_SEARCH_PATH)`, blob `bc09230` =
  `bc09230323f6149548033cd4a9a2125a59b7cc27`, byte-for-byte identical to the fork's own file
  (checked against the fork's content, not just the abbreviated sha). The file is parent-synced, so
  without this the next `engine/` sync would reintroduce the old blob; with it the next sync is a
  no-op. **Evidence honestly split:** the macOS build and `ctest` prove only that the `#else`
  (non-Windows) path is unaffected — on macOS the `_WIN32` branch is not compiled, so
  `LoadLibraryExA` is **not** exercised here; the Windows-side evidence is the fork's `win #119` run
  (the SDK loads and its bootstrap exports resolve).
