# Audit addendum — explicit one-SDK-user line + Other-copy line backfill (4 records)

Per `tests/hardware/README.md` ("never edited in place"), this is a
separate addendum, not an edit to any of the four original records listed
below.

**Gap 3 — one-SDK-user not stated as its own explicit line.** All four
records below already contain the underlying fact — each one's
`Preflight:` line reads "...no conflicting process..." (the name-anchored
`pgrep -x` check `scripts/hw-preflight.sh` performs, which *is* the
one-SDK-user confirmation) — but, unlike `2026-10-20-plan153-live-run.md`
and later records, it was not pulled out into its own separate "One-SDK-
user confirmed" line. Restating it explicitly here, same wording as the
other records that do have it:

> One-SDK-user confirmed: `pgrep -x scan_server`/`raptor_client`/etc.
> empty before this run (per the Preflight line already in each record:
> "no conflicting process").

Applies to:
- `2026-10-20-plan151-device-run-raceloss.md`
- `2026-10-20-plan151-device-retry2-raceloss.md`
- `2026-10-20-plan71-attempt6-raceloss.md`
- `2026-10-20-plan93-markerless-raceloss.md`

**Gap 4 — missing "Other copy" line.** All four are `scan_server` runs;
every other `scan_server` record this session (e.g.
`2026-10-20-plan153-live-run.md`, `2026-10-20-plan76-coverage-frozen.md`)
includes the app-bundle copy's path and mtime. These four crashed very
early (during device enumeration, before any scan/record-worthy state)
and the line was omitted in the rush to capture the crash observable —
backfilling it here. All four ran back-to-back with no rebuild in
between, so the bundle copy's mtime is the same across all four (matches
the mtime already visible in each record's own `Preflight:` freshness
line, `17:26:47`):

> Other copy: `build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server` (mtime 17:26:47, refreshed only on an app build per #107)

## Corrected header fields, per record

**`2026-10-20-plan151-device-run-raceloss.md`**
- Other copy: `build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server` (mtime 17:26:47, refreshed only on an app build per #107)
- One-SDK-user confirmed: `pgrep -x scan_server`/`raptor_client`/etc. empty before this run (per the Preflight line's "no conflicting process")

**`2026-10-20-plan151-device-retry2-raceloss.md`**
- Other copy: `build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server` (mtime 17:26:47, refreshed only on an app build per #107)
- One-SDK-user confirmed: `pgrep -x scan_server`/`raptor_client`/etc. empty before this run (per the Preflight line's "no conflicting process")

**`2026-10-20-plan71-attempt6-raceloss.md`**
- Other copy: `build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server` (mtime 17:26:47, refreshed only on an app build per #107)
- One-SDK-user confirmed: `pgrep -x scan_server`/`raptor_client`/etc. empty before this run (per the Preflight line's "no conflicting process")

**`2026-10-20-plan93-markerless-raceloss.md`**
- Other copy: `build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server` (mtime 17:26:47, refreshed only on an app build per #107)
- One-SDK-user confirmed: `pgrep -x scan_server`/`raptor_client`/etc. empty before this run (per the Preflight line's "no conflicting process")

None of this changes any verdict, result, or finding in any of the four
original records — it is condition documentation only.
