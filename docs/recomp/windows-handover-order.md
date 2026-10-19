# Windows handover order — the first cross-machine run (`mac #115` / `win #113`)

**Status: DRAFT — the go-ahead is not given.** It waits on: the Mac's **#127 live reply dump**
(the client landed as #327; the live run is still owed) and **#124's container identification**;
and the fork's **`win #114`** (reconcile). The fork's `win #120` (precondition SKIP) and `win #121`
(preflight load **and** daemon) have **merged** — PR #28 @ `4d893d5` — so they are no longer waits.

## Vocabulary — read this first

Two different axes, both in the record:

- **Conditions** (the fork runbook's "six cells"): **2 client states × 3 phases, 30 s each** —
  the six conditions in the table below.
- **Result fields**: the six measurements taken **per condition**.

The order uses "conditions" and "result fields" so neither list is mapped onto the other.

## Preconditions

**Mac (this side):**
- No scan running, and the vendor daemon **stopped**, not merely idle; the cable released.
- The hw worktree synced and built at a recorded commit; the run's binary named in the record.
- The Mac does not start a scan for the duration of the window.

**Windows (their side):**
- **Start `CrealityScan.exe` and confirm the daemon is up before any test.** On Windows the app is
  what starts the daemon, so "vendor app not running" cannot be assumed: **launch the app, confirm
  the daemon, then either close the UI if the daemon survives, or leave it idle — and the record
  states which**, because it affects the raw numbers.
- The preflight prints **both** a **load PASS/FAIL** and a **daemon PASS/FAIL**, with a reason
  (`win #121`, merged). The daemon probe is a **process proxy** (`Get-Process -Name CrealityScan`) —
  there is no separate service to query, so the record says **process proxy**, never "daemon verified
  as a service".
- **Device-absent is a `SKIP` after `win #120`** (merged): both tests probe `CrealityScan.exe` and
  `VID_2BC5` and exit **2** with a specific reason. **A SKIP is not a measurement** — the operator must
  attach the device and start the daemon, and the record must never present a SKIP as a run.
- After the cable move, `Get-PnpDevice -PresentOnly` shows `VID_2BC5` with Depth/IR/RGB.

## Sequence

1. Mac: stop the daemon; confirm nothing holds the device; release the cable.
2. Operator moves the cable to Windows.
3. Windows: confirm enumeration (`VID_2BC5`, Depth/IR/RGB).
4. Windows: start `CrealityScan.exe`; confirm the daemon; record the app/UI state chosen.
5. Windows: `scripts/hw-preflight.ps1 scan_server` — prints the **load PASS/FAIL** and the
   **daemon PASS/FAIL** (process proxy), each with a reason.
6. Windows: the run — the six **conditions** × the six **result fields**.
7. Windows: postflight; the record written to `tests/results/`.
8. Operator returns the cable; Mac: restart the daemon and confirm enumeration.
9. The fork's `win #114` reconcile runs **before any board move** is claimed from the record.

## Conditions (six, 30 s each)

| # | client state | phase |
| --- | --- | --- |
| 1 | no client | still |
| 2 | no client | moved by hand |
| 3 | no client | paused–resumed |
| 4 | UI open | still |
| 5 | UI open | moved by hand |
| 6 | UI open | paused–resumed |

## Result fields (six, per condition)

| # | field | note |
| --- | --- | --- |
| 1 | raw frame delivery cadence | measured from **`get_frame` timing** — the fork's client is `scan_server`; the record must **not** claim the vendor client's poll |
| 2 | realized poll rate + tick breakdown | the `[perf] window` lines: pose, publish-getter, post+encode, broadcast, points/bytes, realized period — present via #17 |
| 3 | UI `data` fps (HUD) | |
| 4 | UI `render` fps (HUD) | |
| 5 | `raptor_client --live` summary (`bin`/`empty`/`error`, per port) | **"not ported"** — POSIX sockets, excluded from the sync (`mac #116` owns the shim); never blank |
| 6 | crash sweep | WER `ReportArchive` + Event Log 1000 with its own `AppName`/`AppPath`/time; if unavailable, the exact sentence `no Windows crash-report source available - sweep not performed` |

## Artifacts pinned for the window

- Scanner variant + serial: `<fill at go-ahead>`; USB link (3.x).
- `CrealityScan` **4.3.2**; `lib_orbbec_scan` build **3.5.3.260921**; `OrbbecSDK.dll` (unversioned).
- **No update or reinstall** of the vendor app during the window; if either changes, the comparison is void.
- Build provenance: compiler/MSVC, CMake and Ninja versions, and the configured `start_stream` profile.

## The record

- **Header: the parent's eight fields, identical** — Date / Worktree / Branch / Target / Other copy /
  Preflight / Interposer / Run start.
- **Windows preconditions block, after them**: build SHA + compiler/CMake/Ninja; device variant +
  serial + USB link; vendor versions as pinned; **machine spec** (Ryzen 9 5900X, 31.9 GB,
  Windows 11 Pro 26200, RTX 2080 Ti 11 GB — WMI under-reports the video memory to 4 GB, so record
  both); app/daemon state (which was chosen, per the precondition above); **browser** used for the HUD.
- **Raw log and HUD evidence committed beside the record in the same PR** — the gate's
  `record-provenance` requires it. The record is **agent-tier**.
- Conditions and result fields as two separate tables; the record states the condition when each
  result field was taken.

## SKIP wording when a precondition is not met

Merged in `win #120` (fork PR #28 @ `4d893d5`): exit **2**, on the test's **captured output** (stdout
there; the parent may use stderr — **the contract is the exit code and the text**), specific rather
than generic:

```
[worker] SKIP: precondition not met: CrealityScan.exe (RPC daemon host) is not running; listener checks NOT exercised.
[worker] SKIP: precondition not met: no VID_2BC5 device enumerated; listener checks NOT exercised.
```

**The cross-machine grep token is `(RPC daemon host) is not running`.** The parent emits
`CrealityScan (RPC daemon host) is not running` (its process name), so both records grep alike. The
device case is literal on both sides: `no VID_2BC5 device enumerated`. A genuine init failure stays
**1**. The parent's row `#130` carries the same contract.

## Reference

Fork rows: `win #113` (the run), `win #119` (the loader fix, folded into the parent), `win #120`
(precondition-vs-FAIL), `win #121` (preflight load assertion), `win #114` (reconcile).
Parent rows: `mac #115` (the cross-machine check), `#130` (our own precondition-vs-FAIL defect),
`#127`/`#124` (the Mac-side work gating the go-ahead).
