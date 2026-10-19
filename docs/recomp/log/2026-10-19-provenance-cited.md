- 2026-10-19 — **`record-provenance` accepts a cited artifact**, not only a stem-matching companion. Real runs name
  their logs per condition (`<slug>-baseline.log`, `<slug>-enabled.log`) while the record is named for its verdict
  (`<slug>-confirmed.md`), so the stem rule rejected eight honest records. A record is now accompanied when **either**
  a companion starts with its stem **or** the record cites, in backticks, a raw artifact added in the same PR. The
  selftest pins four cases (stem companion; unrelated companion alone; cited added artifact; citation of a file not in
  the PR). The check remains gated — a record with neither still fails.
