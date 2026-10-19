# Correction — `2026-10-18-plan65-app-launch-rerun.md`

**Dated**: 2026-10-18 · **Author**: dispatcher, from the verifier's finding on PR #220 ·
**Corrects**: the record named above (never edited in place).

**What the companion shows, contrary to the record's central claim.** The record says no teardown
ever appears and that the worker ran orphaned 7+ minutes at 270%+ CPU. Its own companion
(`2026-10-18-plan65-app-launch-rerun.log`, 42 KB) contains a **complete, successful teardown**
ending at `21:01:16.826268` — ~8 s after the `21:01:09` quit request:

- `:635` `[worker] Stop requested (Ctrl+C or UI "finish") — ending the live loop and tearing down cleanly.`
- `:636` `obscan_scan_pause -> OB_SCAN_SUCCESS (0)`
- `:643` `obscan_scan_stop -> OB_SCAN_SUCCESS (0)`
- `:644–666` `obscan_session_destroy` — "4 poll(s), 3 refusal(s), 0.4 s, final OB_SCAN_SUCCESS"
- `:684` `Context destroyed` @ `21:01:16.826268`, then the MoltenVK tail

So the SIGTERM/teardown machinery #73 is about **worked** in this session. The orphan guard's own
print (`[worker] parent exited — …`) appears **zero** times: the stop arrived through the signal/UI
path, so the guard was neither exercised nor needed here.

**What stands, and what does not.**
- "No teardown ever appears" is **withdrawn**; so is "orphaned 7+ minutes" as stated. The 7-minute
  `ps` watch observed *a* process at 270%+ CPU; **which** process is now the open question — two
  candidates: a stale worker from an earlier launch, or **this** worker hanging *after* its clean
  teardown (the log ends abruptly inside the MoltenVK/Vulkan tail, consistent with a post-teardown
  hang).
- The **app-side anomaly stands** (the user's direct observation): the app's own process vanished
  from `ps` within ~1 s of the quit request, no crash report was generated, and the window stayed
  visually present and unresponsive to a follow-up ⌘Q — only its own close button dismissed it.
  That is not what `NSTerminateLater` waiting for a worker's real exit should look like, so the
  app's exit path remains the thing to instrument.
- Observations 1 and 2 are unaffected — their evidence is the user's screenshots, as the record
  says; the companion carries the bring-up/streaming portion (`clients=7`, ticks climbing),
  consistent with a live page. The log's abrupt mid-print ending is real, as the record says.

**Net read**: not "the quit never reaches the worker" — the worker's teardown ran clean. The two
live questions are (a) the app's early/odd exit with the window lingering, and (b) the identity and
lifecycle of the process the 7-minute watch tracked.
