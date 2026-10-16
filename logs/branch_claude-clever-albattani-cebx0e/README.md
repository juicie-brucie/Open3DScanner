# Hardware test of branch `claude/clever-albattani-cebx0e`

That branch (commit `b30580f`, a separate Claude session, Opus 5.5)
independently reworked the same `obscan_scan_stop`/teardown area this
session had just finished validating, reaching the opposite conclusion
about `obscan_scan_stop`'s real signature — and, per its own commit
message, had only been "verified against `fake_obscan`"; it had never
run against the physical scanner.

## What got checked first (before touching hardware)

Its commit message cites two very specific native-log claims. Both
checked out against logs already in this repo, independent of running
anything on this branch:

- The *restored* 4-arg `obscan_scan_stop(scan, progress_cb, status_cb,
  is_block)` shape produces real `"stop progress 0.01 ... 1.0"` /
  `"change to stop success !"` lines
  (`../native_scan_log/scan_log_20261015-215147.txt`).
- Our (session-local, now superseded) 3-arg shape logs
  `obscan_scan_stop` immediately followed by `obscan_session_destroy`
  with **nothing** in between, then both destroys refused with
  `OB_SCAN_ERROR_MULTI_CALLED` ("must first wait other process
  finshed: destroy session !") — i.e. it returns `OB_SCAN_SUCCESS` but
  does nothing real
  (`../native_scan_log/scan_log_20261016-020420.txt`).

Both of these are genuine, verified findings: the 3-arg `obscan_scan_stop`
merged earlier in this session's main history was a regression, and this
branch's fix for it is correct. (A fix for this, informed by this
branch's analysis, follows in this session's own subsequent commits —
check git log/blame on `obscan_api.h` for the current state rather than
assuming this note is still accurate.)

## Hardware test of this branch's actual code

Built and ran `tools/live_capture_test` from this branch directly
against the physical Raptor Pro (markers in view, same physical setup
as this session's other recent runs). Result: **a reproducible
segfault**, identical on both of two consecutive runs, at the same
point — right after logging `"device messages: connect=1 disconnect=1
other=0; device count=1"` (`main.cpp`, the `Say(...)` call right after
the `WaitFor` device-count poll), before `obscan_context_create_session`
is ever reached. The vendor SDK's own log is clean up to and including
`"Init device return:1."`; nothing from the vendor side explains the
crash, pointing at this branch's own new code (e.g. the
`obscan_device_management_get_device_info` call right after that log
line, or state around it) rather than the vendor SDK.

- `native_scan_log/` — the two identical-outcome runs' vendor-SDK scan
  logs.
- `live_capture_test_runs/` — this branch's own `live_capture_test`
  stdout/stderr for both runs.

So: real, useful ABI analysis (`obscan_scan_stop`, and independently
also finding Markerless fuse mode never produces a point — matching
this session's own conclusion), but the branch's own control-flow
rewrite has not yet run successfully against real hardware.
