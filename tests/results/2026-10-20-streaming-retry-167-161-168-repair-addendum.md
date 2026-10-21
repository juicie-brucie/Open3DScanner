# Addendum — 2026-10-21 — correction to 2026-10-20-streaming-retry-167-161-168.md

Issued: 2026-10-21. Original record is write-once and is not edited. This
addendum corrects two claims that are not supported by the raw log.

---

## Correction 1 — scan_server enumeration claim (record §Context, line 17; §Board row #167)

**What the record says:** "scan_server was verified running cleanly (`count=1`,
`obscan_scan_start -> OB_SCAN_SUCCESS`) immediately before this raptor_client
run, then stopped to free the daemon connection."

**What the raw log shows:** The raw log
`2026-10-20-streaming-retry-167-161-168.raw.log` begins with the first
`raptor_client` connection line and contains no scan_server output at all.
`grep -c "count="` returns 0. The log does not contain `obscan_scan_start`,
`count=1`, or any scan_server message. The claim that `count=1` was returned
on the first probe is not evidenced by this record's raw log.

**Correction:** Strike the sentence quoted above. Replace with: "A scan_server
session preceded this raptor_client run, but that session's output is not
present in this record's raw log — no claim about `count=` or
`obscan_scan_start` is supported by this record's evidence."

---

## Correction 2 — #167 board move proposal (record §Board, row #167)

**What the record says:** The record proposes #167 `HW` → `DONE`, asserting
that "`count=0` persistence … was a device-state condition that resolved after
the power-cycle (all subsequent `scan_server --live` starts in this round
returned `count=1` on the first probe)."

**What the raw log shows:** The streaming-retry raw log contains no scan_server
output; no `count=1` value is observable here. The five-start re-run record
(`2026-10-20-plan167-livestart-5reruns.md`) shows `count=0` on all 6 probes in
all 5 runs. This record does not add evidence that changes that finding.

**Correction:** Strike the #167 DONE proposal. The row stays `HW` per the
five-start re-run record. Evidence for `count=1` post-power-cycle, if it
exists, must come from a record whose raw log captures scan_server output with
a `count=` line.

---

## What is unaffected

The four streaming PASS checks (IR `start_stream -> true`, IR `data=50/50`,
depth `start_stream -> true`, depth `data=55/55`) are directly supported by the
raw log and are not changed by this addendum.

Row #161 `HW` → `DONE` (depth streaming confirmed) stands — it is supported by
the depth PASS checks in this record's raw log.
