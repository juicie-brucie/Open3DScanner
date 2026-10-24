#include "recon/cloud_compare.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace openscanner::recon {
namespace {

// Pack an integer cell coordinate triple into one key. Cell indices can be
// negative (a query may sit outside the set's bounding box), so each axis is
// biased into a 21-bit field. The cell counts are bounded (the auto-size and
// ChooseCell both cap the per-axis span), so the fields never overflow.
std::int64_t PackCell(int ix, int iy, int iz) {
    constexpr std::int64_t kBias = 1 << 20;  // range [-2^20, 2^20) per axis
    const std::int64_t x = ix + kBias, y = iy + kBias, z = iz + kBias;
    return x | (y << 21) | (z << 42);
}

double DistSq(const Point3& a, const Point3& b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

void Bounds(const std::vector<Point3>& pts, double* mn, double* mx) {
    for (int i = 0; i < 3; ++i) {
        mn[i] = std::numeric_limits<double>::infinity();
        mx[i] = -mn[i];
    }
    for (const Point3& p : pts) {
        const double v[3] = {p.x, p.y, p.z};
        for (int i = 0; i < 3; ++i) {
            mn[i] = std::min(mn[i], v[i]);
            mx[i] = std::max(mx[i], v[i]);
        }
    }
}

// A cell sized to the cloud's density — about one point per cell on average —
// so a nearest-neighbour query touches O(1) cells whether the cloud is dense or
// sparse. (A span/constant cell would leave a sparse cloud with mostly-empty
// cells and a shell walk that crosses hundreds of empty rings to reach a
// neighbour.) Exactness never depends on the cell size; it only trades speed.
double AutoCell(const std::vector<Point3>& pts) {
    if (pts.empty()) return 1.0;
    double mn[3], mx[3];
    Bounds(pts, mn, mx);
    const double span = std::max({mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2], 0.0});
    if (span <= 0.0) return 1.0;  // all points coincide
    const double per_axis = std::cbrt(static_cast<double>(pts.size()));
    return std::max(span / std::max(1.0, per_axis), 1e-12);
}

// The cell CompareClouds uses: tied to tau, but never so small relative to the
// cloud's extent that the shell walk spans millions of empty rings. Exactness
// is independent of this — only speed.
double ChooseCell(const std::vector<Point3>& pts, double tau) {
    return std::max(tau, AutoCell(pts));
}

}  // namespace

NearestNeighborIndex::NearestNeighborIndex(const std::vector<Point3>& points, double cell)
    : points_(points), cell_(cell > 0.0 ? cell : AutoCell(points)) {
    double mn[3], mx[3];
    Bounds(points, mn, mx);
    ox_ = std::isfinite(mn[0]) ? mn[0] : 0.0;
    oy_ = std::isfinite(mn[1]) ? mn[1] : 0.0;
    oz_ = std::isfinite(mn[2]) ? mn[2] : 0.0;
    const double span = points.empty() ? 0.0 : std::max({mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2], 0.0});
    max_ring_ = static_cast<int>(span / cell_) + 2;
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        cells_[CellKeyOf(points[i])].push_back(i);
    }
}

std::int64_t NearestNeighborIndex::CellKeyOf(const Point3& p) const {
    return PackCell(static_cast<int>(std::floor((p.x - ox_) / cell_)),
                    static_cast<int>(std::floor((p.y - oy_) / cell_)),
                    static_cast<int>(std::floor((p.z - oz_) / cell_)));
}

bool NearestNeighborIndex::Nearest(const Point3& q, double* dist, int* index) const {
    if (points_.empty()) return false;
    const int cx = static_cast<int>(std::floor((q.x - ox_) / cell_));
    const int cy = static_cast<int>(std::floor((q.y - oy_) / cell_));
    const int cz = static_cast<int>(std::floor((q.z - oz_) / cell_));
    double best = std::numeric_limits<double>::infinity();
    int best_idx = -1;
    for (int r = 0; r <= max_ring_; ++r) {
        // The closest a ring-r cell can lie to q is (r-1)*cell; once that
        // exceeds the best, no further shell can improve it.
        if (r >= 1) {
            const double floor_dist = static_cast<double>(r - 1) * cell_;
            if (floor_dist * floor_dist >= best) break;
        }
        for (int dx = -r; dx <= r; ++dx)
            for (int dy = -r; dy <= r; ++dy)
                for (int dz = -r; dz <= r; ++dz) {
                    if (std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) != r) continue;
                    const auto it = cells_.find(PackCell(cx + dx, cy + dy, cz + dz));
                    if (it == cells_.end()) continue;
                    for (int idx : it->second) {
                        const double d = DistSq(q, points_[idx]);
                        if (d < best) { best = d; best_idx = idx; }
                    }
                }
    }
    if (best_idx < 0) return false;
    if (dist) *dist = std::sqrt(best);
    if (index) *index = best_idx;
    return true;
}

bool NearestNeighborIndex::AnyWithin(const Point3& q, double radius) const {
    if (points_.empty()) return false;
    const int cx = static_cast<int>(std::floor((q.x - ox_) / cell_));
    const int cy = static_cast<int>(std::floor((q.y - oy_) / cell_));
    const int cz = static_cast<int>(std::floor((q.z - oz_) / cell_));
    const double radius_sq = radius * radius;
    const int rmax = static_cast<int>(radius / cell_) + 2;
    for (int r = 0; r <= rmax; ++r) {
        if (r >= 1 && static_cast<double>(r - 1) * cell_ > radius) break;
        for (int dx = -r; dx <= r; ++dx)
            for (int dy = -r; dy <= r; ++dy)
                for (int dz = -r; dz <= r; ++dz) {
                    if (std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) != r) continue;
                    const auto it = cells_.find(PackCell(cx + dx, cy + dy, cz + dz));
                    if (it == cells_.end()) continue;
                    for (int idx : it->second)
                        if (DistSq(q, points_[idx]) <= radius_sq) return true;
                }
    }
    return false;
}

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
    const NearestNeighborIndex index_b(b, ChooseCell(b, tau));
    double sum = 0.0, sum_sq = 0.0, worst = 0.0;
    for (const Point3& p : a) {
        double nn = 0.0;
        if (!index_b.Nearest(p, &nn, nullptr)) continue;
        sum += nn;
        sum_sq += nn * nn;
        worst = std::max(worst, nn);
    }
    const double n = static_cast<double>(a.size());
    m.mean_nn = sum / n;
    m.rms_nn = std::sqrt(sum_sq / n);
    m.max_nn = worst;

    // Coverage: the fraction of B within tau of some A point.
    const NearestNeighborIndex index_a(a, ChooseCell(a, tau));
    std::size_t covered = 0;
    for (const Point3& p : b) {
        if (index_a.AnyWithin(p, tau)) ++covered;
    }
    m.coverage = static_cast<double>(covered) / static_cast<double>(b.size());

    m.valid = true;
    return m;
}

}  // namespace openscanner::recon
