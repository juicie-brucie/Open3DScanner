# Denoising and Point Cloud Fusion: Algorithm Survey

Research notes for OpenScanner's reconstruction pipeline. Covers the two stages every
structured-light / ToF scanner app (Creality Scan included) has to get right between a raw
depth/point capture and a printable mesh: **denoising** individual scans, and **fusing**
multiple scans/frames into one consistent surface.

## 1. Point cloud denoising

Raw scanner output has two distinct error modes that need different treatment:

- **Outliers** — isolated points far from the true surface (flying pixels at depth
  discontinuities, IR interference, reflective/dark materials).
- **Surface noise** — small per-point displacement around the true surface (sensor
  quantization, thermal noise, calibration error).

Most pipelines run outlier removal first, then a surface-noise filter, then (optionally)
a learned refinement pass.

### 1.1 Outlier removal

| Method | Idea | Notes |
|---|---|---|
| Statistical Outlier Removal (SOR) | Flag points whose mean distance to *k* nearest neighbors is > N standard deviations from the dataset mean | Cheap, implemented in PCL/Open3D (`remove_statistical_outlier`); good first pass |
| Radius outlier removal | Drop points with fewer than *k* neighbors within radius *r* | Simpler, good for sparse flying-pixel removal at depth edges |
| Density-based (DBSCAN-style) clustering | Cluster by local density, drop small/low-density clusters | Handles clustered outlier blobs SOR misses |

### 1.2 Classical surface-noise filters

- **Bilateral filtering (point-based)** — the point-cloud analogue of image bilateral
  filtering (Fleishman et al. 2003, "Bilateral Mesh Denoising" for the mesh variant).
  Projects each point along its normal using a weighted average of neighbors, with
  weights falling off both in *spatial* distance and in *normal/feature* difference. This
  is the standard way to smooth noise **without** washing out edges/corners — critical
  for scanned mechanical parts.
- **Moving Least Squares (MLS) / APSS** — fits a local polynomial (or algebraic point
  set surface) to each point's neighborhood and projects the point onto it. Very good at
  removing high-frequency jitter on smooth regions; tends to round off sharp features
  unless combined with a sharp-feature-aware variant (RIMLS — Robust Implicit MLS,
  Öztireli et al. 2009 — adds robust statistics so it doesn't smear across edges).
  Implemented in CGAL (`jet_smooth_point_set`, `bilateral_smooth_point_set`) and PCL
  (`MovingLeastSquares`).
- **WLOP (Weighted Locally Optimal Projection)** — Huang et al. 2009. Resamples a noisy,
  possibly non-uniform point cloud onto a clean, evenly-distributed approximation of the
  underlying surface using an L1-median-style iterative projection. Good for consolidating
  multiple overlapping scans into one clean, uniformly-dense set — CGAL ships this as
  `wlop_simplify_and_regularize_point_set`. A natural step right before meshing.
  **EAR (Edge-Aware Resampling)**, a follow-up to WLOP by the same group, additionally
  sharpens edges during resampling.
- **Jet fitting / normal estimation via PCA** — most of the above require good per-point
  normals first; normals are usually estimated by local PCA over a k-NN neighborhood
  (the smallest-eigenvalue eigenvector of the local covariance matrix), optionally
  smoothed/propagated via a minimum spanning tree for consistent orientation (Hoppe et
  al. 1992's classic approach, still the default in Open3D/PCL/MeshLab).

### 1.3 Mesh-domain denoising (post-reconstruction)

Once a mesh exists (e.g. post Poisson reconstruction), denoising is usually done on
**face normals**, then the mesh is re-fit to the smoothed normal field, because
operating in normal space preserves sharp edges far better than smoothing vertex
positions directly:

- **Bilateral mesh denoising** (Fleishman et al. 2003) — bilateral filter over face/vertex
  normals.
- **Guided mesh normal filtering** (Zhang et al. 2015) — uses a joint-bilateral-style
  "guidance normal" patch selection to avoid over-smoothing near features; one of the
  strongest classical mesh denoisers and a common MeshLab/CGAL baseline to compare
  against.
- **L0 mesh smoothing** — sparsity-promoting optimization that produces a piecewise-flat
  result with crisp edges, good for printed/mechanical geometry.
- **Cascaded Normal Regression** (Wang et al. 2016) and later learned normal filters —
  learn the Zhang et al.-style guidance step via regression instead of hand-tuned
  thresholds.

### 1.4 Learned / deep denoising

Relevant for a project explicitly building toward AI-assisted scanning:

- **PointCleanNet** (Rakotosaona et al. 2019) — two-stage PointNet-style network: first
  classifies/removes outliers, then regresses a per-point displacement to project
  remaining points onto the surface. One of the first strong learned point denoisers.
- **Pointfilter** (Zhang et al. 2020) — predicts per-point displacement directly in a
  rotation-invariant local frame; robust across different noise levels without retraining.
- **Score-Based Point Cloud Denoising / ScoreDenoise** (Luo & Hu, ICCV 2021) — models the
  noisy point distribution's log-density gradient (a "score") and denoises via gradient-
  ascent steps toward the clean manifold; conceptually close to diffusion models and
  generalizes well to unseen noise patterns.
- **DMRDenoise** (Luo & Hu 2020) — denoise-by-downsample-then-upsample using a
  differentiable manifold reconstruction, avoids some of the oversmoothing of direct
  displacement regression.
- **Total Denoising / unsupervised methods** — learn directly from noisy scans with no
  clean ground truth (self-supervised consistency across noisy observations of the same
  surface), relevant because scanner training data is usually unpaired.
- 2025 surveys worth tracking for the current state of the art: Wang et al., *"A Survey
  of Deep Learning-based Point Cloud Denoising"* (arXiv:2508.17011) and Zhang et al.,
  *"Deep Learning For Point Cloud Denoising: A Survey"* (arXiv:2508.11932) — both frame
  denoising as outlier-removal + surface-restoration and benchmark supervised vs.
  unsupervised methods.

### Recommendation for OpenScanner's pipeline

A practical, dependency-light first pipeline, ordered:
1. Radius/statistical outlier removal (cheap, kills flying pixels at depth edges).
2. PCA normal estimation + MST orientation propagation.
3. Bilateral or RIMLS point smoothing (edge-preserving).
4. WLOP/EAR resampling when consolidating multiple overlapping captures into one cloud
   before meshing.
5. Leave a hook for a learned post-filter (Pointfilter/ScoreDenoise-style) later — these
   are small models and a reasonable place for OpenScanner's "AI" angle to add real value
   over Creality Scan's classical pipeline.

## 2. Point cloud fusion (multi-scan / multi-frame)

Fusion is the problem of turning N partial, independently-posed point clouds (either
multiple turntable rotations, or a live handheld sweep) into one consistent model.

### 2.1 Pairwise registration

- **ICP (Iterative Closest Point)** — alternates (a) finding nearest-neighbor
  correspondences between two clouds and (b) solving for the rigid transform minimizing
  correspondence error, until convergence.
  - *Point-to-point ICP* — original Besl & McKay 1992 formulation, minimizes squared
    Euclidean distance between matched points.
  - *Point-to-plane ICP* — minimizes distance along the target's normal direction instead;
    converges faster and more robustly on piecewise-smooth surfaces, the de facto default
    today (Open3D's `registration_icp` with `TransformationEstimationPointToPlane`).
  - *Generalized ICP (GICP)* — Segal et al. 2009, models each point with a local covariance
    (plane-ish) and minimizes a Mahalanobis-distance cost; effectively a probabilistic
    unification of point-to-point and point-to-plane.
  - *Colored ICP* (Park et al. 2017) — adds a photometric term using per-point color, which
    matters a lot for texture-rich scans where geometry alone under-constrains the
    tangential (in-plane) alignment.
  - ICP needs a decent initial alignment (it only finds the nearest *local* minimum).
    Initial poses typically come from turntable encoder angle, IMU, feature matching
    (FPFH + RANSAC global registration), or fiducial markers.

### 2.2 Global / multi-way registration

Pairwise ICP accumulates drift over long sequences, so production pipelines register
pairs first and then **globally optimize all poses at once**:

- **Pose graph optimization** — build a graph where nodes are scan poses and edges are
  pairwise ICP transforms (with a fitness/information weight); solve a nonlinear least
  squares problem (e.g. Levenberg–Marquardt via g2o/Ceres) that spreads residual error
  across the whole graph rather than letting it accumulate along a chain. Open3D exposes
  this directly as `registration.global_optimization`.
  - **Loop closure** is the key win here: when a turntable scan or handheld sweep returns
    to a previously-seen view, detecting that and adding the corresponding edge lets the
    optimizer correct accumulated drift around the whole loop — this is the single
    biggest practical difference between "looks fine for 10 frames" and "stays aligned
    for a full 360° scan."
- **Bundle-adjustment-style joint optimization** — treat all frame poses (and optionally
  a few surface parameters) as one joint optimization, common in SLAM-style handheld
  scanning rather than discrete turntable captures.

### 2.3 Volumetric / TSDF fusion (the KinectFusion family)

For frame-rate depth streams (structured light / ToF handheld scanning) rather than
discrete still captures, volumetric fusion is the standard approach:

- **KinectFusion** (Newcombe et al. 2011) — the seminal pipeline: track camera pose each
  frame via ICP against a raycast of the current reconstruction, then fuse the new depth
  map into a 3D voxel grid of a **Truncated Signed Distance Function (TSDF)**. Each voxel
  stores a running-weighted-average estimate of signed distance to the nearest surface;
  fusing new data is just updating that average, which is why it's cheap and stable to
  run live. The surface is extracted at the TSDF's zero-crossing via raycasting (for live
  preview) or Marching Cubes (for a final mesh).
  - Open implementations: **KinFu** (in PCL), **InfiniTAM**, **Open3D**'s
    `pipelines.integration.ScalableTSDFVolume` / `t.geometry.VoxelBlockGrid`.
  - Limitation: a dense regular voxel grid bounds the reconstructable volume and uses a
    lot of memory; **voxel hashing** (Nießner et al. 2013) and Open3D's scalable/hashed
    TSDF address this by only allocating voxel blocks near the observed surface.
- **Surfel-based fusion** (point-based, not voxel-based) — represents the surface as a
  cloud of oriented disks ("surfels": position, normal, radius, confidence), updated
  frame-to-frame by fusing new measurements into existing surfels or spawning new ones.
  - **ElasticFusion** (Whelan et al. 2015) — adds *non-rigid* deformation of the surfel
    map to correct for loop closures without needing pose-graph bundle adjustment,
    notable for dense, map-centric (rather than frame-centric) SLAM.
  - Surfels are more memory-efficient than dense voxel grids for sparse/thin structures
    and don't require choosing a bounding volume up front — relevant for an
    object-scanning app (vs. room-scanning) where object extent is unknown beforehand.
- **Signed-distance vs. point/mesh fusion trade-off**: TSDF/voxel approaches give very
  clean, watertight output (good for print-ready meshes) but cost memory and blur very
  thin features; surfel/point fusion preserves thin features and arbitrary topology
  better but needs a separate meshing step (Poisson, ball-pivoting) at the end.

### 2.4 Surface reconstruction after fusion

Once points/surfels are fused and denoised:

- **Poisson Surface Reconstruction** (Kazhdan et al. 2006; screened variant 2013) — treats
  oriented points as samples of an indicator-function gradient and solves a Poisson
  equation over an adaptive octree for a smooth, watertight mesh. The standard choice
  when point normals are reliable (which bilateral/MLS-denoised, globally-registered
  scans usually are) — this is what PCL, Open3D, MeshLab and CloudCompare all ship as
  their default "points → mesh" button.
  - **Screened Poisson** adds a point-interpolation constraint to the energy so the
    reconstructed surface actually passes near the input points (vanilla Poisson can
    smooth sharp detail away); this is the version almost everyone uses in practice.
- **Ball Pivoting Algorithm (BPA)** (Bernardini et al. 1999) — "rolls" a ball of a chosen
  radius across the point cloud, connecting points it touches into triangles. Faster, more
  local, and keeps holes as holes (doesn't try to close gaps the way Poisson does) —
  useful when you'd rather see missing-data holes than have Poisson hallucinate a smooth
  surface over them.

### Recommendation for OpenScanner's pipeline

- **Turntable-style capture** (discrete frames at known angular steps, Creality Scan's
  primary mode): pairwise point-to-plane ICP per adjacent pair seeded from the encoder
  angle → global pose-graph optimization with loop closure at 360° → WLOP/bilateral
  cleanup of the merged cloud → Screened Poisson for the final watertight mesh.
- **Handheld / live-preview mode** (if pursued later): TSDF or hashed-voxel fusion
  (Open3D's `VoxelBlockGrid` is a solid, already-available starting point) for the
  live reconstruction, with the option to re-run Screened Poisson on the final fused
  point set for a cleaner offline export.

## Open-source building blocks referenced above

**OpenScanner ships under MIT, so every dependency we actually vendor or link has to be
MIT-compatible — permissive (MIT/BSD/Apache-2.0) or, at most, file-level weak-copyleft
(MPL-2.0) where we never modify the library's own files.** Anything (L)GPL is excluded
below: static/combined linking of (L)GPL code would force the combined work under those
terms, which conflicts with staying MIT. Where the best-known classical implementation
of an algorithm happens to be GPL, the table calls that out and names the permissive
substitute we use instead.

| Library | Role | License | OK to vendor/link? |
|---|---|---|---|
| [Open3D](https://github.com/isl-org/Open3D) | ICP, global registration, TSDF/voxel-hash fusion, Poisson/BPA reconstruction — covers most of sections 2.1–2.4 out of the box | MIT | Yes |
| [PCL](https://github.com/PointCloudLibrary/pcl) | SOR/radius outlier removal, MLS, KinFu, Poisson/greedy triangulation | BSD-3-Clause | Yes |
| [meshio](https://github.com/nschloe/meshio) | Mesh format I/O (used by the conversion tool, §3) | MIT | Yes |
| ~~[CGAL](https://github.com/CGAL/cgal)~~ | ~~WLOP/EAR, jet/bilateral point smoothing~~ | GPL/LGPL (dual) | **No** — excluded. WLOP/bilateral point smoothing is implemented directly against Open3D's point/normal types instead (small enough to own outright; see §1.4 recommendation) |
| ~~[vcglib](https://github.com/cnr-isti-vclab/vcglib)~~ / ~~MeshLab~~ | ~~mesh-domain denoising, Poisson, BPA, curvature filters~~ | GPL-3 | **No** — excluded. Open3D's Poisson/BPA cover the reconstruction step; see §3 for the mesh-editing ("Meshmixer-like") replacement stack |

Open3D is the pragmatic anchor dependency: ICP, global registration with loop closure,
TSDF/surfel-style fusion, and Poisson/BPA reconstruction, all MIT-licensed with
first-class Python *and* C++ bindings, and nothing here requires touching CGAL or
vcglib/MeshLab at all.

## 3. Vetting fusion accuracy (not just "does it look fused")

A fused point cloud or mesh can look clean and still be quantitatively wrong — small
registration drift, scale error, or a biased ICP convergence don't show up to the eye at
object scale. It's worth borrowing the validation discipline from a related (not
directly parallel, but structurally similar) problem: measuring deviation between a
fabrication pipeline's output and its ground-truth reference.

**Beyer et al., "A Slicer-Independent Framework for Measuring G-Code Accuracy in
Medical 3D Printing"**, *Journal of Imaging* 12(1):25, 2026
([doi:10.3390/jimaging12010025](https://doi.org/10.3390/jimaging12010025)) is a useful
reference case, even though it's validating slicer G-code against an STL rather than
validating fused point clouds against a scan ground truth. Their approach, worth
mirroring here:

- **Unidirectional distance, not symmetric distance, when one side is ground truth.**
  They explicitly chose mean surface distance (MSD) and RMS from each reconstructed
  point to the *original* mesh surface — not a symmetric measure like Hausdorff distance
  — because one side of the comparison (the source STL) is the trusted reference, and a
  one-directional metric is more interpretable than a measure that treats both sides as
  equally uncertain. **Same logic applies to fusion validation**: when checking a fused
  scan against a known-good calibration object or a higher-fidelity reference scan,
  measure point→reference-surface distance, not a symmetric metric, and say clearly
  which side is assumed ground truth.
- **Report MSD, RMS, *and* min/max, not just one number.** A single mean can hide a
  pipeline that's accurate almost everywhere but badly wrong in one region (e.g., a
  poorly-registered partial scan, or a surface area with few ICP correspondences). Min/max
  alongside MSD/RMS makes localized failures visible in the summary numbers, not just in
  a color-mapped deviation visualization someone has to remember to look at.
- **Check normality before trusting summary statistics or comparing conditions.** Their
  RMS and min values were *not* normally distributed across repeated runs, which means
  mean ± standard deviation claims, or a t-test between two pipeline configurations,
  would have been misleading; they used distribution-appropriate statistics and reported
  effect sizes instead. Any A/B comparison of OpenScanner fusion settings (e.g. "does
  loop closure actually improve accuracy") should check this rather than assuming
  Gaussian error and reaching for a plain t-test.
- **Repeatability across conditions is its own failure mode, separate from mean
  accuracy.** They found one slicer could vary RMS by up to 0.002 mm depending purely on
  where the model sat in the build volume — same input geometry, different result, for a
  reason that has nothing to do with the geometry itself. The fusion-pipeline analogue:
  re-run the same scan sequence with different turntable start angles, different capture
  order, or different initial-pose seeding, and confirm the fused output doesn't move
  more than noise-floor between runs. A pipeline that's accurate on average but
  unstable run-to-run will still produce bad prints unpredictably.
- **Register/calibrate before measuring, and say how.** Their pipeline corrected
  XY/Z offset and ran registration before computing any deviation metric, with a
  calibration object to normalize coordinates across slicers. For OpenScanner, this
  means: define a fixed evaluation protocol (e.g. a reference calibration part scanned
  and fused the same way every time) so that accuracy numbers from different pipeline
  versions are actually comparable to each other, rather than each ad hoc test
  reinventing its own alignment.

Concretely, this argues for a small, scriptable evaluation harness early — not after the
fusion pipeline is "done" — built on Open3D (`compute_point_cloud_distance` /
`compute_point_to_point_distance` give the unidirectional point→reference distances
directly) plus a known reference object (a calibration phantom, or a high-resolution
reference scan of a test part) that every fusion pipeline change gets measured against
with the same MSD/RMS/min/max + non-parametric statistics protocol, not just "it looks
right in the viewer."

## Sources

- [A Survey of Deep Learning-based Point Cloud Denoising](https://arxiv.org/abs/2508.17011)
- [Deep Learning For Point Cloud Denoising: A Survey](https://arxiv.org/abs/2508.11932)
- Fleishman, Drori, Cohen-Or, *"Bilateral Mesh Denoising"*, SIGGRAPH 2003
- Öztireli, Guennebaud, Gross, *"Feature Preserving Point Set Surfaces based on Non-Linear Kernel Regression"* (RIMLS), Eurographics 2009
- Huang et al., *"Consolidation of Unorganized Point Clouds for Surface Reconstruction"* (WLOP), SIGGRAPH Asia 2009
- Newcombe et al., *"KinectFusion: Real-Time Dense Surface Mapping and Tracking"*, ISMAR 2011
- Nießner et al., *"Real-time 3D Reconstruction at Scale using Voxel Hashing"*, SIGGRAPH Asia 2013
- Whelan et al., *"ElasticFusion: Dense SLAM Without A Pose Graph"*, RSS 2015
- Kazhdan & Hoppe, *"Screened Poisson Surface Reconstruction"*, ACM TOG 2013
- Bernardini et al., *"The Ball-Pivoting Algorithm for Surface Reconstruction"*, IEEE TVCG 1999
- Beyer et al., *"A Slicer-Independent Framework for Measuring G-Code Accuracy in Medical 3D Printing"*, Journal of Imaging 12(1):25, 2026 ([doi:10.3390/jimaging12010025](https://doi.org/10.3390/jimaging12010025)) — referenced in §3 for its accuracy-validation methodology
