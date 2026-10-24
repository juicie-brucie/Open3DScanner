# PLAN #93 / #151 — RPC host up but device not detected — diagnostic observation — BLOCKED

Follow-up to `2026-10-21-plan93-plan151-browser-setup-blocked.md`. Operator reports CrealityScan
is now running, but its UI shows no device. This record is observation only — no run attempted,
no fix applied.

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next

## Observations

1. **Process check** (`pgrep -fl -i creality`, `pgrep -x RPCServer`, `launchctl list`):
   only the app process itself is present —
   `11713 /Applications/CrealityScan.app/Contents/MacOS/CrealityScan`
   (`launchctl` entry `application.com.creality.crealityscan.67958136.67958141`). No separate
   `RPCServer` process exists under that name.

2. **Port check**: `lsof -nP -iTCP -sTCP:LISTEN` shows CrealityScan listening only on
   `TCP *:57864`. Nothing is listening on 10010 or 10013 at this moment.

3. **Leftover helper processes** (`ps -ax`): four `openscanner-helper --claim iokit` processes
   (PIDs 81917/81919 since 14:48:40, 82291/82298 since 14:52:15 — both `sudo -n` and plain
   copies of each) are still running, ~8 hours old, started well before this session. These are
   this project's own IOKit-claim helper (row #170), not a vendor process.

4. **CrealityScan's own log**
   (`~/Library/Logs/Creality/CrealityScan/Player.log`, tailed live): repeating
   `avoid probing device using attached kernel interface`, interleaved with
   `[error][GVCPClient.cpp:355] sendto failed with error:49` every ~12–24s (error 49 =
   `EADDRNOTAVAIL`). No "device found"/"device added" line appears in the tailed window.

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | A vendor RPC daemon process distinct from the app is running | **[FAIL]** only the `CrealityScan` app process exists; no `RPCServer` process |
| 2 | Vendor RPC ports 10010/10013 have a listener | **[FAIL]** `lsof` shows no listener on either port; only `:57864` |
| 3 | CrealityScan's own log shows it finding the device | **[FAIL]** repeated `avoid probing device using attached kernel interface` + GVCP `sendto` errno 49, no device-found line |

## Open questions

- Is CrealityScan's "avoid probing device using attached kernel interface" line caused by our
  own `openscanner-helper --claim iokit` processes (PIDs 81917/81919/82291/82298, running since
  14:48/14:52, i.e. from an earlier, unrelated session) holding an IOKit claim on the USB
  device — should the operator (or coordinator) stop those four stray processes before retrying
  device detection in CrealityScan?
- Does CrealityScan's UI have a manual "connect"/"refresh devices" action the operator has not
  yet tried, independent of the above?
- Is GVCP (GigE Vision Control Protocol) expected at all for a USB-connected Raptor Pro, or is
  that log noise from an unrelated network-camera path in the same binary?

Lead: likely a leftover IOKit claim from this project's own `macos_helper` test processes
(`build/tools/macos_helper/openscanner-helper --claim iokit`, PIDs 81917/81919/82291/82298)
left running from an earlier session, colliding with CrealityScan's own device probe.
