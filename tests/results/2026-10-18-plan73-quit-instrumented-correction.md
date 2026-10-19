# Correction — `2026-10-18-plan73-quit-instrumented.md`

**Dated**: 2026-10-18 · **Author**: dispatcher, from the verifier's finding on PR #235 ·
**Corrects**: the record named above (never edited in place).

**1. The stdout log's endpoint — and a line that matters.** The record says the captured stdout
ends at `obscan_session_destroy -> OB_SCAN_SUCCESS (0)` (line 3046) with "nothing after". The file
is **3284 lines / 173,931 bytes**; after 3046 it carries the vendor's own teardown — `rpc client
destroyed` ×2, then **`[10/18 21:48:35.295286][info][16524576][Context.cpp:89] Context destroyed`**,
then the RPC/GVCP tail and the Vulkan/MoltenVK extension listings to EOF. The true statement is
narrower: **no further `[worker]`-prefixed line** appears after 3046.

That omitted line is diagnostically load-bearing: the wrapper's `obscan_context_destroy` call never
returned (correct as recorded), but the vendor's destruction **did reach its own "Context destroyed"
222 ms into the call** (call start `515.073068`). The indefinite spin is therefore in what *follows*
the vendor's destroy — and the file's tail is the same "ends mid-MoltenVK" signature the earlier
rerun recorded, which this run reproduces. Bug A / #89's recon seed is corrected accordingly: what
distinguishes the hang may be the **post-destroy MoltenVK/Vulkan teardown**, not the destroy call.

**2. The app-side gap is 14.287 s, not 8.3 s.** The record's "8.3 seconds later" has no anchor:
`NSTerminateLater` is `508.725702`, `windowWillClose -> terminate:` is `523.012362` → **14.287 s**.
(Nearest alternatives — ~8.51 s from `obscan_scan_stop`'s call/return, ~8.40 s from the vendor's
`Stop streams done!`, ~7.94 s from `session_destroy`'s completion — are none of them the pair the
record means.) The corrected figure is used in row 73 and the PR body; the room's log entry keeps its
as-it-happened wording deliberately.

**Everything else verifies**: the wait path's timestamps, the duplicate `windowWillClose`, Bug A's
reasoning (no completion line, and no `obscan_context_destroy` print at all in the stdout), and the
third-failure-mode framing.
