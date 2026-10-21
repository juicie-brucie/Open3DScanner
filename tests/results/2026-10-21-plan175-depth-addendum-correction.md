# Correction addendum — 2026-10-21-plan175-depth-addendum.md

Date: 2026-10-21
Corrects: `2026-10-21-plan175-depth-addendum.md`

## Defect 1 — All six timing values wrong (sourced from wrong run)

The addendum's "Verbatim timing line" and telemetry table were quoted from the
first (interactive, unsaved) raptor_client run on depth port 10010. The archived
raw log (`2026-10-21-plan175-depth-addendum.raw.log`) contains the second run's
output, whose values differ on all six numeric fields.

Raw log line 320 (verbatim):
```
[timing] depth: n=62 clock=steady_clock p50=167us p95=259us max=945us mean=190us; payload=0B/s wire=2437B/s; first_frame=none; timeouts=0; window=8082619us
```

Raw log line 319 (verbatim):
```
[live] done: get_frame=62 data=0 empty=62 error=0; stream stopped
```

Corrected telemetry table (replacing the addendum's table):

| Metric | Value (raw log) | Value quoted in addendum (wrong) |
| --- | --- | --- |
| Calls (n) | 62 | 62 (correct) |
| p50 | 167 µs | 177 µs |
| p95 | 259 µs | 302 µs |
| max | 945 µs | 2,162 µs |
| mean | 190 µs | 224 µs |
| payload B/s | 0 | 0 (correct) |
| wire B/s | 2,437 | 2,435 |
| first_frame | none | none (correct) |
| timeouts | 0 | 0 (correct) |
| window | 8,082,619 µs | 8,088,807 µs |
| data/empty/error | 0/62/0 | 0/62/0 (correct) |

## Defect 2 — Exit code not sourced from raw log

The addendum states `Exit code: 1`. The raw log file does not contain an EXIT:
line (the `echo "EXIT:$?"` was not redirected to the file). The exit code 1 is
confirmed from the bash tool output captured during the run (`EXIT:1` printed to
the terminal after the command completed). The exit code is correct; the raw log
source is absent.

Note: the raw log ends at line 320 with the `[timing]` line. There is no
`EXIT:` line in `2026-10-21-plan175-depth-addendum.raw.log`.

## Verdict and board

No change — verdict remains FAIL, board move unchanged (#175 stays HW).
The observable (zero data frames, exit 1, `start_stream -> non-error`) is
confirmed by the raw log.
