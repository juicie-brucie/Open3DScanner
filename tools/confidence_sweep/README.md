# confidence_sweep — the confidence/retention experiment's harness (row #159)

One question: **does a per-point confidence signal let us throw points away and
keep the geometry accurate?** The harness answers it with a curve — accuracy
against retained points — and, beside it, the curve for **random retention at
the same retained fraction**. The random curve is the point of the tool: a
confidence signal only counts if it beats random.

The estimator, its assumptions and the conditions that would falsify it are in
`docs/recomp/log/2026-10-20-row-159-confidence-experiment.md`.

Device-free, dependency-free and deterministic: the same input produces the
same CSV and the same plot, byte for byte.

## The inputs

| | |
| --- | --- |
| the cloud | `--ply FILE` (ascii or binary PLY), `--rec FILE` (a recorded V1 session), or `--synthetic` (the self-test scene) |
| the ground truth | `--truth plane NX NY NZ D`, `--truth sphere CX CY CZ R`, or `--truth ply FILE` |
| the confidence field | `--confidence residual` (the default estimator) or `--confidence file FILE` |

A confidence value is **higher = more trusted**. A field file is one value per
point, either `index,value` or `value` per line, in point order.

## The sweep

`--sweep-steps N` (default 21) rows, the retained fraction running from 100 %
down to `--sweep-min-fraction` (default 5 %). Each row keeps the most trusted
points down to that fraction and reports:

- `rmse` — the root-mean-square distance from the kept points to the truth;
- `p95` — the 95th percentile of the same distances;
- the same two for a **random** keep of the same size, averaged over
  `--random-trials` (default 20) draws from a fixed `--seed`.

`--absolute` sweeps a raw confidence value between `--sweep-min` and
`--sweep-max` instead of a quantile.

## The acceptance

`--expect-beats-random` makes the tool exit **3** unless the confidence curve
beats the random control at **every** row of the acceptance window (every row
except full retention, where both curves are the same set of points — 20 of
the 21 rows by default) and the mean relative improvement clears
`--min-improvement`.

> **The curve must beat random retention to count.** A confidence signal that
> does not beat a random keep of the same size carries no information about
> which points to keep, whatever it correlates with.

## The commands

```sh
# the self-test scene, with the CSV and the plot
build/tools/confidence_sweep/confidence_sweep --synthetic \
    --csv sweep.csv --plot sweep.svg

# the acceptance, as the tool's own exit code
build/tools/confidence_sweep/confidence_sweep --synthetic --expect-beats-random

# a real capture: the vendor's model.ply against a taped wall at 400 mm
build/tools/confidence_sweep/confidence_sweep \
    --ply model.ply --truth plane 0 0 1 0.400 \
    --csv sweep.csv --plot sweep.svg --expect-beats-random

# a recorded session: the last cloud frame in the recording
build/tools/confidence_sweep/confidence_sweep \
    --rec session.rec --rec-mode last --truth plane 0 0 1 0.400 \
    --csv sweep.csv --plot sweep.svg
```

The CSV's columns: `threshold` (on the confidence scale), `retained_fraction`,
`n_retained`, `rmse`, `p95`, `median`, then the random control's
`rmse_random_mean`, `rmse_random_std`, `p95_random_mean`, `median_random_mean`.
All the distances are metres.

## The self-test

`ctest -LE hardware` runs three entries:

- `confidence_sweep_test` — the synthetic scene, the estimator's separation of
  the clean and the bad region, the sweep, the **negative control** (a shuffled
  confidence field must *not* beat random), determinism, and the PLY and
  recorded-session round trips;
- `confidence_sweep_cli_test` — the acceptance through the command line;
- `confidence_sweep_artifacts_test` — the CSV and the SVG are reproducible, and
  they still match the committed synthetic run
  (`fixtures/synthetic_sweep.csv`, `fixtures/synthetic_sweep.svg`).

## The synthetic scene

A 160 × 160 grid at 4 mm on the plane `z = 0` (25,600 points). The quadrant
`x > 0, y > 0` is the **bad** region: a 1.5 mm bias and 2.0 mm of noise. The
rest is clean (0.15 mm of noise). The truth is the plane itself, so the
harness knows every point's error exactly.
