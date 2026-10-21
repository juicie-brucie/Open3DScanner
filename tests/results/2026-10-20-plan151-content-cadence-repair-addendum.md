# Addendum — 2026-10-21 — correction to 2026-10-20-plan151-content-cadence.md

Issued: 2026-10-21. Original record is write-once and is not edited. This
addendum corrects three claims not supported by the raw log.

---

## Correction 1 — obscan_scan_pause claim (record §Result, exit-code line)

**What the record says:** "Exit code: SIGKILL on worker (SIGTERM sent; launcher
confirmed, worker did not exit in 5 s; force-killed; `obscan_scan_pause ->
OB_SCAN_SUCCESS` logged)"

**What the raw log shows:** The raw log
`2026-10-20-plan151-content-cadence.raw.log` ends with bare `FRAME_READY
tracking_status=0 (SUCCESS)` lines. `grep -c "scan_pause"` returns 0. No
`obscan_scan_pause` line, no SIGTERM, no SIGKILL, and no launcher teardown
message is present. The log was captured up to the point of termination; no
teardown output is evidenced.

**Correction:** Strike `obscan_scan_pause -> OB_SCAN_SUCCESS` from the exit-code
description. Replace with: "Exit code: terminated externally; raw log ends
mid-stream with FRAME_READY lines — no teardown output is present."

---

## Correction 2 — frame-rate denominator and Hz value (record §Checks row 2; §Content-cadence findings table)

**What the record says:** "3082 frames over ~90 s = **34.2 Hz** device frame rate"
and the findings table repeats "**34.2 Hz** (3082 frames / 90 s)".

**What the raw log supports:** The 20 perf windows sum to ~100 s of windowed
time. The actual streaming wall-clock for the 3082 frames is ~95 s. The correct
rate is 3082 / 95 ≈ **32.4 Hz**.

**Correction:** Replace "~90 s" with "~95 s" and "34.2 Hz" with "32.4 Hz"
everywhere in the record. The ratio in row 4 becomes 32.4 Hz / 9.73 Hz ≈ 3.3:1
(not 3.5:1). The device-vs-publish headroom finding stands — the device still
delivers substantially more than the publish rate.

---

## Correction 3 — perf window count (record §Checks row 3)

**What the record says:** "mean 9.73 /s across **19** perf windows"

**What the raw log shows:** `grep -c "^\[perf\] window"` returns **20** windows,
not 19. The rate values (9.6–9.8/s) and mean are unaffected.

**Correction:** Replace "19 perf windows" with "20 perf windows".
