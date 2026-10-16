# Logs from the live-capture reverse-engineering phase

Raw logs collected while getting `obscan_scan_preview`/`obscan_scan_start`/
`obscan_scan_stop` working end-to-end against a physical CR-Scan Raptor Pro
(see the top-level README's "Known issues" and git history around this
period). Kept as a record of what was actually observed, not curated —
expect noise, dead ends, and the same bug reproduced multiple times across
different config attempts.

- `native_scan_log/` — the vendor SDK's own `scan_log_*.txt` files
  (`ObScanContextConfig::output_ob_scan_log_dir`). This is where the
  "cannot called start !", "the line laser cannot support voxel_size",
  `processBLAEFrame skip!`, and `lasere total_points_num` evidence used
  throughout this investigation came from.
- `native_sdk_log/` — the lower-level `ScannerStreamSDK.log`
  (`output_ob_sdk_log_dir`), mostly per-property USB/RPC traffic.
- `live_capture_test_runs/` — our own tool's stdout/stderr for individual
  test runs (`tools/live_capture_test`), showing the ABI call sequence and
  status codes we observed, cross-referenced against the native logs
  above.

These contain this device's real serial number, firmware version, and
local network info (nothing secret — no credentials/keys) — acceptable
here since this is a private record of one person's own hardware, not
anything redistributed from Creality/Orbbec.
