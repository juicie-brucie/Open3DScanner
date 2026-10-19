- 2026-10-18 — board: **correction** to the `row-73-reopened` entry above. The rerun's companion log
  (read at PR #220's verdict) shows a **complete, successful teardown** ~8 s after the quit, so "no
  teardown ever appears" and "orphaned 7+ minutes" do not hold as written. What stands: the app-side
  anomaly (process gone ~1 s, window lingering until its close button) and the identity of the
  270%-CPU process. #73 stays `HW`; #79's target is corrected to match. See the dated addendum.
