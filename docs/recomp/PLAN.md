# Recomp plan

Single source of truth for the recon → build → hw → reconcile loop. This is a
**queue**, not a design doc: one line per task, move the status as you go.

Status: `TODO` · `RECON` · `SPEC-READY` · `BUILD` · `HW` · `DONE` · `BLOCKED`

## Active

| # | Target | Status | Notes |
| --- | --- | --- | --- |
| 1 | Device-message listener registration (trigger for SDK device-watcher) | TODO | README "Known issues": undocumented; listener is what starts the watcher thread. |
| 2 | Data- and process-message listeners before `obscan_scan_preview` | TODO | Ordering requirement; preview fails otherwise. |
| 3 | `obscan_scan_get_realtime_model_mesh` read-only-after-pause constraint | TODO | Partially observed; recover exact lifetime rules. |
| 4 | `obscan_scan_pause` / `stop` / `session_destroy` clean teardown (4 client ports) | TODO | Confirm all four ports released (RGB/IR/IMU/depth). |

## Open questions (from recon)

_Recon appends unresolved items here. Each should name the target and the specific
unknown._

## Log

_Append dated one-liners: what changed, which agent, and the outcome._
