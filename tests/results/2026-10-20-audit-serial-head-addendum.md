# Audit addendum — device serial + HEAD backfill (7 records)

Per `tests/hardware/README.md` ("never edited in place"), this is a
separate addendum, not an edit to any of the seven original records
listed below. Written in response to a condition-documentation audit of
every device-touching record from the 2026-10-20 session.

**Gap 1 — missing device serial number.** All seven records below
identify the device only as "Creality Raptor Pro" without its serial.
Every other device-touching record this session (starting from
`2026-10-20-plan71-plane-removal-attempt5.md` onward) used the serial
**`CR0886100SN`** — the same physical device throughout the whole
session; no device was swapped at any point. Backfilling that value here
for these seven:

- `2026-10-20-plan127-live-reply-dump.md`
- `2026-10-20-plan127-live-reply-dump-retry.md`
- `2026-10-20-plan127-depth-retry2-hang.md`
- `2026-10-20-plan132-gap-free-capture.md`
- `2026-10-20-plan132-gap-free-capture-retry.md`
- `2026-10-20-plan132-ir-retry3-hang.md`
- `2026-10-20-plan141-live-tick-split.md`

**Gap 2 — missing HEAD field**, in three of the above seven:
- `2026-10-20-plan127-live-reply-dump.md` (run start 2026-10-20 01:15:52)
- `2026-10-20-plan132-gap-free-capture.md` (run start 2026-10-20 01:16:28)
- `2026-10-20-plan141-live-tick-split.md` (run start 2026-10-20 01:17:14)

**HEAD at the time of these three runs: `efe9253`** (Merge pull request
#369 from juicie-brucie/dispatch/board-154). This is confirmed directly
from this session's own transcript, not inferred from commit authorship
dates: `git log --oneline -1` was run at the very start of today's
dispatch, immediately before the first of these three runs, and returned
`efe9253`; no further `git fetch`/merge happened in this session until
well after all three runs (the next sync landed `71aadd5`, first seen in
the later `ungated-smoke-batch3` investigation). Note that `efe9253`'s
own commit timestamp (`2026-10-20 00:52:59`) is *earlier* than when this
session actually synced to it — commit-date proximity to a run's
timestamp is not a safe way to infer HEAD, which is why this was
confirmed from the session's own recorded command output instead. This
is also the same HEAD already recorded in
`2026-10-20-plan71-plane-removal-attempt5.md` (run start 01:55:32),
consistent with no sync happening between these four runs.

## Corrected header fields, per record

**`2026-10-20-plan127-live-reply-dump.md`**
- HEAD: `efe9253`
- Device: Creality Raptor Pro, serial `CR0886100SN`, CrealityScan daemon running, device enumerated, no other SDK user

**`2026-10-20-plan127-live-reply-dump-retry.md`**
- Device: Creality Raptor Pro, serial `CR0886100SN`, CrealityScan daemon running, device enumerated, no other SDK user

**`2026-10-20-plan127-depth-retry2-hang.md`**
- Device: Creality Raptor Pro, serial `CR0886100SN`, daemon running, device enumerated, no other SDK user

**`2026-10-20-plan132-gap-free-capture.md`**
- HEAD: `efe9253`
- Device: Creality Raptor Pro, serial `CR0886100SN`, CrealityScan daemon running, device enumerated, no other SDK user

**`2026-10-20-plan132-gap-free-capture-retry.md`**
- Device: Creality Raptor Pro, serial `CR0886100SN`, daemon running, device enumerated, no other SDK user

**`2026-10-20-plan132-ir-retry3-hang.md`**
- Device: Creality Raptor Pro, serial `CR0886100SN`, daemon running, device enumerated, no other SDK user

**`2026-10-20-plan141-live-tick-split.md`**
- HEAD: `efe9253`
- Device: Creality Raptor Pro, serial `CR0886100SN`, daemon running, device enumerated, no other SDK user

None of this changes any verdict, result, or finding in any of the seven
original records — it is condition documentation only.
