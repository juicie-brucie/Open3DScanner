# Row #153 — the exposure/laser config is applied on change only — HW (the live run is owed)

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-build5
- Branch: build/plan153-exposure-on-change
- Target: build/tools/scan_server/scan_server (the live worker; **no device run in this record** — the vendor SDK was never loaded)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (the bundle copy, refreshed on an app build)
- Preflight: n/a — no hardware run in this record; the evidence is hardware-free (the ctest suite) and the log analysis
- Interposer: n/a — no vendor SDK was loaded
- Run start: n/a — hardware-free

## The waste

`obscan_scan_config_combined_exposure_and_laser_current` was called **every tick**
(`tools/scan_server/main.cpp`: `control.auto_exposure = false` and the unconditional call in the
live tick). It is one of the two untimed vendor calls that dominate the live tick's unsegmented
work (#141/#147), and the value rarely changes — so most of that was re-asserting the same
number.

## What changed

| # | Change | Where |
| --- | --- | --- |
| 1 | The config is pushed **only when the value changes**; the first application still happens before the scan starts (so before its first frame) | `tools/scan_server/main.cpp` (the pre-start call and the tick), `tools/scan_server/exposure_apply.h` |
| 2 | The `exposure` segment still measures the call when it happens, and the window counts the pushes (`exposure n=1`) | `tools/scan_server/perf_window.h`, `tools/scan_server/main.cpp` |
| 3 | The apply path is device-free testable: the live path and the test drive the same helper | `tools/scan_server/exposure_apply.h`, `tools/scan_server/exposure_apply_test.cpp` |

The call sites:

- `tools/scan_server/main.cpp` — the **first** application, next to
  `obscan_scan_preview` and before `obscan_scan_start` (the vendor app does the same in that
  order — see the analysis below). It pushes `kDefaultExposure` and the tracker remembers it.
- `tools/scan_server/main.cpp` — the **tick**: the call now happens only when
  `ApplyExposureIfChanged` says so. Each push logs
  `[worker] exposure config pushed: value=<n> -> <status> (<code>)`.
- `tools/scan_server/main.cpp` — the **resume**: a pause→resume re-runs the SDK's scan-mode
  setup, which writes the laser current itself, so the resume invalidates what we pushed and
  the next manual tick pushes it again (the same caution the plane toggles already carry in
  this file).

## The semantics (all three checked)

| Check | How it is met |
| --- | --- |
| The first application still happens **before any frame** | The pre-start call above (before `obscan_scan_start`); the tracker is seeded with it, so the tick does not repeat it |
| A change (a property SET, the slider) still applies on the next tick | The UI sends `{"cmd":"set_exposure","auto":false,"value":n}` (`ui/app.js`); the handler sets `control.exposure_value`, the tick compares it with the pushed value and pushes it — `exposure_apply_test` drives exactly this sequence |
| `auto_exposure = false` is still in force when the value is applied | The push path is entered only in manual mode; in auto mode nothing is pushed and the tracker is told that nothing is held, so returning to manual pushes again |

## Why the per-tick re-assert is not required (the API contract + the logs)

1. **The API contract**: the call is a setter that "pushes to the device"
   (`docs/recon/findings/scan-live-data.md` §18) — the device holds the value.
2. **Our own log**: in the exposure sweep (`logs/exposure_sweep_new_setup_20261016/native_scan_log.txt`,
   11 calls 3 s apart over 28 s) the SDK wrote the laser current **exactly once per call, within
   0 ms of it**, and **never in between** — the device's own AE loop wrote nothing. The value
   maps to the laser current (100 → 0.238 A … 1000 → 1.504 A) and the device keeps it.
3. **The vendor's own client**: in the CrealityScan app's own session
   (`logs/creality_scan_comparison_20261016/native_logs/scan_log_20261016-024738.txt`) the app
   applied the config **112 times in 151 s (0.74/s), 74 distinct values, the longest gap 59 s** —
   it applies it when its own auto-exposure value changes, not once per frame.
4. **The "decay" the old comment described was not ours to hold.** The call also sets the IR
   exposure time to 0 (= auto) — and every IR-exposure write in both logs is `0` (371 of them,
   no other value). The decaying value was the **device's own auto-exposure** ("AE GET EXPOSURE
   TIME" 3000 → 2403 → …), i.e. the AE's domain, which the per-tick re-assert never held: it
   only re-wrote `0` 10×/s.

The analysis and its numbers are in the raw log (`2026-10-20-row-153-exposure-on-change.log`,
section 3 and 4) and are reproducible from the committed logs.

## Evidence (hardware-free)

- `ctest -LE hardware`: **10/10 passed**, including the new `exposure_apply_test`.
- `exposure_apply_test` drives the live path's own helper with a recorder: the first push, the
  skip when nothing changed, a push on a changed value, no push in auto mode, a push again when
  manual mode returns, a push again after a resume invalidates, and a **refused** push retried
  on the next tick.
- `perf_window_test` pins the new counter: the window counts the pushes (`exposure n=`) and the
  segment still measures the call it made (`max`), while the ticks that skipped it measure zero.

## What the live run must show (the row stays `HW` until then)

One live `scan_server --live` run with the scanner attached, before/after:

1. `[perf] window ... | exposure p50=... p95=... max=... ms | exposure n=<count>` — the count
   is **1 per window** (plus one when the value changes), not one per tick.
2. `[perf] summary ... exposure_mean=<x>ms` — the segment's mean per tick drops to roughly
   `1/N` of its old value (the old value was one call per tick).
3. The image is unchanged: the same manual exposure value produces the same picture as before
   (the device holds the value — see the log evidence above).
4. `[worker] exposure config pushed: value=<n> -> OB_SCAN_SUCCESS (0)` appears when the slider
   moves, and **not** on the ticks after it.

## Open questions / risks

- The only part that needs the device is the *effective* exposure (point 3 above): the logs say
  the device holds the value, but only a live run shows the picture.
- `tools/live_capture_test` re-asserts the same config per poll — same waste, **outside this
  row's wall** (`tools/scan_server/**`), so it is untouched here.
- The push is now logged per application (it was silent in the tick). That is one log line per
  change, not per tick.

## Board

- Row #153 → `HW`: the code and the reasoning are here; the device-side saving (the `exposure`
  segment before/after) is the hardware room's to measure.
