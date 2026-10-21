# Addendum — 2026-10-21 — correction to 2026-10-20-plan71-attempt9-ab.md

Issued: 2026-10-21. Original record is write-once and is not edited. This
addendum corrects the Phase B window count and mean values.

---

## Correction — Phase B window count and means (record §A/B comparison table; §Checks row 5)

Raw log: `2026-10-20-plan71-attempt9-ab.raw.log`.

The toggle line `obscan_scan_plane_remove(false) ... [auto 1->0 tick=746]` is at
log line 7196. `grep -c "^\[perf\] window"` returns 33 total windows; 15 fall
before line 7196 (Phase A) and 18 fall after it (Phase B).

**Phase B windows from raw log (points= values in order):**

19,736.0 / 19,899.4 / 19,781.9 / 19,823.8 / 19,819.9 / 19,869.3 / 19,833.5 /
19,825.0 / 19,833.0 / 19,896.3 / 19,784.8 / 19,867.0 / 19,775.4 / 19,891.9 /
19,957.6 / 19,826.6 / 19,721.3 / 19,733.8

Corrections:

| Metric | Record says | Raw log shows |
|---|---|---|
| Phase B window count | 17 | **18** |
| Phase B first-6 mean | 19,810 | **19,821.7** (sum 118,930.3 / 6) |
| Phase B full mean | 19,832 | **19,826.5** (sum 356,876.5 / 18) |

Phase A settled figures (last 6 of 15 windows: 19,683.1 / 19,693.8 / 19,740.9 /
19,806.9 / 19,709.3 / 19,768.9 — mean 19,733.8) are correct in the record.

The overlap conclusion and step-change verdict are unaffected: the Phase B range
still overlaps Phase A's settled band and no step-change is detectable.

---

## FRAME_READY total

`grep -c "FRAME_READY tracking_status=0 (SUCCESS)"` returns **4856**, not the
"(inferred) 4498" cited in the record's §Session-end summary. The ticks=1609
figure in the `[perf] summary` line is confirmed in the raw log. The tracking
success verdict (100%) is unaffected; only the count is wrong.
