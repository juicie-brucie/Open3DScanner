# Correction — `2026-10-17-device-message-listener-test-run2.md`

**Date:** 2026-10-17
**Corrects:** [`2026-10-17-device-message-listener-test-run2.md`](./2026-10-17-device-message-listener-test-run2.md) — the 20:01 run
**Raised by:** dispatcher, after the crash-report archival check.

The record says the crash "was **not independently root-caused here** (no debugger
attached, no crash dump captured)" and that no "`.ips` crash report or debugger
attach was captured here". True of the run's session, but not of the OS: macOS
wrote a crash report for this run.

- Source: `~/Library/Logs/DiagnosticReports/device_message_listener_test-2026-10-17-200148.ips`
- Archived as: [`2026-10-17-device-message-listener-test-run2.crash.ips`](./2026-10-17-device-message-listener-test-run2.crash.ips) (sha256 `7129e1dafb23f186f899ddad89ce9a699bdfde4f29763a095db5cb8289e0831e`)

The session did not collect it; `.claude/agents/hw-test.md` rule 11 now requires
the check on every non-clean exit. This correction only adds the archival fact —
the record's crash conclusions were reached without this report and are not
re-assessed here. See the plan8 crash analysis cited by the record, which
retracts its libomp attribution.
