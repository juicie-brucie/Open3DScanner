#pragma once
// Per-frame pose estimation (WO-A1, Phase C "track").
//
// A frame's pose is a rigid transform placing that frame's triangulated points
// into a common model frame. This first stage estimates it from 3-D point
// correspondences (shared markers/features across frames) with the closed-form
// least-squares rigid solve (Kabsch/Umeyama): the optimal R,t for 3-D->3-D
// correspondences under Gaussian noise, via a 3x3 SVD.
//
// Design for "adaptability" — two seams keep the pipeline open to change:
//   * POSE SOURCE — fusion consumes a Pose, whoever produced it, so a pose from
//     an upstream source (an encoder, the operator) can be fed straight to
//     fusion, bypassing the estimator below.
//   * ESTIMATOR — the solve is a strategy (PoseEstimator), so a robust/RANSAC
//     variant, or a host-side Ceres global bundle-adjustment refine, can be
//     swapped in later without changing any consumer.
//
// Dependency decision, for the same "adaptability": the initial solver is
// closed-form and zero-dependency (no Eigen, no Ceres) — a tiny, fixed-size,
// allocation-free 3x3 kernel. That keeps it accurate and efficient now and easy
// to relocate or replace; global bundle adjustment (which would justify a
// heavier solver) is a deliberate follow-on behind the estimator seam, not a
// dependency taken on up front. Clean-room and device-free throughout.

#include <vector>

#include "recon/triangulate.h"  // Point3

namespace openscanner::recon {

// A rigid transform: a model-frame point is R * p + t, with R row-major 3x3.
// The default is the identity.
struct Pose {
    double r[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    double t[3] = {0, 0, 0};
};

// p transformed by the pose.
Point3 Apply(const Pose& pose, const Point3& p);

// The pose that applies `b` then `a`: Apply(Compose(a, b), p) == Apply(a, Apply(b, p)).
Pose Compose(const Pose& a, const Pose& b);

// The inverse transform.
Pose Invert(const Pose& pose);

// One 3-D point correspondence: a source point and where it should land, with
// an optional weight (0 drops the pair).
struct Correspondence {
    Point3 from;
    Point3 to;
    double weight = 1.0;
};

struct PoseEstimate {
    Pose pose;
    bool ok = false;          // false: fewer than 3 usable pairs, or degenerate
    double rms_residual = 0;  // RMS of |R*from + t - to| over the used pairs
    int used = 0;             // pairs with positive weight
};

// The estimator seam. The algorithm is a strategy so it can be swapped (a
// robust/RANSAC variant, a Ceres-backed global refine) without touching the
// fusion stage that consumes a Pose.
class PoseEstimator {
public:
    virtual ~PoseEstimator() = default;
    virtual PoseEstimate Estimate(const std::vector<Correspondence>& pairs) const = 0;
    virtual const char* name() const = 0;
};

// The closed-form least-squares rigid solve (Kabsch/Umeyama) via a 3x3 SVD:
// the optimal R,t under Gaussian noise, O(n) with a tiny fixed-size
// decomposition. Needs >= 3 non-collinear pairs; collinear or coincident
// inputs are reported as not-ok. The returned R is always a proper rotation
// (reflections are corrected), so a planar correspondence set is fine.
class KabschEstimator : public PoseEstimator {
public:
    PoseEstimate Estimate(const std::vector<Correspondence>& pairs) const override;
    const char* name() const override { return "kabsch-svd"; }
};

}  // namespace openscanner::recon
