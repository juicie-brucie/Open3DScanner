# Correction — `2026-10-17-device-message-listener-test.md`

**Date:** 2026-10-17
**Corrects:** [`2026-10-17-device-message-listener-test.md`](./2026-10-17-device-message-listener-test.md) — the 19:21 run
**Raised by:** dispatcher, after the crash-report archival check.

The record's open questions say the crash "was not verified in this run (no crash
dump/backtrace was captured here, unlike the plan8 run which had a `.crash.ips`
file)". That is true of the run's session, but not of the OS: macOS wrote a crash
report for this run.

- Source: `~/Library/Logs/DiagnosticReports/device_message_listener_test-2026-10-17-192132.ips`
- Archived as: [`2026-10-17-device-message-listener-test.crash.ips`](./2026-10-17-device-message-listener-test.crash.ips) (sha256 `7122283d4dfb229bb4d77709480cc681653e3e0107e9448cd813fe0c42f7d2bf`)

The session did not collect it — the hw-test instructions did not yet require
checking `DiagnosticReports`; `.claude/agents/hw-test.md` rule 11 now does. The
record's other conclusions stand unchanged; only the "no crash report exists"
reading is corrected.
