#pragma once
// Fuse per-frame triangulated clouds into one accumulated model (WO-A2, Phase
// C "fuse"). Each frame's cloud is placed in the model frame by its Pose (from
// the "track" stage, pose.h, or an external source) and merged.
//
// The fuse runs in two tiers, mirroring the pipeline's live/post-scan split:
//   * LIVE / incremental — IncrementalFuser: feed one posed frame at a time and
//     keep a voxel-bounded running model, cheap enough to sit ahead of the live
//     view, with memory bounded by the scanned volume, not the frame count.
//   * POST-SCAN / batch — FuseBatch: place every frame by its pose and merge at
//     the chosen resolution.
// Global bundle-adjustment refinement of the poses is the deferred host-side
// follow-on behind pose.h's estimator seam; this stage applies the poses it is
// given.
//
// IcpRefine is the cloud-based pose refinement for when the correspondence
// solve needs tightening against the accumulated geometry; it composes the
// Kabsch estimator (pose.h) with the shared nearest-neighbour index
// (cloud_compare.h) rather than carrying its own. Clean-room, device-free,
// zero-dependency — the recon stack's "adaptability" posture.

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "recon/pose.h"
#include "recon/triangulate.h"  // Point3

namespace openscanner::recon {

// A per-frame cloud and the pose that places it in the model frame.
struct FrameCloud {
    std::vector<Point3> points;
    Pose pose;
};

// Incremental (live-tier) accumulation into a voxel-bounded model: each
// occupied voxel keeps the running centroid of the points that fell in it, so
// the model's size is bounded by the scanned volume, not the frame count.
class IncrementalFuser {
public:
    // `voxel` is the bin size; <= 0 keeps every point (unbounded).
    explicit IncrementalFuser(double voxel);

    // Add one frame's points, placed by `pose`.
    void Add(const std::vector<Point3>& frame_points, const Pose& pose);

    // The model so far — one point per occupied voxel (its centroid), or every
    // added point when voxel <= 0. Returned in a deterministic order.
    std::vector<Point3> Model() const;

    std::size_t frame_count() const { return frames_; }
    std::size_t point_count() const;  // model points (occupied voxels, or all points)

private:
    struct VoxelAccum {
        double sx = 0, sy = 0, sz = 0;
        int n = 0;
    };
    double voxel_;
    std::size_t frames_ = 0;
    std::vector<Point3> points_;                         // voxel_ <= 0
    std::unordered_map<std::int64_t, VoxelAccum> voxels_;  // voxel_ > 0
};

// Batch (post-scan-tier) fuse: place every frame by its pose and merge,
// optionally voxel-downsampled (voxel <= 0 keeps every point). Deterministic
// order.
std::vector<Point3> FuseBatch(const std::vector<FrameCloud>& frames, double voxel);

// Point-to-point ICP: refine `init` so `source` best aligns onto `target`.
// Each iteration pairs every source point (under the current pose) with its
// nearest target point and re-solves the rigid pose (Kabsch). It converges to a
// local optimum, so `init` should be a reasonable guess (the track pose).
struct IcpOptions {
    int max_iterations = 50;
    double tol = 1e-7;               // stop when the RMS improves by less than this
    double max_pair_distance = 0.0;  // reject correspondences beyond this; 0 = no limit
};
struct IcpResult {
    Pose pose;
    int iterations = 0;
    double rms = 0.0;  // RMS of the accepted correspondences at the final pose
    bool ok = false;   // false: empty input, or too few correspondences to solve
};
IcpResult IcpRefine(const std::vector<Point3>& source, const std::vector<Point3>& target,
                    const Pose& init, const IcpOptions& options = {});

// Fused-model consistency: the RMS, over consecutive frame pairs, of the
// nearest-neighbour distance between their points in the model frame, counting
// only matches within `tau` (the overlapping region). Lower is a more
// consistent fuse; it grows with pose drift. Returns 0 when nothing overlaps.
double PairwiseConsistencyRms(const std::vector<FrameCloud>& frames, double tau);

}  // namespace openscanner::recon
