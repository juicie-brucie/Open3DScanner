#pragma once
// Per-frame pose estimation (WO-A1, Phase C "track").
//
// A frame's pose is a rigid transform placing that frame's triangulated points
// into a common model frame. This first stage estimates it from 3-D point
// correspondences (shared markers/features across frames) by solving the
// least-squares absolute-orientation problem (the Kabsch/Umeyama problem): the
// optimal R,t for 3-D->3-D correspondences under Gaussian noise. It is solved
// here by Horn's unit-quaternion method (the top eigenvector of a 4x4 matrix
// built from the cross-covariance) rather than the SVD route — the quaternion
// is a proper rotation by construction, so there is no reflection case to
// detect and fix up.
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

// The closed-form least-squares rigid solve (the Kabsch/Umeyama problem),
// implemented by Horn's unit-quaternion method: the optimal R,t under Gaussian
// noise, O(n) to form the cross-covariance plus a fixed 4x4 eigensolve. Needs
// >= 3 non-collinear pairs; collinear or coincident inputs are reported as
// not-ok. The returned R is a proper rotation by construction (a unit
// quaternion has no reflection case), so a planar correspondence set is fine.
// (The class is named for the problem; the quaternion route avoids the
// det-sign fix-up that an SVD solve needs.)
class KabschEstimator : public PoseEstimator {
public:
    PoseEstimate Estimate(const std::vector<Correspondence>& pairs) const override;
    const char* name() const override { return "kabsch-svd"; }
};

}  // namespace openscanner::recon
