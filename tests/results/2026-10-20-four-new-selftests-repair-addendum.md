# Addendum — 2026-10-21 — correction to 2026-10-20-four-new-selftests.md

Issued: 2026-10-21. Original record is write-once and is not edited. This
addendum corrects the four per-test timing values and the command block.

---

## Correction 1 — per-test timing values (record §Checks, rows 1–4)

Raw log: `2026-10-20-four-new-selftests.raw.log`.

| Check | Timing in record | Timing in raw log |
|---|---|---|
| 1 `device_probe_test` | `Passed 0.34 sec` | `Passed    0.00 sec` |
| 2 `ws_broadcast_counters_test` | `Passed 0.26 sec` | `Passed    0.21 sec` |
| 3 `fast_encode_test` | `Passed 0.06 sec` | `Passed    0.01 sec` |
| 4 `raptor_client_live_timeout` | `Passed 1.03 sec` | `Passed    0.91 sec` |

All four tests passed; the verdicts are unaffected. Only the timing figures
are wrong in the record.

---

## Correction 2 — command block (record §Command)

**What the record says:** The command block shows a separate
`ctest --test-dir build -R 'device_probe_test|raptor_client_live_timeout|ws_broadcast_counters_test|fast_encode_test' --output-on-failure`
invocation before the full-suite run.

**What the raw log shows:** The raw log records a single continuous ctest run
from Test #1 through Test #21 (`100% tests passed out of 21`) with no `-R`
filter. There is no filtered run for the four new tests separately. The `-R`
invocation shown in the record's command block did not produce output in this
raw log.

**Correction:** The `-R` invocation line is not evidenced by this raw log. The
evidenced command is the full-suite `ctest --test-dir build --output-on-failure`
(or equivalent) that ran tests 1–21. Check 5 (`100% tests passed out of 21`)
is unaffected.
