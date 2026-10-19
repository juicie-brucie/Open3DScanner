- 2026-10-19 — the Windows dispatcher's readiness report: `scan_server` builds (MSVC 19.44/Ninja; one `M_PI` fix), no
  `raptor_client` (POSIX sockets — parent now owns portability as **#116**), the scanner **not attached** since 17 Oct,
  and the fork predates #111. Two corrections recorded on #115: synthetic is the default mode (`scan_server <port>`, no
  `--synthetic` flag), and `scan_server` **does** serve the UI over HTTP (verified on a fresh parent build: `GET /` → 200,
  `text/html`, 3474 B) — the observed zero-byte close is the `static_root.empty()` path, not a missing feature.
