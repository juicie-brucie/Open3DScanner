# Correction addendum — 2026-10-21-plan151-browser.md

Date: 2026-10-21
Corrects: `2026-10-21-plan151-browser.md`

Two defects found by arithmetic check and file-system verification:

## Defect 1 — Frame counter multiplier wrong (clock/math defect)

Lines 35–37 of the original:
> Note: frame numbers #991, #1395, #1673 are the published-frame counter at the time
> of each sample; the difference #991→#1673 over 20 s ≈ 682 frames / 20 s = 34.1
> published ticks/s, which matches the server's 9.7 Hz publish rate (682 / 20 = 34.1
> — consistent with the 4× counter increment cadence of the HUD's frame# field).

**Error:** 34.1/s does not match 9.7 Hz. The correct multiplier is:
- Publishes over 20 s at 9.7 Hz: 9.7 × 20 = 194
- Counter increments per publish: 682 / 194 = **3.51×**, not 4×

The note's "consistent with the 4× counter increment cadence" is wrong.
The observed cadence is ~3.5 counter increments per publish.

Corrected note:
> Note: frame numbers #991, #1395, #1673 are the published-frame counter at the time
> of each sample; the difference #991→#1673 over 20 s = 682 counter increments.
> At 9.7 Hz publish rate over 20 s: 9.7 × 20 = 194 publishes; 682 / 194 ≈ **3.5
> counter increments per publish** (not 4×; the HUD's frame# counter increments
> at ~3.5× the publish rate in this session).

## Defect 2 — Wrong record citation extension (citation defect)

Line 95 of the original:
> - How do frame age and drop rate change at higher server publish rates (e.g. 24 Hz
>   or 50 Hz, as tested device-free in `2026-10-19-plan141-tick-instrument.log`)?

The cited path `2026-10-19-plan141-tick-instrument.log` uses the wrong extension.
The archived record is `tests/results/2026-10-19-plan141-tick-instrument.md`
(confirmed: `.md` file exists; the `.log` file alongside it is the raw log, not the record).

Corrected citation:
> as tested device-free in `2026-10-19-plan141-tick-instrument.md`

## Verdict and board

No change to verdict (PASS) or board move — all eight checks remain valid.
The browser fps, frame age, and drop observations are unaffected by the counter-math
correction.
