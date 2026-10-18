- 2026-10-17 — repair: PR #40 (OQ-4 thread accounting) was merged carrying
  verify-1's two MEDIUMs unfixed, and PR #44 had marked **#16** `DONE` with half
  its criteria unmet — the file-scope hoist landed, but the header-rule widening
  never did, and the rule still stops at the destroy boundary. Fixed here:
  `#include <mutex>` and a locked `CbCount()` accessor replace the four unguarded
  `g_cb_threads.size()` reads that raced the locked `push_back`; **#16 reopens to
  `BUILD`** (the widening is a contract edit, recon's); **#17 moves to `HW`** (its
  instrumentation landed, so a run is what settles OQ-4). ARCHITECTURE.md restates
  the landing gate — a merge needs a verifier verdict on the exact head commit, and
  the verifier is a reporter, never an executor — and its Git-discipline item now adds
  that a room is only ever torn down by the user.
