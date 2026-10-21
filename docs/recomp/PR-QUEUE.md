# PR landing queue

Owner: dispatch. One landing in flight; the GitHub merge queue is not used.

## Queue — 2026-10-21 (after the telemetry/USB wave)

**No open PRs.** The wave landed #437–#448 (except unused numbers); see the wave log
`2026-10-21-telemetry-usb-wave.md`.

## Landed this wave

| PR | Row | What |
| --- | --- | --- |
| #437 | #143 | pull semantics (latest, 0 % empty, 6–7 Hz sample; e1→e2 correction) |
| #438 | #141 | tick instrument (pacing, residual, exposure/release segments) |
| #439 | #149 | `model.ply` reader (full-cloud reader half) |
| #440 | #150 | IMU retro (axis, temperature candidates, gyro) |
| #441 | #175 | `raptor_client` frame telemetry |
| #442 | #176 | retrospective live-telemetry distribution |
| #443 | #114 | stream-profile enumeration |
| #445 | #171 | raw-capture path (UVC/HID, descriptor table) |
| #446 | #71 | plane-toggle retro (the count is not sufficient) |
| #447 | #172 | RPC shim (offline source, 11-element whole-frame array) |
| #448 | — | the #71 attempt-9 A/B record (evidence) |
| #434 | — | the #132 gap-free capture record (evidence) |

## Next

1. The operator cards (hardware queue T7): #169 descriptor capture → #171 live capture →
   #170 claim/release → #172 vendor A/B.
2. Device-free builds when slots free: **#154** (probe + presets), **#103** (README),
   **#118** (`guard-hw-run` read check), **#116** (Windows portability).
3. The held record repairs (streaming-retry, four-new-selftests, plan151-content-cadence,
   plan71-attempt9 table) and #165's `_TEMPLATE.md` half.

A PR is added here when it opens; it leaves when merged or closed.
