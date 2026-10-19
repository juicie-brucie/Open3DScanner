- 2026-10-18 — hardware + GUI (main session + the user watching the window,
  `wt-hw`, branch `hw/next`): #65's rerun, now with #72/#73/#74 landed.
  Cold start and crash-surfaced both PASS cleanly (#72 confirmed: real page
  loads, full HUD, `backend: webgpu`; crash alert exact). **Quit does not**:
  requesting quit (via `osascript` and separately via a real ⌘Q) left the
  worker running orphaned at 270%+ CPU for 7+ minutes, no teardown ever
  logged, until manually `kill -9`'d. The app's own process vanished from
  `ps` within ~1s (no crash report), yet the window stayed visually present
  and unresponsive to a follow-up ⌘Q — only clicking its own close button
  dismissed it. This contradicts #73's "not reproduced outside the room"
  diagnosis (which blamed a window-server-less launch); this rerun used a
  normal, window-server-connected launch the user watched directly. #73
  reopened: `DONE` → `HW`. Needs the fix author present with a debugger on
  the quit path — black-box `ps` observation can't show why SIGTERM either
  isn't sent or isn't landing on the worker.
