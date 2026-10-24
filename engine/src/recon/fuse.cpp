#include "recon/fuse.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

#include "recon/cloud_compare.h"  // NearestNeighborIndex

namespace openscanner::recon {
namespace {

// A voxel key on an absolute grid at the origin (no bounding-box offset). The
// bias keeps negative coordinates in a 21-bit field; voxel counts for a scan
// volume stay far inside that.
std::int64_t VoxelKey(const Point3& p, double voxel) {
    constexpr std::int64_t kBias = 1 << 20;
    const std::int64_t x = static_cast<int>(std::floor(p.x / voxel)) + kBias;
    const std::int64_t y = static_cast<int>(std::floor(p.y / voxel)) + kBias;
    const std::int64_t z = static_cast<int>(std::floor(p.z / voxel)) + kBias;
    return x | (y << 21) | (z << 42);
}

bool PointLess(const Point3& a, const Point3& b) {
    if (a.x != b.x) return a.x < b.x;
    if (a.y != b.y) return a.y < b.y;
    return a.z < b.z;
}

// One centroid per occupied voxel, in a deterministic order.
std::vector<Point3> VoxelReduce(const std::vector<Point3>& points, double voxel) {
    struct Accum { double sx = 0, sy = 0, sz = 0; int n = 0; };
    std::unordered_map<std::int64_t, Accum> cells;
    for (const Point3& p : points) {
        Accum& a = cells[VoxelKey(p, voxel)];
        a.sx += p.x; a.sy += p.y; a.sz += p.z; ++a.n;
    }
    std::vector<Point3> out;
    out.reserve(cells.size());
    for (const auto& kv : cells) {
        const Accum& a = kv.second;
        out.push_back({a.sx / a.n, a.sy / a.n, a.sz / a.n});
    }
    std::sort(out.begin(), out.end(), PointLess);
    return out;
}

}  // namespace

IncrementalFuser::IncrementalFuser(double voxel) : voxel_(voxel) {}

void IncrementalFuser::Add(const std::vector<Point3>& frame_points, const Pose& pose) {
    ++frames_;
    for (const Point3& p : frame_points) {
        const Point3 world = Apply(pose, p);
        if (voxel_ > 0.0) {
            VoxelAccum& a = voxels_[VoxelKey(world, voxel_)];
            a.sx += world.x; a.sy += world.y; a.sz += world.z; ++a.n;
        } else {
            points_.push_back(world);
        }
    }
}

std::vector<Point3> IncrementalFuser::Model() const {
    if (voxel_ <= 0.0) {
        std::vector<Point3> out = points_;
        std::sort(out.begin(), out.end(), PointLess);
        return out;
    }
    std::vector<Point3> out;
    out.reserve(voxels_.size());
    for (const auto& kv : voxels_) {
        const VoxelAccum& a = kv.second;
        out.push_back({a.sx / a.n, a.sy / a.n, a.sz / a.n});
    }
    std::sort(out.begin(), out.end(), PointLess);
    return out;
}

std::size_t IncrementalFuser::point_count() const {
    return voxel_ > 0.0 ? voxels_.size() : points_.size();
}

std::vector<Point3> FuseBatch(const std::vector<FrameCloud>& frames, double voxel) {
    std::vector<Point3> all;
    for (const FrameCloud& f : frames)
        for (const Point3& p : f.points) all.push_back(Apply(f.pose, p));
    if (voxel > 0.0) return VoxelReduce(all, voxel);
    std::sort(all.begin(), all.end(), PointLess);
    return all;
}

IcpResult IcpRefine(const std::vector<Point3>& source, const std::vector<Point3>& target,
                    const Pose& init, const IcpOptions& options) {
    IcpResult result;
    result.pose = init;
    if (source.size() < 3 || target.empty()) return result;

    const NearestNeighborIndex index(target, 0.0);  // target is fixed; build once
    const KabschEstimator kabsch;
    double prev_rms = std::numeric_limits<double>::infinity();

    for (int it = 1; it <= options.max_iterations; ++it) {
        std::vector<Correspondence> pairs;
        pairs.reserve(source.size());
        for (const Point3& p : source) {
            const Point3 q = Apply(result.pose, p);
            double dist = 0.0;
            int idx = -1;
            if (!index.Nearest(q, &dist, &idx)) continue;
            if (options.max_pair_distance > 0.0 && dist > options.max_pair_distance) continue;
            pairs.push_back({p, target[static_cast<std::size_t>(idx)], 1.0});
        }
        if (pairs.size() < 3) return result;  // not enough overlap to solve
        const PoseEstimate est = kabsch.Estimate(pairs);
        if (!est.ok) return result;

        result.pose = est.pose;
        result.rms = est.rms_residual;
        result.iterations = it;
        result.ok = true;
        if (std::fabs(prev_rms - est.rms_residual) < options.tol) break;
        prev_rms = est.rms_residual;
    }
    return result;
}

double PairwiseConsistencyRms(const std::vector<FrameCloud>& frames, double tau) {
    if (frames.size() < 2 || !(tau > 0.0)) return 0.0;
    double sum_sq = 0.0;
    std::size_t matched = 0;

    std::vector<Point3> a;  // frame i in the model frame
    for (std::size_t i = 0; i + 1 < frames.size(); ++i) {
        a.clear();
        a.reserve(frames[i].points.size());
        for (const Point3& p : frames[i].points) a.push_back(Apply(frames[i].pose, p));
        const NearestNeighborIndex index_a(a, tau);
        if (index_a.empty()) continue;
        for (const Point3& p : frames[i + 1].points) {
            const Point3 b = Apply(frames[i + 1].pose, p);
            double dist = 0.0;
            if (!index_a.Nearest(b, &dist, nullptr)) continue;
            if (dist <= tau) {  // overlapping region only
                sum_sq += dist * dist;
                ++matched;
            }
        }
    }
    return matched ? std::sqrt(sum_sq / static_cast<double>(matched)) : 0.0;
}

}  // namespace openscanner::recon
