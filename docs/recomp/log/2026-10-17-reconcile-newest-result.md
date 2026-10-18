- 2026-10-17 — reconcile: newest result
  (`2026-10-17-plan8-device-message-listener-crash-analysis.md`) compared line by
  line against `obscan_api.h` and
  [`findings/device-message-listener.md`](../recon/findings/device-message-listener.md).
  **2 spec claims contradicted** (#13 header hazard block, #14 findings §9), **1
  spec gap** (#15 re-add re-enters device creation), **1 implementation defect**
  (#16 `scan_server` stack-local listeners), **1 test defect** (#17 thread
  accounting / overstated OQ-4). No source changed in this step — tasks only.
  Evidence: the run's `.crash.ips`, `raw.log` (threads `14660842` vs `14660873`),
  and `device_message_listener_test/main.cpp`. Nothing here duplicates #11 — that
  row owns the crash itself; these own the contracts it falsified.
