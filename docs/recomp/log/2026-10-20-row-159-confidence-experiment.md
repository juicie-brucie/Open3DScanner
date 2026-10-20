# 2026-10-20 — row #159: the confidence/retention experiment (design + offline harness)

**Row:** #159 — *The confidence/retention experiment — accuracy vs retained points, with ground truth.*
**Status after this entry:** `TODO` → **`SPEC-READY`** — the design and the harness exist and the synthetic
self-test passes. **`DONE` is not earned:** the row's acceptance is the curve on the **real** capture, and that
capture belongs to **#126** (the device) and is currently blocked behind **#157** (the live path's `start_stream`
suffix defect).
**Wall:** `docs/recomp/**`, `tools/**`, `scripts/**`. No change to `engine/include/openscanner/obscan_api.h`, no
change to any findings file. No device, no vendor SDK: the harness links nothing but the C++ standard library.

**Deliverables**

| | |
| --- | --- |
| the design | this note — §1 the scope, §2 the estimator and its falsification, §3 the metric, §4 the sweep and the control, §5 the acceptance, §6 the capture protocol for #126 |
| the harness | `tools/confidence_sweep/` — the tool, the self-test, the committed synthetic run (§7) |
| the synthetic self-test | passes; the numbers in §7.2 |

---

## 1. What this row delivers, and what it does not

The row asks for one curve: **accuracy against retained points**, on a scan with known ground truth, with a
per-point confidence that predicts which points to keep. The curve only says something if it is read against a
control, so the harness always produces **two** curves — the confidence curve and the curve for **random retention
at the same retained fraction** — and the acceptance is stated against the second.

**What is in:** the estimator choice and what would falsify it (§2), the metric (§3), the sweep and the control
(§4), the acceptance (§5), the capture protocol that #126 must run (§6), and a device-free harness that produces
the CSV and the plot for any cloud + truth + confidence field (§7).

**What is not in, and why:** the curve on real data. It needs a scan of a scene whose geometry is known to the
millimetre, which is exactly what **#126** creates, and it needs the live capture path, which **#157** blocks.
Nothing in this row can be run against the device from here. The harness is deliberately built so that #126's
capture drops straight into it: a PLY, a plane, one command.

---

## 2. The estimator: what the signal can be, and which one this row takes

`docs/recon/findings/scan-quality-gate.md` §2 established that the vendor's export surface carries **no per-point
or per-region quality signal** — only a per-frame tracking-status enum. So the confidence has to be derived.
Three options were surveyed.

### 2.1 Option 1 — the per-point local residual on the fused cloud (**chosen**)

**What it is.** For each point, take its `k` nearest neighbours (default `k = 16`), fit a plane to them, and take
the **RMS distance of the neighbourhood to that plane**. The residual is a cost; the confidence is its negation
(higher = more trusted). The neighbourhood comes from a uniform grid sized by the cloud's median nearest-neighbour
distance, and the plane's normal comes from a 3×3 symmetric eigen-decomposition (cyclic Jacobi).

**Why this one.**

- It is computable **offline on whatever cloud we already have** — the vendor's fused `model.ply`, a V1 frame
  from a `--record` session, or a synthetic cloud. It needs no provenance, no extra capture mode and no new
  vendor call. (Which of those clouds the *ground truth* is expressed for is a separate question, answered in
  §6.2.)
- It costs nothing to be wrong: if it does not beat random, we have learned that the device's errors are not
  local roughness, which is itself the answer (and it points at option 2).
- The curve it produces is a **ranking**, so it is insensitive to the confidence's scale and units — the sweep
  is over quantiles, not over absolute values.

**What it assumes** (each one is a way for it to fail):

1. **The error is local.** A point's error is visible in its own neighbourhood. This is the assumption that
   matters: a patch that is *shifted along the surface normal* but is smooth is **locally planar**, so its
   residual is as low as the clean region's. The estimator is blind to a systematic, locally-planar error.
2. **The cloud is locally sampled at a roughly uniform spacing.** The neighbourhood radius scales with the
   median spacing; a wildly non-uniform cloud (a dense foreground over a sparse background) gets a
   neighbourhood that mixes scales.
3. **The truth surface is locally flat over the neighbourhood's scale.** "Off the surface" and "rough" then
   coincide. A truth surface with structure at the scale of the neighbourhood (a checkerboard's edge, a
   cylinder's curvature) would inflate the residual of correct points.

**What would falsify it** — stated in advance, so the real run can settle it:

- **F1.** On the #126 capture, the confidence curve does **not** beat random retention at the same retained
  fraction (or beats it by a few percent, inside the control's spread). Then the local residual does not predict
  the point-to-truth error for this scanner, and the estimator is wrong for this device's error model.
- **F2.** The residual fails to separate a region with a *known* bias (a synthetic or taped offset patch). Then
  the signal is not measuring what it is claimed to measure.
- **F3.** The curve beats random on the synthetic scene but not on the real capture. Then the synthetic scene's
  error model (local roughness) is not the real one, and the real errors are systematic — registration, scale or
  pose drift — which points at **option 2** (attribution from our own fusion) instead.
- **F4.** The curve beats random on the real capture but only in the region of the wall nearest the scanner.
  Then the signal is measuring range-dependent noise rather than registration quality, and it will not survive a
  change of scene.

**The blind spot, stated plainly.** Assumption 1 is not a detail: a *smooth* offset region is invisible to this
estimator. That is the single most likely way the real run could disappoint, and it is why the synthetic scene's
bad region carries **both** a bias and noise (the bias alone would not be detected).

### 2.2 Option 2 — point attribution from per-tick clouds (our own fusion)

**What it is.** Keep the per-frame clouds and their poses, fuse them ourselves, and record for each fused point
which frames contributed to it and how well each of those frames registered. The per-point confidence is then
built from the contributing frames' residuals.

**What it would require.** (a) The per-frame cloud *with* its pose, per tick — the V1 `pose` message carries the
4×4 matrix, and `obscan_frame_get_frame_cloud` gives the frame's own cloud, but the frame object is one-shot and
its point count and buffer ownership are not documented (see the header's notes on the frame family).
(b) A **fusion stack of our own** — registration plus a per-point residual per correspondence.
(c) The correspondence bookkeeping: which frames contributed to which fused point.

**Why it is not this row.** It is a much larger piece of work, and it only works on clouds **we** fused. The
artifact #126 produces is the **vendor's** fused cloud, which carries no provenance at all, so option 2 could not
score it. It is also *our* residuals that option 2 measures — a signal about our registration, not about the
device. It stays the second choice: if F1 fires but F3 shows the real errors are registration errors, option 2 is
the honest answer.

### 2.3 Option 3 — a vendor signal

**What the ABI actually offers** (from the declared header — the vendor binary was not read):

- the fused cloud is `ObScanPointColorClouds`: points, **normals**, colours, plus a bbox. The normals are
  geometry, not confidence;
- per frame: the **tracking-status enum** (`obscan_frame_get_frame_tracking_status`), two **distance-advice
  arrays** (4-byte entries, a count, no mapping to points recovered) and the **reference-rods** view (unmapped);
- per the findings (§2 above), **no per-point quality value anywhere in the export surface**.

**Verdict: option 3 is not available as a confidence source.** The only per-point fields the SDK carries are
positions, normals and colours; none of them is a quality signal, and the per-frame advice arrays have no
recovered mapping onto points.

**What is left of it, and it is useful:** the tracking-status enum is a **frame-level** label — the vendor's own
reason for a frame ("textureless", "moving too fast", "insufficient valid points"). Dropping whole frames on it
is a legitimate, cheap pre-filter *before* the per-point sweep. It is not a per-point confidence, and the harness
does not pretend otherwise: the confidence field is an input, so a future frame-level filter would feed the
harness a cloud with whole frames removed.

---

## 3. The metric

For each point `i`, `d_i` is the **unsigned distance from the point to the truth surface** — the plane's
`|n·p − d|`, the sphere's `| ‖p − c‖ − r |`, or the distance to the nearest point of a truth point set.

For a kept set, the harness reports:

| | |
| --- | --- |
| `rmse` | `sqrt( mean(d_i²) )` over the kept points — the headline number |
| `p95` | the 95th percentile of `d_i` (nearest rank) — the tail |
| `median` | the median of `d_i` — for reference |

**Why RMSE and not the mean:** a mean can be moved by a handful of outliers; the RMSE is the number a downstream
surface fit would feel, and it punishes a kept set that hides a bad patch. **Why p95 and not the max:** the max is
one point. All three are in metres.

The random control is scored with the **same** metric on the **same** points.

---

## 4. The sweep and the control

**The sweep.** 21 rows (`--sweep-steps`, default 21), the retained fraction running from 100 % down to
5 % (`--sweep-min-fraction`). 20 of those rows are in the acceptance window; the full-retention row is
not, by construction. The threshold on each row is the confidence at the retention boundary, i.e. a **quantile**
of the confidence field. A quantile sweep is distribution-free: the same curve comes out whatever scale the
confidence is on, and the "threshold" column is the value a consumer would have to pin to get that retained
fraction. `--absolute` sweeps a raw confidence value between `--sweep-min` and `--sweep-max` instead, for when
the field has a usable absolute scale.

**The control.** For each row, `--random-trials` (default 20) random keeps of the same size, drawn from a fixed
`--seed` (default `20261020`), scored with the same metric. The CSV carries the control's **mean** and its
**standard deviation**, and the plot draws the mean as a dashed curve with a ±1 s.d. band. The seed is fixed so
the whole run repeats exactly: the same input gives the same CSV and the same SVG, byte for byte.

**What the two curves mean.** The confidence curve is the accuracy of "keep the most trusted points". The random
curve is the accuracy of "keep any points" — the accuracy a retention policy gets with no information at all. The
gap between them is the entire value of the confidence signal.

---

## 5. The acceptance

> **The curve must beat random retention to count.**

A confidence signal that does not beat a random keep of the same size carries **no information** about which
points to keep, whatever else it may correlate with. The synthetic self-test pins this from both sides: a real
signal must beat random, and a **shuffled** signal must not — without the second half, a harness that reported a
winner for any field at all would pass.

Operationally, the criterion is:

1. at **every** row of the acceptance window — every row except full retention, where both curves are the same
   set of points and neither can beat the other — the confidence curve's `rmse` is **below** the random control's
   mean `rmse`; and
2. the mean relative improvement over the window clears `--min-improvement` (default 0).

The harness makes this an **exit code**, not a reading of a table: `--expect-beats-random` exits **3** when the
criterion is not met. `ctest -LE hardware` runs that command on the synthetic scene, so a regression in the
estimator or the control fails the build.

---

## 6. The capture protocol for #126 (what the real run has to do)

This is the protocol the harness's numbers will be produced from. It belongs to #126 (it owns the device and the
record); it is stated here so the two rows agree on what "the real run" means.

### 6.1 The scene

- A **flat, matte wall** — a painted MDF panel or a drywall panel, big enough to fill the frame at the closest
  distance. Matte, because a specular wall's brightness changes the depth quality with the exposure.
- The scanner at a **fixed height**, on a tripod or a table, not touched between distances.
- **Three distances: 300 / 400 / 500 mm**, measured from the scanner's **origin** (not the front face of the
  housing) to the wall's surface with a steel tape, each ±1 mm. Three marks on the floor or the table so the
  distance can be reset exactly and the reset can be checked.
- A **checkerboard** — printed squares of a known size (20.0 mm), taped flat **in the wall's plane**, inside the
  frame at every distance. It gives a second, independent length scale (its corners' spacing) and the IR path's
  cross-check.

### 6.2 The ground truth, in the frame the harness scores in

The harness scores a cloud **in that cloud's own frame**. Two of our inputs are in **different** frames, and this
is the trap: the same numeric plane cannot serve both.

- The **published cloud** — what `--record` writes and what the live view shows — is in the **scanner's frame**,
  with no export transform applied to it.
- The vendor's **`model.ply`** is written *after* the SDK applies the model's **rotate pose** (the transform is
  described in `docs/recon/findings/scan-live-data.md` §16, and the matrix is printed in the scan log as
  "rotate pose for model.ply"). It is in the **rotated** frame.

**So the recorded session is the authoritative input for this experiment.** The tape and the wall are physical;
the scanner's frame is the frame the wall is measured in; and the published cloud is the one that is in it.
`model.ply` is usable only with the truth plane carried through the same rotate pose (or with the pose undone) —
its matrix is in the scan log, so the run can do it, but it is a second thing to get right and it is not needed.

**How the taped distance becomes the numeric plane.** The wall is square to the scanner, so the distance from the
scanner's origin to the wall along the wall's normal **is** the taped distance (300/400/500 mm, ±1 mm), and the
wall's normal in the cloud's frame is the scanner's optical axis. Our findings do **not** pin the scanner's axis
convention in the cloud's frame, so the normal is obtained by **fitting the wall to the cloud** and the **tape
stays the anchor**:

```sh
# 1. fit the wall, and check the fitted offset against the tape
build/tools/confidence_sweep/confidence_sweep --rec session.rec --rec-mode last \
    --fit-plane --fit-plane-check 0.400
#    [fit] normal (0.000, 0.000, 1.000), fitted offset 0.3998 m, fit residual: rms 0.0004 m, p95 0.0009 m
#    [fit] the fitted offset 0.3998 m against the taped 0.400 m: delta -0.0002 m (tolerance 0.002 m)
#    [fit] OK - the fit agrees with the tape.

# 2. the sweep, against the taped plane with the fitted normal
build/tools/confidence_sweep/confidence_sweep --rec session.rec --rec-mode last \
    --truth plane 0.000 0.000 1.000 0.400 --csv sweep.csv --plot sweep.svg --expect-beats-random
```

**What the fit's residual is, and is not.** The fit's residual is the cloud's distance to the wall's *own*
best-fit plane. It says whether the cloud is a single plane at all: a residual much larger than the expected noise
means something else was in frame (the table's edge, the floor) or that the fit failed, and the run stops there
rather than reporting a curve about the wrong surface. The wall should fill the frame, so that the fit sees the
wall and nothing else.

**The limitation, stated rather than discovered later.** Because the normal comes from the fit, a **systematic
tilt** of the whole cloud relative to the wall would be **absorbed** by the fit and would not appear in the
metric. That is a real limit of this truth model: the curve measures the cloud's roughness and its offset from
the taped distance, not a global tilt. The experiment's question — *does the confidence signal rank the points by
error?* — is not affected, because a global tilt is locally planar and the estimator does not claim to detect it
(§2.1's assumption 1). A tilt-sensitive measurement is the checkerboard's geometry, which is #126's own metric,
not this curve.

**The other checks on the taped distance.** The **checkerboard's spacing** is the second, independent number: the
measured corner spacing against the printed 20.0 mm. The wall is set square to the scanner with a bubble level
(**≤ 0.5°**), and the level reading is recorded. Everything goes in the run's record: the tape readings, the level
reading, the board's printed size, and a photograph of the setup.

### 6.3 The sequential vendor oracle run

The vendor's own app is the **oracle** on the same static scene: our capture says what our pipeline sees, the
vendor's says what the device can do, and the two are compared on identical geometry. The runs are **sequential,
never concurrent**, and the scene is not touched between them:

1. set the scene, tape-measure, photograph it;
2. **our** capture — one process, one SDK user (`scan_server --live --record <session>.rec`), then close it;
3. the **vendor's** app on the same scene → the oracle's model;
4. the harness on the recorded session's cloud, against the taped plane with the fitted normal (§6.2).

**One SDK user at a time** is a hard rule of the run, not a preference: the device's daemon serves one client,
and a second SDK user is refused. So the order above is sequential by construction, and no step may be left
running while the next one starts. (The live path's own defect — the `start_stream` method name — is #157; until
it is fixed, step 2 has to go through whatever path is known to work, and the run records which one was used.)

### 6.4 What the real run must produce

- the **recorded session's last cloud frame** (the authoritative input — §6.2) and the tape-measured truth: the
  normal from the wall's fit, the offset from the tape, and the fit's residual;
- the **CSV** and the **SVG** from the harness, on that cloud;
- the verdict: does the confidence curve beat the random control at the same retained fraction (§5)?

A verdict of "no" is a legitimate outcome of the run and is recorded as such — F1 in §2.1 is a result, not a
failure of the run.

---

## 7. The harness

`tools/confidence_sweep/` — one C++17 tool, no dependencies beyond the standard library, deterministic.

| file | |
| --- | --- |
| `harness.h` / `harness.cpp` | the core: the truth, the estimator, the sweep, the control, the synthetic scene |
| `io.cpp` | the PLY reader/writer, the recorded-session reader, the confidence-field reader |
| `plot.cpp` | the SVG plot (written by hand, so it needs no plotting library and is byte-reproducible) |
| `main.cpp` | the command line |
| `sweep_test.cpp` | the self-test |
| `test_artifacts.py` | the artefact test (reproducibility + the committed run) |
| `fixtures/synthetic_sweep.csv`, `fixtures/synthetic_sweep.svg` | the committed synthetic run |

The recorded session's envelope is decoded with the project's own codec
(`engine/include/openscanner/frame_protocol.h`) — the wire format is not restated in the tool.

**The two modes of the sweep.** The default is a **quantile** sweep: the retained fraction is chosen, and the
threshold is the confidence at the retention boundary. `--absolute` instead sweeps a **raw confidence value**
between `--sweep-min` and `--sweep-max`, and there the threshold decides how many points survive — so the row's
`retained_fraction` is the fraction **actually kept**, not the sweep's nominal one, and the acceptance window is
driven off that same actual value (a row that is effectively full retention falls outside it, as §5 requires).

**`--fit-plane`** fits a plane to the cloud (total least squares) and reports the normal, the offset and the
fit's residual — the truth's normal for a real capture, and the run's own check that the cloud is a single
surface (§6.2). `--fit-plane-check D` compares the fitted offset with the taped distance `D` and exits **4** if
they disagree by more than `--fit-plane-tolerance` (default 2 mm).

### 7.1 The commands

```sh
# the self-test scene, with the CSV and the plot
build/tools/confidence_sweep/confidence_sweep --synthetic \
    --csv sweep.csv --plot sweep.svg

# the acceptance, as the tool's own exit code (3 when the curve does not beat random)
build/tools/confidence_sweep/confidence_sweep --synthetic --expect-beats-random

# the real capture (#126), step 1: fit the wall and check it against the tape
build/tools/confidence_sweep/confidence_sweep \
    --rec session.rec --rec-mode last --fit-plane --fit-plane-check 0.400

# the real capture, step 2: the sweep, against the taped plane with the fitted normal
build/tools/confidence_sweep/confidence_sweep \
    --rec session.rec --rec-mode last --truth plane 0.000 0.000 1.000 0.400 \
    --csv sweep.csv --plot sweep.svg --expect-beats-random
```

**The vendor's `model.ply` is in the rotated frame** (§6.2), so it is not the input for this experiment: the
same numeric plane would be measuring the wrong surface. If a run uses it anyway, the truth plane has to be
carried through the same rotate pose (the matrix is in the scan log).

### 7.2 The synthetic self-test — the result

The scene: a **160 × 160 grid at 4 mm on the plane `z = 0`** (25,600 points). The quadrant `x > 0, y > 0` is the
bad region — a **1.5 mm bias** and **2.0 mm** of noise; the rest is clean (0.15 mm of noise). The truth is the
plane, so every point's error is known exactly.

| | |
| --- | --- |
| the clean region's RMSE against the truth | **1.50 × 10⁻⁴ m** |
| the bad region's RMSE against the truth | **2.51 × 10⁻³ m** |
| the estimator's residual, clean region | **1.35 × 10⁻⁴ m** |
| the estimator's residual, bad region | **1.58 × 10⁻³ m** (11.7× the clean region's) |
| the whole cloud's RMSE / p95 | **1.26 × 10⁻³ m** / 3.23 × 10⁻³ m |

The sweep (21 rows, 100 % → 5 % retained; 20 of them in the acceptance window):

| retained | the confidence curve's RMSE | the random control's RMSE | the ratio |
| --- | --- | --- | --- |
| 100 % | 1.260 × 10⁻³ m | 1.260 × 10⁻³ m | 1.0 (the same set of points) |
| 76.2 % | 2.97 × 10⁻⁴ m | 1.259 × 10⁻³ m | 4.2× |
| 71.5 % | **1.47 × 10⁻⁴ m** | 1.257 × 10⁻³ m | **8.6×** |
| 52.5 % | 1.29 × 10⁻⁴ m | 1.261 × 10⁻³ m | 9.8× |
| 47.8 % | 1.25 × 10⁻⁴ m | 1.257 × 10⁻³ m | 10.0× |
| 5.0 % | 8.34 × 10⁻⁵ m | 1.252 × 10⁻³ m | 15.0× |

The knee is where it should be: the bad region is a quarter of the points, so dropping the worst ~25 % removes
it, and the curve falls to the clean region's noise. The random control is flat — it never gets better, because
it has nothing to choose with.

**The verdict on the synthetic scene:** the confidence curve beats random at **20 of 20** rows of the acceptance
window; the mean relative improvement is **78.3 %**, the worst row **12.5 %**; the area between the curves is
**9.08 × 10⁻⁴ m**. The whole sweep takes **0.16 s** of user time on 25,600 points (one M1, this room's
machine — a cost figure, not a claim about any other machine).

**The negative control:** a **shuffled** confidence field — the same values, permuted — does **not** beat random
(1.254 × 10⁻³ m against the control's 1.257 × 10⁻³ m at 47.8 % retained: the shuffled curve lands on the
random curve, as it must). Without this check, the first result would prove nothing.

### 7.3 The committed run and the tests

`fixtures/synthetic_sweep.csv` and `fixtures/synthetic_sweep.svg` are the committed synthetic run (sha256
`43cd65b8…c97984` and `4f98e4f9…a93947`). `ctest -LE hardware` runs three entries — all green:

- `confidence_sweep_test` — the scene, the estimator's separation of the two regions, the sweep, the negative
  control, determinism (the same input gives the same numbers), the PLY round trip, the recorded-session round
  trip (against the project's own encoder) and the confidence-field round trip; plus the **absolute mode** (the
  reported fraction is the fraction actually kept, and the effectively-full row is outside the acceptance
  window), the **sweep's minimum fraction** (a non-default minimum changes the sweep) and the **fitted plane**;
- `confidence_sweep_cli_test` — the acceptance through the command line;
- `confidence_sweep_artifacts_test` — the CSV and the SVG are byte-reproducible across two runs, the CSV carries
  both curves, the confidence curve beats random at every row, the absolute mode's fractions match what it kept,
  `--sweep-min-fraction` reaches the sweep, the plane fit runs and its check fails loudly, and the numbers still
  match the committed run (compared with a relative tolerance of 10⁻⁶, because the last bits of a float can move
  between compilers).

---

## 8. What is owed, and to whom

| owed | by whom | blocked on |
| --- | --- | --- |
| the curve on **real** data (the row's acceptance) | the hardware tester, with the harness as it stands | **#126** (the device, the scene, the taped truth) and **#157** (the live capture path) |
| the vendor oracle run on the same scene | the same run | #126's protocol, §6.3 |
| option 2 (attribution from our own fusion), if F1 fires | a build row | this note's §2.2 |

The harness is not the blocker and does not need to change for the real run: it takes a PLY or a recorded session,
a plane, and one command.
