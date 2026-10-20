# Row #130 — the hardware tests tell a missing precondition from a real failure — DONE

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-build5
- Branch: build/plan130-preconditions
- Target: build/tests/hardware/device_message_listener_test/device_message_listener_test and build/tests/hardware/data_process_message_listener_test/data_process_message_listener_test (explicit argument — the preflight's fixed-order search only looks under build/tools/<target>/<target>; the hardware tests live under build/tests/hardware/<target>/<target>)
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: RPCServer(925,) running (process proxy: a vendor RPC host process exists); freshness: binary 22:40:40 >= newest source main.cpp 22:40:12
- Interposer: n/a for the runs below — no vendor SDK was loaded. A hardware run expects first line: `[launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib`
- Run start: 2026-10-19 22:41:58

## The defect

`SKIP_RETURN_CODE 2` was returned **only** when the library path was empty; a failed
`obscan_context_create()` returned **1**. A host with the SDK installed but the vendor's
RPC daemon down — or with no device enumerated — therefore recorded a **FAIL** where the
truth is "this run could not happen". `scripts/hw-preflight.sh` checked pwd, conflicting
processes, freshness and the interposer, but never the daemon, so the doomed run was
started in the first place.

## What changed

| # | Change | Where |
| --- | --- | --- |
| 1 | Both listener tests probe the daemon and the device **before** `obscan_context_create()`; a missing precondition exits **2** with a reason line, a genuine init failure stays **1** | `tests/hardware/common/hw_precondition.{h,cpp}`, both `main.cpp` |
| 2 | The preflight asserts the daemon for the targets that create an OBScan context and names the fix | `scripts/hw-preflight.sh` (new step 5) |
| 3 | A hardware-free evidence script + an SDK test double, so both paths can be shown without a device and without the vendor library | `tests/hardware/precondition_evidence.sh`, `tests/hardware/stub_sdk/` |

The probe runs **before the SDK is loaded**, so a skip never starts the vendor library.

## The reason lines (the contract)

```
[worker] SKIP: precondition not met: CrealityScan (RPC daemon host) is not running; listener checks NOT exercised.
[worker] SKIP: precondition not met: no VID_2BC5 device enumerated; listener checks NOT exercised.
[worker] SKIP: precondition probe failed (CrealityScan: <state>; VID_2BC5: <state>); listener checks NOT exercised.
```

`precondition not met:` is the stable anchor; the cross-machine grep token
`(RPC daemon host) is not running` and the literal device wording
`no VID_2BC5 device enumerated` are unchanged from the Windows lane's contract
(`docs/recomp/windows-handover-order.md`, "SKIP wording when a precondition is not met").
The line goes to the test's captured **stdout**, as there.

## Evidence (hardware-free; the vendor SDK was never loaded)

Raw log: `2026-10-19-plan130-precondition-skip.log` (same directory).
The tests were pointed at the SDK **test double** (`tests/hardware/stub_sdk` — a library
that exports the loader's entry points and whose `obscan_context_create()` fails), and the
two precondition verdicts were forced through the probe's test-only overrides
(`OPENSCANNER_HW_DAEMON_STATE`, `OPENSCANNER_HW_DEVICE_STATE`).

| Path | Command | Exit | Line |
| --- | --- | --- | --- |
| daemon missing | `OPENSCANNER_HW_DAEMON_STATE=absent <test> <stub>` | **2** | `precondition not met: CrealityScan (RPC daemon host) is not running; listener checks NOT exercised.` |
| device missing | `OPENSCANNER_HW_DEVICE_STATE=absent <test> <stub>` | **2** | `precondition not met: no VID_2BC5 device enumerated; listener checks NOT exercised.` |
| probe indeterminate | `OPENSCANNER_HW_DAEMON_STATE=unknown <test> <stub>` | **2** | `precondition probe failed (CrealityScan: probe indeterminate (forced); VID_2BC5: VID 0x2BC5 enumerated); listener checks NOT exercised.` |
| genuine init failure | `OPENSCANNER_HW_DAEMON_STATE=present OPENSCANNER_HW_DEVICE_STATE=present <test> <stub>` | **1** | `[worker] obscan_context_create() -> OB_SCAN_ERROR_FATAL (845283585)` — the run failed, it is not a skip |

Both listener tests behave identically (`tests/hardware/precondition_evidence.sh` checks
all four paths for each and exits non-zero if any of them drifts).

Through CTest the same run is recorded as a **skip** — the defect's own claim:

```
$ OPENSCANNER_HW_DAEMON_STATE=absent ctest --test-dir build -L hardware
1/2 Test #8: device_message_listener_test .........***Skipped   0.28 sec
2/2 Test #9: data_process_message_listener_test ...***Skipped   0.16 sec
```

On the old code the same situation reached `if (status != OB_SCAN_SUCCESS) return 1;` (the
failed `obscan_context_create()`), which CTest records as **Failed**.

The probe's own verdict on this host, with nothing loaded
(`<test> --precondition-probe`, exit 0):

```
[probe] daemon: satisfied (RPCServer running)
[probe] device: satisfied (VID 0x2BC5 enumerated)
[probe] preconditions satisfied
```

## The preflight's new check

`scripts/hw-preflight.sh <target>` now asserts the daemon for the targets that create an
OBScan context (`device_message_listener_test`, `data_process_message_listener_test`,
`smoke_test`, `live_capture_test`, `scan_server`). It is a **process proxy**, not a service
query, and it says so in the record header it prints:

```
- Preflight: pwd OK; no conflicting process; daemon: RPCServer(925,) running (process proxy: a vendor RPC host process exists); freshness: ...
```

When the daemon is not there (shown with a `pgrep` that reports "not found"), the preflight
stops and names the fix:

```
hw-preflight: FAIL — the CrealityScan RPC daemon host is not running (no 'CrealityScan' or 'RPCServer' process) — fix: start CrealityScan (macOS: the 'com.creality.RPCServer' LaunchAgent; the CrealityScan app counts as the RPC host too).
```

The existing checks (pwd/worktree, conflicting processes, binary selection, freshness,
interposer expectation) are unchanged, and the record header keeps its eight fields.

## Deviations and open questions

- **The macOS daemon proxy is the app *or* the root RPC daemon.** The reason line keeps the
  contracted wording (`CrealityScan (RPC daemon host) is not running`), but the process the
  proxy looks for on macOS is `CrealityScan` **or** `RPCServer`. On macOS the RPC server is
  Creality's root daemon (`/Library/Creation/RPCServer`, the `com.creality.RPCServer`
  LaunchAgent) and the CrealityScan app is that daemon's GUI client — see
  `docs/recomp/FULL-INDEPENDENCE.md` §1 (layer L3). Probing only the app would report a
  false skip on every host where the daemon runs and the app is closed — the normal macOS
  state, and the state this host is in. The cross-machine grep token
  `(RPC daemon host) is not running` is unaffected.
- **The device probe is an OS enumeration, not the SDK's.** The SDK's own watchers start
  inside `obscan_context_create()` and report through the library, not to us, so the probe
  asks the OS (IOKit) whether a device with the scanner's vendor id (`0x2BC5`) is
  enumerated. The id is the one the SDK's own detection watches (README.md, "Device
  enumeration returns 0 devices ... — resolved").
- **The launcher no longer inserts a MoltenVK that is not there.** `dyld` aborts the
  process when an inserted dylib cannot be loaded, so a library path without a sibling
  `libMoltenVK.dylib` (the test double, a copied dylib) killed the worker. The insertion is
  now conditional on the file being there; the vendor install path is unchanged.
- **The Windows branch of the device probe is not exercised on this lane** (it lists the
  connected devices with `pnputil` and looks for `VID_2BC5`). The Windows lane has its own
  implementation (`win #120`).
- **Owed to the hardware room:** a real run of both listener tests with the scanner
  attached, to confirm that the probe's positive branch (both preconditions satisfied) lets
  the run proceed and that the tests still pass. Nothing in this change alters what the
  tests check — only what happens before them.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-19-plan130-precondition-skip '2026-10-19 22:41:58' <target>`:

> no crash report for 'device_message_listener_test' at/after 2026-10-19 22:41:58 — safe to record 'no crash report (checked DiagnosticReports for device_message_listener_test at/after the run start)'.

(same for `data_process_message_listener_test`).

## Board

- Row #130 → `DONE`: both paths are shown (skip 2 with a reason, failure 1), the preflight
  asserts the daemon and names the fix, and the record carries the evidence.
