#pragma once
// A/B compare two point clouds (Phase C, WO-2): our cloud against a reference
// (the vendor's cloud for the same scene). This is the first stage that scores
// "own the math" against the vendor's own output.
//
// Metrics: the two point counts, the RMS / mean / max nearest-neighbour
// distance from A to B, and coverage — the fraction of B that has an A point
// within `tau`. A uniform voxel grid over B, searched in expanding shells,
// makes each nearest-neighbour query fast and exact, so a small cloud of ours
// can be scored against a vendor cloud of millions of points.
//
// Pure: no I/O, deterministic. The reference's provenance and the paired
// recording are the caller's problem; this only scores two clouds in whatever
// common frame and units the caller has already put them in.

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "recon/triangulate.h"

namespace openscanner::recon {

// A uniform-voxel nearest-neighbour index over a point set, searched in
// expanding Chebyshev shells: exact, and near-O(1) per query for a locally
// uniform cloud, so it scales to large (vendor-sized) clouds. One
// implementation, shared by the A/B compare here and by the fusion/ICP stage
// (pose.h's seam) — kept reusable in keeping with the recon stack's
// "adaptability" goal.
//
// It holds a reference to the points; they must outlive the index.
class NearestNeighborIndex {
public:
    // `cell` is the voxel size; pass <= 0 to auto-size it from the point
    // spread. Exactness never depends on the cell size (the shells plus the
    // real distance tests give that) — a larger cell only trades speed.
    NearestNeighborIndex(const std::vector<Point3>& points, double cell);

    bool empty() const { return points_.empty(); }

    // The nearest stored point to q: its distance in *dist, its index in
    // *index. False when the set is empty.
    bool Nearest(const Point3& q, double* dist, int* index) const;

    // Whether any stored point lies within `radius` of q.
    bool AnyWithin(const Point3& q, double radius) const;

    // Every stored point within `radius` of q, as indices into the point set
    // (appended to *out, which is cleared first). Used for local
    // neighbourhoods, e.g. PCA normal estimation.
    void Within(const Point3& q, double radius, std::vector<int>* out) const;

private:
    std::int64_t CellKeyOf(const Point3& p) const;

    const std::vector<Point3>& points_;
    double cell_ = 1.0;
    double ox_ = 0.0, oy_ = 0.0, oz_ = 0.0;
    int max_ring_ = 1;
    std::unordered_map<std::int64_t, std::vector<int>> cells_;
};

struct CloudMetrics {
    std::size_t count_a = 0;
    std::size_t count_b = 0;
    double rms_nn = 0.0;   // RMS over A of the distance to the nearest B point
    double mean_nn = 0.0;  // mean over A of that distance
    double max_nn = 0.0;   // worst over A
    double coverage = 0.0;  // fraction of B within tau of some A point, 0..1
    bool valid = false;     // false when either cloud is empty, or tau <= 0
};

// Score cloud A (ours) against reference cloud B. `tau` is the coverage radius
// in the clouds' own units and must be > 0. Either cloud empty, or tau <= 0,
// gives valid = false.
CloudMetrics CompareClouds(const std::vector<Point3>& a, const std::vector<Point3>& b, double tau);

// Pull the positions out of a triangulated cloud, for CompareClouds.
std::vector<Point3> Positions(const std::vector<CloudPoint>& cloud);

}  // namespace openscanner::recon
