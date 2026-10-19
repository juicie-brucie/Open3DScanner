- 2026-10-19 — first runs of four ctests under their own names: **tracking_status_test**, **bounded_watchdog_test**,
  **mesh_tools_smoke_test** PASS; **smoke_test** PASS per its own exit-code contract (exit 3 = recovered from the known
  `obscan_context_destroy()` vendor SIGSEGV), with the crash archived. **Correction recorded**: `smoke_test` is *not*
  hardware-free at runtime — it dlopens the real vendor SDK and forks a worker that enumerates the device — so it carries
  the gated tests' risk profile and belongs in preflight's target knowledge (filed). Open questions left by the run:
  `get_device_num` reported 0 on all seven samples after `DeviceManager` logged the device added; and the crash's frequency
  is not established.
