# 2026-10-20 — round b close: the queue drained

The round's landing ladder completed after `2026-10-20-round-b.md` was written; this
entry records the close, not a new round.

**Landed after the round entry:**

| PR | merge | verdict | what |
| --- | --- | --- | --- |
| #422 | `25618c7` | MERGEABLE @ `38cdb9f` | #163 P10 broadcast counters |
| #424 | `2174a66` | MERGEABLE @ `fd2c2da` | #164 P11 allocation-free S1 encoder |
| #426 | `25fdf33` | MERGEABLE @ `1b17d52` | #169 USB descriptor map (partial; gate open) |
| #428 | `0f39d74` | MERGEABLE @ `aec81d9` | #170 privileged helper skeleton |
| #423 | `a84a413` | MERGEABLE @ `8da0451` | #165 P12 record factory (script half) |
| #425 | — | script-tier @ `55d7dc0` | the round-close board/queue/log PR |
| #427 | — | script-tier @ `7d9bad8` | the #163/#165/#169 board pass |

**Board at close:** #163/#164 `DONE`; #165 `SPEC-READY` (the `_TEMPLATE.md` half is
blocked by a path permission — operator action); #169 `RECON` (the partial finding
landed; the operator's USB descriptor capture is the driver-model gate); #170 `HW` (the
skeleton landed; the live claim/release with the vendor daemon stopped is the operator
gate); #171/#172 held behind their dependencies; #100 remains the Phase D parent.

**Owed / next, in order:**

1. **P12 template** — the two-line `tests/results/_TEMPLATE.md` requirement; only the
   operator can apply it (tool-denied for builder and dispatcher).
2. **#169 USB descriptor capture** — the operator's `system_profiler` / protocol-analyzer
   read; it gates #171/#172.
3. **Hardware:** T0 (#167 live-start re-run, five starts) -> the #168 device re-run ->
   #161 depth retry -> T2/T3.
4. **#170 live claim/release** — vendor daemon stopped, claim, quit/unplug release.
5. **Next device-free builds when slots free:** #149, #154, #103, #118, #116.
