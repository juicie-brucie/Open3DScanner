// The confidence/retention experiment's core (row #159).
//
// See harness.h for the contract and
// docs/recomp/log/2026-10-20-row-159-confidence-experiment.md for the
// experiment this implements.

#include "harness.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numeric>

namespace openscanner {
namespace confidence {
namespace {

// ------------------------------------------------------------------ rng ----

// SplitMix64. The control's draws must repeat exactly, so the generator is
// pinned and seeded from the command line.
class Rng {
public:
    explicit Rng(uint64_t seed) : state_(seed) {}
    uint64_t next() {
        uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    // [0, 1)
    double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
    // [0, bound)
    std::size_t below(std::size_t bound) {
        if (bound == 0) return 0;
        return static_cast<std::size_t>(next() % bound);
    }
    double normal() {
        double u1 = uniform();
        if (u1 < 1e-300) u1 = 1e-300;
        const double u2 = uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }

private:
    uint64_t state_;
};

// ----------------------------------------------------------------- grid ----

// A uniform grid over the cloud, held as a cell-start table plus the point
// indices ordered by cell. One level only: the estimator needs a handful of
// exact nearest neighbours, not a k-d tree.
struct Grid {
    double cell = 1.0;
    double ox = 0.0, oy = 0.0, oz = 0.0;
    int nx = 1, ny = 1, nz = 1;
    std::vector<int> start;  // nx*ny*nz + 1 entries
    std::vector<int> items;  // point indices, grouped by cell

    [[nodiscard]] int index(int ix, int iy, int iz) const { return (iz * ny + iy) * nx + ix; }
};

Grid build_grid(const Cloud& cloud, double cell, std::size_t max_cells) {
    Grid g;
    if (cloud.size() == 0) return g;
    double minx = cloud.xyz[0], maxx = cloud.xyz[0];
    double miny = cloud.xyz[1], maxy = cloud.xyz[1];
    double minz = cloud.xyz[2], maxz = cloud.xyz[2];
    for (std::size_t i = 1; i < cloud.size(); ++i) {
        const float* p = cloud.point(i);
        minx = std::min(minx, static_cast<double>(p[0]));
        maxx = std::max(maxx, static_cast<double>(p[0]));
        miny = std::min(miny, static_cast<double>(p[1]));
        maxy = std::max(maxy, static_cast<double>(p[1]));
        minz = std::min(minz, static_cast<double>(p[2]));
        maxz = std::max(maxz, static_cast<double>(p[2]));
    }
    cell = std::max(cell, 1e-9);
    // Grow the cell until the grid fits the budget: a thin sheet or a big
    // cloud would otherwise ask for an enormous number of empty cells.
    for (int guard = 0; guard < 64; ++guard) {
        g.nx = std::max(1, static_cast<int>(std::ceil((maxx - minx) / cell)) + 1);
        g.ny = std::max(1, static_cast<int>(std::ceil((maxy - miny) / cell)) + 1);
        g.nz = std::max(1, static_cast<int>(std::ceil((maxz - minz) / cell)) + 1);
        const double cells = static_cast<double>(g.nx) * g.ny * g.nz;
        if (cells <= static_cast<double>(max_cells)) break;
        cell *= std::cbrt(cells / static_cast<double>(max_cells)) * 1.000001;
    }
    g.cell = cell;
    g.ox = minx;
    g.oy = miny;
    g.oz = minz;
    const std::size_t cell_count = static_cast<std::size_t>(g.nx) * g.ny * g.nz;
    std::vector<int> counts(cell_count + 1, 0);
    auto cell_of = [&](std::size_t i, int* ix, int* iy, int* iz) {
        const float* p = cloud.point(i);
        *ix = std::min(g.nx - 1, std::max(0, static_cast<int>((p[0] - g.ox) / g.cell)));
        *iy = std::min(g.ny - 1, std::max(0, static_cast<int>((p[1] - g.oy) / g.cell)));
        *iz = std::min(g.nz - 1, std::max(0, static_cast<int>((p[2] - g.oz) / g.cell)));
    };
    for (std::size_t i = 0; i < cloud.size(); ++i) {
        int ix, iy, iz;
        cell_of(i, &ix, &iy, &iz);
        ++counts[g.index(ix, iy, iz) + 1];
    }
    for (std::size_t c = 0; c < cell_count; ++c) counts[c + 1] += counts[c];
    g.start = counts;
    g.items.assign(cloud.size(), 0);
    std::vector<int> cursor(g.start.begin(), g.start.end() - 1);
    for (std::size_t i = 0; i < cloud.size(); ++i) {
        int ix, iy, iz;
        cell_of(i, &ix, &iy, &iz);
        g.items[cursor[g.index(ix, iy, iz)]++] = static_cast<int>(i);
    }
    return g;
}

// The cells at Chebyshev distance exactly r from (ix,iy,iz): six faces, twelve
// edges and eight corners. Enumerating the ring instead of the whole box keeps
// the search cheap when the neighbourhood has to grow.
template <typename F>
void for_each_ring_cell(const Grid& g, int ix, int iy, int iz, int r, F&& visit) {
    auto emit = [&](int cx, int cy, int cz) {
        if (cx < 0 || cy < 0 || cz < 0 || cx >= g.nx || cy >= g.ny || cz >= g.nz) return;
        visit(cx, cy, cz);
    };
    if (r == 0) {
        emit(ix, iy, iz);
        return;
    }
    // Faces: one coordinate at distance r, the other two strictly inside.
    for (int axis = 0; axis < 3; ++axis) {
        const int inner_a = (axis + 1) % 3;
        const int inner_b = (axis + 2) % 3;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            for (int a = -(r - 1); a <= (r - 1); ++a) {
                for (int b = -(r - 1); b <= (r - 1); ++b) {
                    int coords[3] = {ix, iy, iz};
                    coords[inner_a] += a;
                    coords[inner_b] += b;
                    coords[axis] += sgn * r;
                    emit(coords[0], coords[1], coords[2]);
                }
            }
        }
    }
    // Edges: two coordinates at distance r, the third strictly inside.
    for (int a = 0; a < 3; ++a) {
        for (int b = a + 1; b < 3; ++b) {
            const int free_axis = 3 - a - b;
            for (int sa = -1; sa <= 1; sa += 2) {
                for (int sb = -1; sb <= 1; sb += 2) {
                    for (int c = -(r - 1); c <= (r - 1); ++c) {
                        int coords[3] = {ix, iy, iz};
                        coords[a] += sa * r;
                        coords[b] += sb * r;
                        coords[free_axis] += c;
                        emit(coords[0], coords[1], coords[2]);
                    }
                }
            }
        }
    }
    // Corners: all three at distance r.
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            for (int sz = -1; sz <= 1; sz += 2) {
                emit(ix + sx * r, iy + sy * r, iz + sz * r);
            }
        }
    }
}

// The up-to-k nearest neighbours of one point, kept sorted by distance.
struct Neighbours {
    int k = 0;
    std::vector<double> d2;
    std::vector<int> index;
    int count = 0;

    void insert(double distance2, int idx) {
        if (count < k) {
            int p = count++;
            d2[p] = distance2;
            index[p] = idx;
            while (p > 0 && d2[p - 1] > d2[p]) {
                std::swap(d2[p - 1], d2[p]);
                std::swap(index[p - 1], index[p]);
                --p;
            }
        } else if (distance2 < d2[k - 1]) {
            d2[k - 1] = distance2;
            index[k - 1] = idx;
            int p = k - 1;
            while (p > 0 && d2[p - 1] > d2[p]) {
                std::swap(d2[p - 1], d2[p]);
                std::swap(index[p - 1], index[p]);
                --p;
            }
        }
    }
};

void collect_neighbours(const Grid& g, const Cloud& cloud, std::size_t i, Neighbours* out, double max_radius) {
    const float* p = cloud.point(i);
    int ix = std::min(g.nx - 1, std::max(0, static_cast<int>((p[0] - g.ox) / g.cell)));
    int iy = std::min(g.ny - 1, std::max(0, static_cast<int>((p[1] - g.oy) / g.cell)));
    int iz = std::min(g.nz - 1, std::max(0, static_cast<int>((p[2] - g.oz) / g.cell)));
    const int max_ring = static_cast<int>(std::ceil(max_radius / g.cell));
    for (int r = 0; r <= max_ring; ++r) {
        for_each_ring_cell(g, ix, iy, iz, r, [&](int cx, int cy, int cz) {
            const int cell = g.index(cx, cy, cz);
            for (int s = g.start[cell]; s < g.start[cell + 1]; ++s) {
                const int j = g.items[s];
                const float* q = cloud.point(static_cast<std::size_t>(j));
                const double dx = static_cast<double>(q[0]) - p[0];
                const double dy = static_cast<double>(q[1]) - p[1];
                const double dz = static_cast<double>(q[2]) - p[2];
                out->insert(dx * dx + dy * dy + dz * dz, j);
            }
        });
        if (out->count == out->k) {
            // Every cell in the next ring is at least r * cell away, so once
            // the worst kept neighbour is closer than that, nothing outside
            // the searched rings can be nearer.
            const double worst = std::sqrt(out->d2[out->k - 1]);
            if (r * g.cell > worst) break;
        }
    }
}

// The eigenvalues and eigenvectors of a symmetric 3x3 matrix (cyclic Jacobi).
// The eigenvector of the smallest eigenvalue is the local plane's normal.
void eigen_sym3(const double in[3][3], double eval[3], double evec[3][3]) {
    double a[3][3];
    double v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) a[i][j] = in[i][j];
    for (int sweep = 0; sweep < 64; ++sweep) {
        double off = 0.0;
        for (int p = 0; p < 3; ++p)
            for (int q = p + 1; q < 3; ++q) off += a[p][q] * a[p][q];
        if (off <= 1e-300) break;
        for (int p = 0; p < 3; ++p) {
            for (int q = p + 1; q < 3; ++q) {
                if (a[p][q] == 0.0) continue;
                const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const double t = (theta >= 0.0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0);
                const double s = t * c;
                for (int k = 0; k < 3; ++k) {
                    if (k == p || k == q) continue;
                    const double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[p][k] = a[k][p];
                    a[k][q] = s * akp + c * akq;
                    a[q][k] = a[k][q];
                }
                const double apq = a[p][q];
                a[p][p] = a[p][p] - t * apq;
                a[q][q] = a[q][q] + t * apq;
                a[p][q] = 0.0;
                a[q][p] = 0.0;
                for (int k = 0; k < 3; ++k) {
                    const double vkp = v[k][p], vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }
    for (int i = 0; i < 3; ++i) eval[i] = a[i][i];
    // Sort ascending, permuting the eigenvectors with the eigenvalues.
    int order[3] = {0, 1, 2};
    for (int i = 0; i < 3; ++i) {
        for (int j = i + 1; j < 3; ++j) {
            if (eval[order[j]] < eval[order[i]]) std::swap(order[i], order[j]);
        }
    }
    double sorted_eval[3];
    double sorted_vec[3][3];
    for (int i = 0; i < 3; ++i) {
        sorted_eval[i] = eval[order[i]];
        for (int k = 0; k < 3; ++k) sorted_vec[k][i] = v[k][order[i]];
    }
    for (int i = 0; i < 3; ++i) eval[i] = sorted_eval[i];
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) evec[k][i] = sorted_vec[k][i];
}

struct Metrics {
    double rmse = 0.0;
    double p95 = 0.0;
    double median = 0.0;
};

// The accuracy of a set of point-to-truth distances.
Metrics metrics_of(std::vector<double>& values) {
    Metrics m;
    if (values.empty()) return m;
    double sum_sq = 0.0;
    for (double v : values) sum_sq += v * v;
    m.rmse = std::sqrt(sum_sq / static_cast<double>(values.size()));
    const std::size_t n = values.size();
    // p95 by nearest rank.
    {
        std::vector<double> copy = values;
        std::size_t rank = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(n)));
        if (rank == 0) rank = 1;
        if (rank > n) rank = n;
        std::nth_element(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(rank - 1), copy.end());
        m.p95 = copy[rank - 1];
    }
    // Median.
    {
        std::vector<double> copy = values;
        const std::size_t mid = n / 2;
        std::nth_element(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(mid), copy.end());
        const double upper = copy[mid];
        if (n % 2 == 1) {
            m.median = upper;
        } else {
            const double lower = *std::max_element(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(mid));
            m.median = 0.5 * (lower + upper);
        }
    }
    return m;
}

}  // namespace

// ----------------------------------------------------------------- truth ----

Truth Truth::plane(double nx, double ny, double nz, double d) {
    Truth t;
    t.kind_ = Kind::kPlane;
    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 0.0) {
        t.nx_ = nx / len;
        t.ny_ = ny / len;
        t.nz_ = nz / len;
    }
    t.d_ = d;
    return t;
}

Truth Truth::sphere(double cx, double cy, double cz, double r) {
    Truth t;
    t.kind_ = Kind::kSphere;
    t.cx_ = cx;
    t.cy_ = cy;
    t.cz_ = cz;
    t.r_ = r;
    return t;
}

Truth Truth::point_set(std::vector<float> xyz) {
    Truth t;
    t.kind_ = Kind::kPointSet;
    t.pts_ = std::move(xyz);
    t.build_grid();
    return t;
}

void Truth::build_grid() {
    if (pts_.empty()) return;
    double minx = pts_[0], maxx = pts_[0];
    double miny = pts_[1], maxy = pts_[1];
    double minz = pts_[2], maxz = pts_[2];
    for (std::size_t i = 1; i < pts_.size() / 3; ++i) {
        minx = std::min(minx, static_cast<double>(pts_[i * 3 + 0]));
        maxx = std::max(maxx, static_cast<double>(pts_[i * 3 + 0]));
        miny = std::min(miny, static_cast<double>(pts_[i * 3 + 1]));
        maxy = std::max(maxy, static_cast<double>(pts_[i * 3 + 1]));
        minz = std::min(minz, static_cast<double>(pts_[i * 3 + 2]));
        maxz = std::max(maxz, static_cast<double>(pts_[i * 3 + 2]));
    }
    // A cell of about one point per cell on the mean diagonal.
    const double diag = std::sqrt((maxx - minx) * (maxx - minx) + (maxy - miny) * (maxy - miny) +
                                  (maxz - minz) * (maxz - minz));
    const std::size_t count = pts_.size() / 3;
    cell_ = std::max(diag / std::cbrt(static_cast<double>(std::max<std::size_t>(1, count))), 1e-9);
    ox_ = minx;
    oy_ = miny;
    oz_ = minz;
    gnx_ = std::max(1, static_cast<int>(std::ceil((maxx - minx) / cell_)) + 1);
    gny_ = std::max(1, static_cast<int>(std::ceil((maxy - miny) / cell_)) + 1);
    gnz_ = std::max(1, static_cast<int>(std::ceil((maxz - minz) / cell_)) + 1);
    const std::size_t cell_count = static_cast<std::size_t>(gnx_) * gny_ * gnz_;
    std::vector<int> counts(cell_count + 1, 0);
    auto cell_of = [&](std::size_t i) {
        const int ix = std::min(gnx_ - 1, std::max(0, static_cast<int>((pts_[i * 3 + 0] - ox_) / cell_)));
        const int iy = std::min(gny_ - 1, std::max(0, static_cast<int>((pts_[i * 3 + 1] - oy_) / cell_)));
        const int iz = std::min(gnz_ - 1, std::max(0, static_cast<int>((pts_[i * 3 + 2] - oz_) / cell_)));
        return (iz * gny_ + iy) * gnx_ + ix;
    };
    for (std::size_t i = 0; i < count; ++i) ++counts[cell_of(i) + 1];
    for (std::size_t c = 0; c < cell_count; ++c) counts[c + 1] += counts[c];
    cell_start_ = counts;
    items_.assign(count, 0);
    std::vector<int> cursor(cell_start_.begin(), cell_start_.end() - 1);
    for (std::size_t i = 0; i < count; ++i) items_[cursor[cell_of(i)]++] = static_cast<int>(i);
}

double Truth::distance(double x, double y, double z) const {
    switch (kind_) {
        case Kind::kPlane:
            return std::fabs(nx_ * x + ny_ * y + nz_ * z - d_);
        case Kind::kSphere: {
            const double dx = x - cx_, dy = y - cy_, dz = z - cz_;
            return std::fabs(std::sqrt(dx * dx + dy * dy + dz * dz) - r_);
        }
        case Kind::kPointSet:
            break;
    }
    if (pts_.empty()) return 0.0;
    // Nearest point in the truth set, found on the uniform grid.
    int ix = std::min(gnx_ - 1, std::max(0, static_cast<int>((x - ox_) / cell_)));
    int iy = std::min(gny_ - 1, std::max(0, static_cast<int>((y - oy_) / cell_)));
    int iz = std::min(gnz_ - 1, std::max(0, static_cast<int>((z - oz_) / cell_)));
    double best = std::numeric_limits<double>::infinity();
    const int max_ring = std::max(gnx_, std::max(gny_, gnz_));
    for (int r = 0; r <= max_ring; ++r) {
        for (int dz = -r; dz <= r; ++dz) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::max(std::abs(dy), std::abs(dz))) != r) continue;
                    const int cx = ix + dx, cy = iy + dy, cz = iz + dz;
                    if (cx < 0 || cy < 0 || cz < 0 || cx >= gnx_ || cy >= gny_ || cz >= gnz_) continue;
                    const int cell = (cz * gny_ + cy) * gnx_ + cx;
                    for (int s = cell_start_[cell]; s < cell_start_[cell + 1]; ++s) {
                        const int j = items_[s];
                        const double ddx = static_cast<double>(pts_[static_cast<std::size_t>(j) * 3 + 0]) - x;
                        const double ddy = static_cast<double>(pts_[static_cast<std::size_t>(j) * 3 + 1]) - y;
                        const double ddz = static_cast<double>(pts_[static_cast<std::size_t>(j) * 3 + 2]) - z;
                        best = std::min(best, std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz));
                    }
                }
            }
        }
        // A ring whose inner distance already exceeds the best hit settles it.
        if (std::isfinite(best) && (r - 1) * cell_ > best) break;
    }
    return std::isfinite(best) ? best : 0.0;
}

std::string Truth::describe() const {
    char buf[256];
    switch (kind_) {
        case Kind::kPlane:
            std::snprintf(buf, sizeof(buf), "plane n=(%.6g,%.6g,%.6g) d=%.6g", nx_, ny_, nz_, d_);
            break;
        case Kind::kSphere:
            std::snprintf(buf, sizeof(buf), "sphere c=(%.6g,%.6g,%.6g) r=%.6g", cx_, cy_, cz_, r_);
            break;
        case Kind::kPointSet:
            std::snprintf(buf, sizeof(buf), "point set (%zu points)", pts_.size() / 3);
            break;
    }
    return buf;
}

// ------------------------------------------------------------- estimator ----

PlaneFit fit_plane(const Cloud& cloud) {
    PlaneFit fit;
    const std::size_t n = cloud.size();
    if (n < 3) return fit;
    double mean[3] = {0.0, 0.0, 0.0};
    for (std::size_t i = 0; i < n; ++i) {
        const float* p = cloud.point(i);
        mean[0] += p[0];
        mean[1] += p[1];
        mean[2] += p[2];
    }
    for (int a = 0; a < 3; ++a) mean[a] /= static_cast<double>(n);
    double cov[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    for (std::size_t i = 0; i < n; ++i) {
        const float* p = cloud.point(i);
        const double d[3] = {static_cast<double>(p[0]) - mean[0], static_cast<double>(p[1]) - mean[1],
                             static_cast<double>(p[2]) - mean[2]};
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) cov[a][b] += d[a] * d[b];
    }
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) cov[a][b] /= static_cast<double>(n);
    double eval[3], evec[3][3];
    eigen_sym3(cov, eval, evec);
    // The normal is the eigenvector of the smallest eigenvalue.
    double normal[3] = {evec[0][0], evec[1][0], evec[2][0]};
    const double len = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    if (len <= 0.0) return fit;
    for (int a = 0; a < 3; ++a) normal[a] /= len;
    // The eigenvector's sign is arbitrary; fix it so the plane's numbers are
    // reproducible from run to run: the largest component is positive.
    int largest = 0;
    for (int a = 1; a < 3; ++a)
        if (std::fabs(normal[a]) > std::fabs(normal[largest])) largest = a;
    if (normal[largest] < 0.0)
        for (int a = 0; a < 3; ++a) normal[a] = -normal[a];
    fit.nx = normal[0];
    fit.ny = normal[1];
    fit.nz = normal[2];
    fit.d = normal[0] * mean[0] + normal[1] * mean[1] + normal[2] * mean[2];
    fit.points = n;
    std::vector<double> residuals(n);
    for (std::size_t i = 0; i < n; ++i) {
        const float* p = cloud.point(i);
        residuals[i] = std::fabs(normal[0] * p[0] + normal[1] * p[1] + normal[2] * p[2] - fit.d);
    }
    const Metrics m = metrics_of(residuals);
    fit.rms = m.rmse;
    fit.p95 = m.p95;
    return fit;
}

double estimate_spacing(const Cloud& cloud, int samples) {
    if (cloud.size() < 2) return 1.0;
    double minx = cloud.xyz[0], maxx = cloud.xyz[0];
    double miny = cloud.xyz[1], maxy = cloud.xyz[1];
    double minz = cloud.xyz[2], maxz = cloud.xyz[2];
    for (std::size_t i = 1; i < cloud.size(); ++i) {
        const float* p = cloud.point(i);
        minx = std::min(minx, static_cast<double>(p[0]));
        maxx = std::max(maxx, static_cast<double>(p[0]));
        miny = std::min(miny, static_cast<double>(p[1]));
        maxy = std::max(maxy, static_cast<double>(p[1]));
        minz = std::min(minz, static_cast<double>(p[2]));
        maxz = std::max(maxz, static_cast<double>(p[2]));
    }
    const double diag = std::sqrt((maxx - minx) * (maxx - minx) + (maxy - miny) * (maxy - miny) +
                                  (maxz - minz) * (maxz - minz));
    // A coarse grid for the nearest-neighbour probe: fine enough to resolve
    // the spacing, coarse enough to stay small on a big cloud.
    const Grid probe = build_grid(cloud, std::max(diag / 256.0, 1e-9), 4u << 20);
    const std::size_t n = cloud.size();
    const std::size_t take = std::min<std::size_t>(static_cast<std::size_t>(std::max(samples, 1)), n);
    const std::size_t stride = std::max<std::size_t>(1, n / take);
    std::vector<double> dists;
    dists.reserve(take);
    for (std::size_t i = 0; i < n && dists.size() < take; i += stride) {
        Neighbours nb;
        nb.k = 2;
        nb.d2.assign(2, 0.0);
        nb.index.assign(2, -1);
        collect_neighbours(probe, cloud, i, &nb, diag);
        if (nb.count >= 2) dists.push_back(std::sqrt(nb.d2[1]));
    }
    if (dists.empty()) return diag > 0.0 ? diag : 1.0;
    std::sort(dists.begin(), dists.end());
    return std::max(dists[dists.size() / 2], 1e-9);
}

std::vector<float> local_residual(const Cloud& cloud, const ResidualOptions& options, std::string* error) {
    std::vector<float> residual;
    if (cloud.size() < 4) {
        if (error) *error = "the cloud needs at least four points for the local fit";
        return residual;
    }
    const double spacing = estimate_spacing(cloud, options.spacing_samples);
    const Grid grid = build_grid(cloud, spacing, 8u << 20);
    const double max_radius = options.max_radius_factor * spacing;
    const int k = std::max(3, options.k);
    residual.assign(cloud.size(), 0.0f);
    Neighbours nb;
    nb.k = k;
    nb.d2.assign(static_cast<std::size_t>(k), 0.0);
    nb.index.assign(static_cast<std::size_t>(k), -1);
    for (std::size_t i = 0; i < cloud.size(); ++i) {
        nb.count = 0;
        collect_neighbours(grid, cloud, i, &nb, max_radius);
        if (nb.count < 3) {
            residual[i] = std::numeric_limits<float>::infinity();
            continue;
        }
        double mean[3] = {0.0, 0.0, 0.0};
        for (int n = 0; n < nb.count; ++n) {
            const float* q = cloud.point(static_cast<std::size_t>(nb.index[n]));
            mean[0] += q[0];
            mean[1] += q[1];
            mean[2] += q[2];
        }
        for (int a = 0; a < 3; ++a) mean[a] /= static_cast<double>(nb.count);
        double cov[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        for (int n = 0; n < nb.count; ++n) {
            const float* q = cloud.point(static_cast<std::size_t>(nb.index[n]));
            const double d[3] = {static_cast<double>(q[0]) - mean[0], static_cast<double>(q[1]) - mean[1],
                                 static_cast<double>(q[2]) - mean[2]};
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b) cov[a][b] += d[a] * d[b];
        }
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) cov[a][b] /= static_cast<double>(nb.count);
        double eval[3], evec[3][3];
        eigen_sym3(cov, eval, evec);
        const double value = eval[0] > 0.0 ? eval[0] : 0.0;
        residual[i] = static_cast<float>(std::sqrt(value));
    }
    return residual;
}

// ----------------------------------------------------------------- sweep ----

SweepSummary run_sweep(const std::vector<float>& confidence, const std::vector<double>& errors,
                       const SweepOptions& options, std::vector<SweepRow>* rows) {
    SweepSummary summary;
    summary.min_relative_improvement = std::numeric_limits<double>::infinity();
    const std::size_t n = confidence.size();
    if (rows) rows->clear();
    if (n == 0 || errors.size() != n) return summary;

    // The confidence order: highest first, ties broken by the point's index so
    // the sweep is reproducible.
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
                     [&confidence](std::size_t a, std::size_t b) { return confidence[a] > confidence[b]; });
    std::vector<float> sorted_confidence(n);
    std::vector<double> sorted_errors(n);
    for (std::size_t i = 0; i < n; ++i) {
        sorted_confidence[i] = confidence[order[i]];
        sorted_errors[i] = errors[order[i]];
    }

    const int steps = std::max(2, options.steps);
    const double min_fraction = std::min(std::max(options.min_fraction, 0.0), 1.0);
    Rng rng(options.seed);

    // The random control draws a fresh permutation per trial; the array is
    // reused and re-shuffled each time, which keeps the draw uniform.
    std::vector<std::size_t> scratch_indices(n);
    std::iota(scratch_indices.begin(), scratch_indices.end(), 0);
    std::vector<double> scratch;

    double prev_fraction = 0.0;
    double prev_gap = 0.0;
    bool have_prev = false;
    for (int step = 0; step < steps; ++step) {
        const double fraction =
            1.0 - static_cast<double>(step) * (1.0 - min_fraction) / static_cast<double>(steps - 1);
        std::size_t keep = static_cast<std::size_t>(std::llround(fraction * static_cast<double>(n)));
        keep = std::min(keep, n);
        SweepRow row;
        if (options.absolute) {
            // An absolute sweep: keep everything at or above the threshold.
            const double lo = options.absolute_min;
            const double hi = options.absolute_max;
            const double t = steps == 1 ? lo : lo + (hi - lo) * static_cast<double>(step) / static_cast<double>(steps - 1);
            row.threshold = t;
            keep = 0;
            for (std::size_t i = 0; i < n; ++i)
                if (sorted_confidence[i] >= t) ++keep;
        } else {
            // A quantile sweep: the threshold is the confidence at the
            // retention boundary, so the row's retained fraction is exact.
            row.threshold = keep > 0 ? sorted_confidence[keep - 1] : std::numeric_limits<double>::quiet_NaN();
        }
        // The row's fraction is the fraction actually kept, not the sweep's
        // nominal one: in the absolute mode the threshold decides how many
        // points survive, and the nominal quantile says nothing about it.
        row.n_retained = keep;
        row.retained_fraction = static_cast<double>(keep) / static_cast<double>(n);
        if (keep == 0) {
            if (rows) rows->push_back(row);
            continue;
        }
        scratch.assign(sorted_errors.begin(), sorted_errors.begin() + static_cast<std::ptrdiff_t>(keep));
        const Metrics m = metrics_of(scratch);
        row.rmse = m.rmse;
        row.p95 = m.p95;
        row.median = m.median;

        // The random control at the same retained fraction.
        std::vector<double> trial_rmse;
        std::vector<double> trial_p95;
        std::vector<double> trial_median;
        trial_rmse.reserve(static_cast<std::size_t>(std::max(1, options.random_trials)));
        for (int trial = 0; trial < std::max(1, options.random_trials); ++trial) {
            for (std::size_t i = 0; i < keep; ++i) {
                const std::size_t j = i + rng.below(n - i);
                std::swap(scratch_indices[i], scratch_indices[j]);
            }
            scratch.resize(keep);
            for (std::size_t i = 0; i < keep; ++i) scratch[i] = errors[scratch_indices[i]];
            const Metrics tm = metrics_of(scratch);
            trial_rmse.push_back(tm.rmse);
            trial_p95.push_back(tm.p95);
            trial_median.push_back(tm.median);
        }
        auto mean_of = [](const std::vector<double>& v) {
            double sum = 0.0;
            for (double x : v) sum += x;
            return v.empty() ? 0.0 : sum / static_cast<double>(v.size());
        };
        auto stddev_of = [](const std::vector<double>& v, double mean) {
            if (v.size() < 2) return 0.0;
            double sum = 0.0;
            for (double x : v) sum += (x - mean) * (x - mean);
            return std::sqrt(sum / static_cast<double>(v.size() - 1));
        };
        row.rmse_random_mean = mean_of(trial_rmse);
        row.rmse_random_std = stddev_of(trial_rmse, row.rmse_random_mean);
        row.p95_random_mean = mean_of(trial_p95);
        row.median_random_mean = mean_of(trial_median);

        // The area between the curves, over the whole sweep.
        const double gap = row.rmse_random_mean - row.rmse;
        if (have_prev) {
            // The sweep runs downwards (from full retention to the smallest
            // fraction), so the step's width is the difference.
            const double df = prev_fraction - row.retained_fraction;
            summary.area_gap += 0.5 * (gap + prev_gap) * df;
        }
        prev_fraction = row.retained_fraction;
        prev_gap = gap;
        have_prev = true;

        // The acceptance window: the full-retention row is excluded by
        // construction (both curves are then the same set of points).
        if (row.retained_fraction > options.window_lo && row.retained_fraction < options.window_hi) {
            ++summary.rows_evaluated;
            if (gap > 0.0) ++summary.rows_beaten;
            if (row.rmse_random_mean > 0.0) {
                const double relative = gap / row.rmse_random_mean;
                summary.mean_relative_improvement += relative;
                summary.min_relative_improvement = std::min(summary.min_relative_improvement, relative);
            }
        }
        if (rows) rows->push_back(row);
    }
    if (summary.rows_evaluated > 0) {
        summary.mean_relative_improvement /= static_cast<double>(summary.rows_evaluated);
    } else {
        summary.min_relative_improvement = 0.0;
    }
    summary.beats_random_fraction =
        summary.rows_evaluated > 0 ? static_cast<double>(summary.rows_beaten) / static_cast<double>(summary.rows_evaluated)
                                   : 0.0;
    summary.beats_random = summary.rows_evaluated > 0 && summary.rows_beaten == summary.rows_evaluated;
    return summary;
}

bool meets_acceptance(const SweepSummary& summary, double min_improvement) {
    if (summary.rows_evaluated == 0) return false;
    return summary.beats_random && summary.mean_relative_improvement >= min_improvement;
}

// ------------------------------------------------------------- synthetic ----

Cloud make_synthetic_scene(const SyntheticSpec& spec, Truth* truth) {
    Cloud cloud;
    const int grid = std::max(4, spec.grid);
    cloud.xyz.reserve(static_cast<std::size_t>(grid) * grid * 3);
    Rng rng(spec.seed);
    const double half = 0.5 * static_cast<double>(grid - 1) * spec.spacing;
    for (int j = 0; j < grid; ++j) {
        for (int i = 0; i < grid; ++i) {
            const double x = -half + static_cast<double>(i) * spec.spacing;
            const double y = -half + static_cast<double>(j) * spec.spacing;
            // The bad quadrant: biased and noisy. The truth is the plane z = 0.
            const bool bad = (x > 0.0 && y > 0.0);
            const double sigma = bad ? spec.bad_sigma : spec.good_sigma;
            const double bias = bad ? spec.bad_bias : 0.0;
            const double z = bias + sigma * rng.normal();
            cloud.xyz.push_back(static_cast<float>(x));
            cloud.xyz.push_back(static_cast<float>(y));
            cloud.xyz.push_back(static_cast<float>(z));
        }
    }
    if (truth) *truth = synthetic_truth();
    return cloud;
}

Truth synthetic_truth() { return Truth::plane(0.0, 0.0, 1.0, 0.0); }

}  // namespace confidence
}  // namespace openscanner
