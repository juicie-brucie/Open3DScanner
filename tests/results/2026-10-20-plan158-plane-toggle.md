# PLAN #158 — the runtime plane-removal command — PASS (device-free); the vendor's verdict is still owed

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-build7
- Branch: build/plan158-plane-toggle
- Target: build/tools/scan_server/plane_toggle_test (the new device-free selftest) and build/tools/scan_server/scan_server (the binary the command lives in)
- Preflight/postflight: **not run** — neither target links the vendor SDK; the selftest binds a mock vendor and the synthetic server streams a generated sphere. No device, no interposer, no crash-report sweep applicable.
- Interposer: n/a — no vendor SDK is loaded by either target.
- Run start: 2026-10-20 (see the command timestamps in the raw log)
- Device: not applicable — no device connection was attempted.

## What was built

A WebSocket command that applies or reverts the vendor's plane-removal state
**mid-session**, without restarting the stream:

```
{"cmd":"set_plane_removal","auto":true,"turntable":false,"manual":"0,0,1,-0.33"}
```

Every field is optional and changes that toggle only; `"manual":""` reverts
the manual plane removal; a malformed field rejects the whole command
(nothing changes, the reason is logged). The command is in the existing
command style (`cmd` + flat fields), and it sits beside `start` / `pause` /
`finish` / `set_exposure` / `set_fov` in the handler.

The three toggles, the decision to cut and the manual-plane struct layout are
unchanged — they are the vendor's (`docs/recon/findings/plane-removal.md`).
The startup env surface is unchanged and still applies at start:
`OPENSCANNER_PLANE_REMOVE=1`, `OPENSCANNER_TURNTABLE_PLANE_REMOVE=1`,
`OPENSCANNER_MANUAL_PLANE="a,b,c,d"`. The runtime command is additive.

### The transition log

One line per transition, with the old -> new state and the vendor's verdict,
the tick it happened in and the point count of the last published cloud — so
a run's point-count series can be aligned with the toggle moment:

```
[worker] plane_removal: obscan_scan_plane_remove(true) -> OB_SCAN_SUCCESS (0) accepted [auto 0->1 tick=42 points=1234]
```

The vendor call name and the status text are the same tokens the startup
path logged, so a grep for `obscan_scan_plane_remove` still finds the line.

### Where the call is made

The WS reader thread only records the *desired* state (atomics). The **scan
thread** turns it into the three vendor calls, in the tick, before the
publish getter — the same thread that makes every other vendor call, so a
toggle cannot race them. The tick's hot path is unchanged when nothing is
pending: one atomic load and a compare, no allocation, no blocking beyond
the vendor call itself. A rejected call is not retried every tick; it is
tried again when the next command arrives.

## Commands

```
./build/tools/scan_server/plane_toggle_test
ctest --test-dir build -LE hardware --output-on-failure
./build/tools/scan_server/scan_server --synthetic 18899   # + a WebSocket client sending the command
```

## Result

- Exit code: `0` for all three.
- Verdict: **PASS** (device-free)
- Raw log: `2026-10-20-plan158-plane-toggle.log` (same directory)

### ctest summary

```
 4/12 Test  #4: plane_toggle_test ................   Passed    0.00 sec
100% tests passed out of 12
```

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | The startup env seed still applies the three toggles at start (the behaviour this row adds to) | **[PASS]** the mock vendor saw three calls — `obscan_scan_plane_remove(true)`, `obscan_scan_turntable_plane_remove(true)`, `obscan_scan_config_manaul_remove_plane(a,b,c,d)` with the plane `0,0,1,-0.33` from the env |
| 2 | Each transition is logged with the old -> new state and the vendor's verdict | **[PASS]** `[worker] plane_removal: obscan_scan_plane_remove(true) -> OB_SCAN_SUCCESS (0) accepted [auto 0->1 startup]` (and the turntable and manual lines) |
| 3 | The command applies mid-session, and reverts | **[PASS]** `{"cmd":"set_plane_removal","auto":false,"manual":""}` produced `obscan_scan_plane_remove(false) ... accepted [auto 1->0 tick=42 points=1234]` and `obscan_scan_config_manaul_remove_plane(off) ... accepted [manual 1->0 tick=42 points=1234]` |
| 4 | The transition line carries the tick and the last published point count (the alignment #71 needs) | **[PASS]** `[auto 1->0 tick=42 points=1234]` |
| 5 | A new manual plane is re-asserted with the new coefficients | **[PASS]** `obscan_scan_config_manaul_remove_plane(a,b,c,d) ... accepted [manual 0->1 plane=0,0,1,-0.5 tick=42 points=1234]`, and the mock call carries `plane[3] == -0.5` |
| 6 | A rejected vendor call is logged as rejected and is not hammered | **[PASS]** `... -> OB_SCAN_ERROR_PARAM (-1) rejected [auto 0->1 tick=7 points=99]`; the next apply made **0** calls and logged nothing; a new command retried it once |
| 7 | A malformed command changes nothing | **[PASS]** `{"cmd":"set_plane_removal","manual":"nonsense"}` made 0 vendor calls and was reported |
| 8 | Another command is left alone | **[PASS]** `{"cmd":"set_fov","value":"wide"}` made 0 vendor calls |
| 9 | The command travels over a real WebSocket and comes back in the status message | **[PASS]** the selftest's own client: `handshake: HTTP/1.1 101 Switching Protocols`, the status carries `"planeRemoval":{"auto":true,"turntable":true,"manual":true}` |
| 10 | The shipped binary's command path works (the synthetic server, the real `scan_server` binary) | **[PASS]** `scan_server --synthetic 18899` + a WebSocket client: the status message carries `"planeRemoval":{"auto":true,"turntable":false,"manual":true}` |
| 11 | The whole device-free suite still passes | **[PASS]** `100% tests passed out of 12` (the twelve device-free entries, on the rebased tree) |

## Branch note

The branch was cut before two PRs landed, so the rebase onto `origin/main`
hit two conflict hunks, both in `tools/scan_server/main.cpp` /
`tools/scan_server/CMakeLists.txt`, and both of the same kind: two
independent features touching the same place.

- **#153** (PR #371) invalidates the exposure value after a resume; this row
  re-asserts the plane toggles after a resume — the same `if (!want_paused)`
  block, two independent vendor calls. The resolution keeps both (the
  controller's re-assert *and* `exposure_apply.Invalidate()`).
- **#142** (PR #359) adds `server_now_ms` to the status JSON; this row adds
  `planeRemoval` to the same JSON. The resolution keeps both fields, and the
  CMakeLists keeps both test entries.

The tree above was rebuilt and re-tested on the new `main` (12/12 device-free
entries).

## Crash sweep

Not run — hardware-free; neither target loads the vendor SDK, so nothing here
could produce a device-process crash report.

## What this does NOT prove

- **The vendor's verdict is a mock's verdict.** Every "accepted" above is
  the mock's answer. Whether the SDK accepts these three calls mid-session,
  and what it then does to the published cloud, is row #71's observation and
  it needs the device. The evidence #71 wants is the log line's
  `-> <status> accepted|rejected` on a real run.
- **The A/B itself is not run.** #71's single-session before/after (one
  static scene, the toggles flipped mid-session) is still owed; this row
  only makes it possible.
- The synthetic server has no vendor, so a plane command there records the
  desired state and shows it in the status JSON; it applies nothing.

## Open questions

- Does the SDK's per-frame executor act on a toggle flipped mid-session
  immediately, or only from the next captured frame? The transition line's
  `tick=` and `points=` are the alignment a hardware run needs to answer
  this.
- Does the SDK accept the *disable* direction (`plane_remove(false)`,
  `turntable_plane_remove(false)`, the manual setter with `enabled=false`)?
  The findings describe the enabled byte these calls write, but the disable
  path is not stated — a hardware run that flips a toggle off and watches
  the status is the check.

## Board

- Row move: #158 `BUILD` → `HW` — the command, the transition log and the
  device-free selftest are done (42 checks, 0 failures; the full suite 10/10);
  the *effect* needs the device, because the evidence is the vendor call's
  own verdict on a live scan.
