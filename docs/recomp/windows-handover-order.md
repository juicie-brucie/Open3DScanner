# Windows handover order — the first cross-machine run (`mac #115` / `win #113`)

**Status: DRAFT — the go-ahead is not given.** The device stays on the Mac until #127's live reply
dump has run, #320's extractor counts are the corpus, and #124's container identification has its
data. This file is the order the go-ahead will send; it exists so the fork lane can check its shape
before the window opens.

## Preconditions

**Mac (this side):**
- No scan running, and the vendor daemon **stopped**, not merely idle; the cable released.
- The hw worktree synced and built at a recorded commit; the run's binary named in the record.
- The Mac does not start a scan for the duration of the window.

**Windows (their side):**
- **`CrealityScan.exe` started and the daemon up before any test runs.** After `win #119` the SDK
  *loads*, so the old skip-on-load-failure path is unreachable: with no daemon, both listener tests
  exit **1** at `obscan_context_create()` and ctest records **FAILED**, not skipped — and the
  interposer banner still prints, so the spurious failure looks like a real run.
- The preflight asserts the SDK **loads**, not merely that the DLL exists; a present-but-unloadable
  SDK is a **FAIL**, not the expected state.
- After the cable move, `Get-PnpDevice -PresentOnly` shows `VID_2BC5` with Depth/IR/RGB.

## Sequence

1. Mac: stop the daemon; confirm nothing holds the device; release the cable.
2. Operator moves the cable to Windows.
3. Windows: confirm enumeration (`VID_2BC5`, Depth/IR/RGB).
4. Windows: start `CrealityScan.exe`; confirm the daemon is up.
5. Windows: `scripts/hw-preflight.ps1 scan_server` — asserts the load, not existence.
6. Windows: the run (the six cells below).
7. Windows: postflight; the record written to `tests/results/`.
8. Operator returns the cable; Mac: restart the daemon and confirm enumeration.
9. The fork's `win #114` reconcile runs **before any board move** is claimed from the record.

## Artifacts pinned for the window

- Scanner variant + serial: `<fill at go-ahead>`
- `CrealityScan` **4.3.2**; `lib_orbbec_scan` build **3.5.3.260921**
- **No update or reinstall** of the vendor app during the window; if either changes, the comparison is void.

## The record

**Header fields:** Date; worktree + branch + HEAD; target binary and how it was built; other copy (if any);
preflight (`.ps1` name + result); interposer — **N/A with the DLL path recorded and the load asserted**;
preconditions (daemon up, one SDK user, vendor app state); run start (dry and live); device (variant, serial,
USB link); vendor versions as pinned above.

**Six cells**, mapped one-for-one onto `mac #115`:

| # | Cell | Note |
| --- | --- | --- |
| 1 | the daemon's raw frame delivery rate | |
| 2 | the realized poll rate + tick breakdown | "not ported" if #111's instrumentation is not, never blank |
| 3 | the UI's `data` fps | |
| 4 | the UI's `render` fps | |
| 5 | `raptor_client --live` summary (`bin`/`empty`/`error`, per port) | "not ported" if the client is not — never blank |
| 6 | the crash sweep (WER / Event Log form) | or the exact "sweep not performed" sentence with its reason |

## Reference

- Fork-side rows: `win #113` (the run), `win #119` (the loader fix, now folded into the parent),
  `win #120` (the precondition distinction), `win #114` (reconcile).
- Parent-side rows: `mac #115` (the cross-machine check), `#130` (the parent's own
  precondition-vs-FAIL defect, filed from the fork's finding), `#127`/`#124` (the Mac-side work
  that gates the go-ahead).
