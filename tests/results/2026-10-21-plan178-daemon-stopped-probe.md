# PLAN #178/#182 — raw_capture daemon-stopped probe — PASS

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 18:21:12 >= newest source correlation.cpp 18:21:07
- Interposer: n/a — the target does not create an OBScan context; no launcher wrapping expected
- Run start: 2026-10-21 19:54:28

## Context

Daemon-stopped variant of the #178 probe [hw-confirmed: 2026-10-21-plan178-probe].
Hypothesis: stopping RPCServer before probing might reveal non-zero isochronous
alternate-setting bandwidth fields that IOKit hides when the daemon holds the interfaces.

Pre-run: `launchctl bootout gui/501 /Library/LaunchAgents/com.creality.RPCServer.plist`
(exit 0). Ports 10010 and 10013 both CLOSED/REFUSED before commands ran.

## Commands

```
build/tools/raw_capture/raw_capture --probe
build/tools/raw_capture/raw_capture --table
```

Post-run: `launchctl bootstrap gui/501 /Library/LaunchAgents/com.creality.RPCServer.plist`
(exit 0). After ~11 s: port 10010 OPEN, port 10013 still CLOSED/REFUSED.
launchctl list shows PID 61698 exit-status 0 for com.creality.RPCServer.

## Result

- Exit code: `0` (both commands)
- Verdict: **PASS**
- Raw log: `2026-10-21-plan178-daemon-stopped-probe.raw.log` (same directory)
- Interposer confirmation: n/a — target does not create an OBScan context

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | --probe output identical to daemon-running run | **[PASS]** byte-for-byte identical; all 55 lines match [hw-confirmed: 2026-10-21-plan178-probe] |
| 2 | No new fields populated (max_packet_size, interval, alternate_setting info) | **[PASS]** all endpoints still show `transfer=bulk max_packet_size=1024 interval=0`; HID still `interval=1`; no isochronous alt-settings appeared |
| 3 | --table `unknown` fields unchanged | **[PASS]** `usb endpoint: unknown` and `format/stride: unknown` for IR and depth; control still `unknown` |
| 4 | Both commands exit cleanly | **[PASS]** exit code 0 for both |

## Field-by-field comparison vs daemon-running probe

Every field in `--probe` output is identical. Every field in `--table` output is identical.
Stopping RPCServer did not expose any new descriptor data via IOKit.

The `unknown` fields in descriptor_table.h remain:
- IR stream USB endpoint address (which of 0x81/0x82/0x83 maps to IR)
- Depth stream USB endpoint address
- Format/stride for both IR and depth streams
- HID report_id (report descriptor not fetched)
- Laser/exposure control endpoint

## Crash sweep

no crash report for 'raw_capture' at/after 2026-10-21 19:54:28 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## Open questions

- Are the `unknown` USB endpoint fields populated only via the RPC→endpoint map (OQ-169e), and is IOKit simply not the source for that mapping regardless of daemon state?
- Port 10013 (IR RPC) remained CLOSED/REFUSED ~11 s after daemon bootstrap while port 10010 (depth RPC) was OPEN — is this normal startup sequencing or a daemon fault?
- Is there any other mechanism (USB descriptor dump, system_profiler, usbdump) that would reveal which of 0x81/0x82/0x83 the device's firmware assigns to IR vs depth without the RPC map?

## Board

- Row move: #178 `DONE` → `DONE` — no change; daemon-stopped probe confirms DONE verdict; `unknown` fields require RPC map, not IOKit
- Row move: #182 `HW` → `DONE` — daemon-stopped probe yields no new fields; the hypothesis is falsified; `unknown` fields in descriptor_table.h are not resolvable via IOKit alone
