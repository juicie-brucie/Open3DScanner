#include "recon/cloud_compare.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace openscanner::recon {
namespace {

// A uniform voxel grid over a point set, searched in expanding Chebyshev
// shells. Cell indices can be negative (a query point may sit outside the
// set's bounding box), so cells are kept in a hash map keyed by the integer
// cell coordinates rather than a dense array — memory is proportional to the
// occupied cells, not the bounding volume.
struct CellKey {
    int x, y, z;
    bool operator==(const CellKey& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct CellHash {
    std::size_t operator()(const CellKey& k) const {
        // A simple 3-way mix; the coordinates are small integers.
        std::uint64_t h = static_cast<std::uint32_t>(k.x);
        h = h * 0x9E3779B97F4A7C15ull + static_cast<std::uint32_t>(k.y);
        h = h * 0x9E3779B97F4A7C15ull + static_cast<std::uint32_t>(k.z);
        return static_cast<std::size_t>(h ^ (h >> 29));
    }
};

class Grid {
public:
    Grid(const std::vector<Point3>& points, double cell) : points_(points), cell_(cell) {
        double min_x = std::numeric_limits<double>::infinity(), min_y = min_x, min_z = min_x;
        double max_x = -min_x, max_y = -min_x, max_z = -min_x;
        for (const Point3& p : points) {
            min_x = std::min(min_x, p.x);
            min_y = std::min(min_y, p.y);
            min_z = std::min(min_z, p.z);
            max_x = std::max(max_x, p.x);
            max_y = std::max(max_y, p.y);
            max_z = std::max(max_z, p.z);
        }
        ox_ = min_x;
        oy_ = min_y;
        oz_ = min_z;
        // The ring count that spans the whole set: beyond it every cell is seen.
        const double span = std::max({max_x - min_x, max_y - min_y, max_z - min_z, 0.0});
        max_ring_ = static_cast<int>(span / cell_) + 2;
        for (int i = 0; i < static_cast<int>(points.size()); ++i) {
            cells_[CellOf(points[i])].push_back(i);
        }
    }

    // The squared distance to the nearest point, or false when the set is empty.
    bool NearestSquared(const Point3& q, double* out_sq) const {
        if (points_.empty()) return false;
        const CellKey c = CellOf(q);
        double best = std::numeric_limits<double>::infinity();
        for (int r = 0; r <= max_ring_; ++r) {
            // The closest a ring-r cell can lie to q is (r-1)*cell (q sits in
            // its own cell, ring 0); once that exceeds the best, stop.
            if (r >= 1) {
                const double floor_dist = static_cast<double>(r - 1) * cell_;
                if (floor_dist * floor_dist >= best) break;
            }
            ForEachInShell(c, r, [&](int idx) {
                best = std::min(best, DistSq(q, points_[idx]));
            });
        }
        *out_sq = best;
        return best < std::numeric_limits<double>::infinity();
    }

    // Is there any point within `radius` of q?
    bool AnyWithin(const Point3& q, double radius) const {
        if (points_.empty()) return false;
        const CellKey c = CellOf(q);
        const double radius_sq = radius * radius;
        const int rmax = static_cast<int>(radius / cell_) + 2;
        bool found = false;
        for (int r = 0; r <= rmax && !found; ++r) {
            if (r >= 1 && static_cast<double>(r - 1) * cell_ > radius) break;
            ForEachInShell(c, r, [&](int idx) {
                if (!found && DistSq(q, points_[idx]) <= radius_sq) found = true;
            });
        }
        return found;
    }

private:
    CellKey CellOf(const Point3& p) const {
        return CellKey{static_cast<int>(std::floor((p.x - ox_) / cell_)),
                       static_cast<int>(std::floor((p.y - oy_) / cell_)),
                       static_cast<int>(std::floor((p.z - oz_) / cell_))};
    }

    static double DistSq(const Point3& a, const Point3& b) {
        const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    // Visit every stored point in the cells at Chebyshev ring exactly `r`.
    template <typename Fn>
    void ForEachInShell(const CellKey& c, int r, Fn&& fn) const {
        for (int dx = -r; dx <= r; ++dx) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dz = -r; dz <= r; ++dz) {
                    if (std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) != r) continue;
                    auto it = cells_.find(CellKey{c.x + dx, c.y + dy, c.z + dz});
                    if (it == cells_.end()) continue;
                    for (int idx : it->second) fn(idx);
                }
            }
        }
    }

    const std::vector<Point3>& points_;
    double cell_;
    double ox_ = 0.0, oy_ = 0.0, oz_ = 0.0;
    int max_ring_ = 1;
    std::unordered_map<CellKey, std::vector<int>, CellHash> cells_;
};

// A grid cell sized so the whole-set ring count stays bounded even when tau is
// tiny next to the cloud's extent. Exactness never depends on the cell size —
// it comes from the expanding shells and the real distance tests — so a larger
// cell only trades a little speed, never correctness.
double ChooseCell(const std::vector<Point3>& b, double tau) {
    double min_x = std::numeric_limits<double>::infinity(), min_y = min_x, min_z = min_x;
    double max_x = -min_x, max_y = -min_x, max_z = -min_x;
    for (const Point3& p : b) {
        min_x = std::min(min_x, p.x);
        min_y = std::min(min_y, p.y);
        min_z = std::min(min_z, p.z);
        max_x = std::max(max_x, p.x);
        max_y = std::max(max_y, p.y);
        max_z = std::max(max_z, p.z);
    }
    const double span = std::max({max_x - min_x, max_y - min_y, max_z - min_z, 0.0});
    // Keep the per-axis ring count under this, so a tiny tau cannot make the
    // shell walk span millions of empty rings.
    constexpr double kMaxRingsPerAxis = 2048.0;
    const double min_cell = span / kMaxRingsPerAxis;
    return std::max(tau, std::max(min_cell, 1e-12));
}

}  // namespace

std::vector<Point3> Positions(const std::vector<CloudPoint>& cloud) {
    std::vector<Point3> out;
    out.reserve(cloud.size());
    for (const CloudPoint& p : cloud) out.push_back(p.position);
    return out;
}

CloudMetrics CompareClouds(const std::vector<Point3>& a, const std::vector<Point3>& b, double tau) {
    CloudMetrics m;
    m.count_a = a.size();
    m.count_b = b.size();
    if (a.empty() || b.empty() || !(tau > 0.0)) return m;

    // A -> B nearest-neighbour statistics.
    const Grid grid_b(b, ChooseCell(b, tau));
    double sum = 0.0, sum_sq = 0.0, worst = 0.0;
    for (const Point3& p : a) {
        double nn_sq = 0.0;
        if (!grid_b.NearestSquared(p, &nn_sq)) continue;
        const double nn = std::sqrt(nn_sq);
        sum += nn;
        sum_sq += nn_sq;
        worst = std::max(worst, nn);
    }
    const double n = static_cast<double>(a.size());
    m.mean_nn = sum / n;
    m.rms_nn = std::sqrt(sum_sq / n);
    m.max_nn = worst;

    // Coverage: the fraction of B within tau of some A point.
    const Grid grid_a(a, ChooseCell(a, tau));
    std::size_t covered = 0;
    for (const Point3& p : b) {
        if (grid_a.AnyWithin(p, tau)) ++covered;
    }
    m.coverage = static_cast<double>(covered) / static_cast<double>(b.size());

    m.valid = true;
    return m;
}

}  // namespace openscanner::recon
