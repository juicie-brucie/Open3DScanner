#include "recon/mesh_surface.h"

#include <Eigen/Dense>
#include <igl/marching_cubes.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

#include "recon/cloud_compare.h"  // NearestNeighborIndex
#include "recon/pose.h"           // Apply

namespace openscanner::recon {
namespace {

struct Bbox {
    double mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
    bool valid = false;
};

Bbox BoundsOf(const std::vector<Point3>& pts) {
    Bbox b;
    if (pts.empty()) return b;
    for (int i = 0; i < 3; ++i) {
        b.mn[i] = std::numeric_limits<double>::infinity();
        b.mx[i] = -b.mn[i];
    }
    for (const Point3& p : pts) {
        const double v[3] = {p.x, p.y, p.z};
        for (int i = 0; i < 3; ++i) {
            b.mn[i] = std::min(b.mn[i], v[i]);
            b.mx[i] = std::max(b.mx[i], v[i]);
        }
    }
    b.valid = true;
    return b;
}

// Marching cubes over a regular grid whose scalar field is `field`, extracted
// at `iso`. The grid is padded so the surface stays interior to it (and so
// closes into a watertight mesh). The voxel is coarsened if the node count
// would exceed the cap.
Surface RunMarchingCubes(const std::vector<Point3>& cloud, double voxel, double pad, double iso,
                         std::size_t max_nodes, const std::function<double(const Point3&)>& field) {
    Surface surface;
    const Bbox b = BoundsOf(cloud);
    if (!b.valid) return surface;

    double lo[3], span[3];
    for (int i = 0; i < 3; ++i) {
        lo[i] = b.mn[i] - pad;
        span[i] = (b.mx[i] + pad) - lo[i];
    }

    // Resolution per axis, with at least 2 nodes; coarsen the voxel if the grid
    // would exceed the node cap.
    auto resolution = [&](double v) {
        long long total = 1;
        int n[3];
        for (int i = 0; i < 3; ++i) {
            n[i] = std::max(2, static_cast<int>(std::ceil(span[i] / v)) + 1);
            total *= n[i];
        }
        return std::pair<long long, std::array<int, 3>>{total, {n[0], n[1], n[2]}};
    };
    auto [total, n] = resolution(voxel);
    while (total > static_cast<long long>(max_nodes) && voxel < 1e12) {
        voxel *= 1.26;  // ~2x the node budget per step
        std::tie(total, n) = resolution(voxel);
    }
    const int nx = n[0], ny = n[1], nz = n[2];

    // Grid corners and field values, in the order marching_cubes expects:
    // S(x + y*nx + z*nx*ny).
    const long long N = static_cast<long long>(nx) * ny * nz;
    Eigen::MatrixXd GV(N, 3);
    Eigen::VectorXd S(N);
    const double dx = span[0] / (nx - 1), dy = span[1] / (ny - 1), dz = span[2] / (nz - 1);
    for (int z = 0; z < nz; ++z)
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                const long long idx = x + static_cast<long long>(nx) * (y + static_cast<long long>(ny) * z);
                const Point3 p{lo[0] + x * dx, lo[1] + y * dy, lo[2] + z * dz};
                GV(idx, 0) = p.x; GV(idx, 1) = p.y; GV(idx, 2) = p.z;
                S(idx) = field(p);
            }

    Eigen::MatrixXd V;
    Eigen::MatrixXi F;
    igl::marching_cubes(S, GV, static_cast<unsigned>(nx), static_cast<unsigned>(ny),
                        static_cast<unsigned>(nz), iso, V, F);
    if (F.rows() == 0) return surface;

    surface.mesh.vertices.resize(static_cast<std::size_t>(V.rows()) * 3);
    for (Eigen::Index i = 0; i < V.rows(); ++i) {
        surface.mesh.vertices[3 * i + 0] = V(i, 0);
        surface.mesh.vertices[3 * i + 1] = V(i, 1);
        surface.mesh.vertices[3 * i + 2] = V(i, 2);
    }
    surface.mesh.triangles.resize(static_cast<std::size_t>(F.rows()) * 3);
    for (Eigen::Index i = 0; i < F.rows(); ++i) {
        surface.mesh.triangles[3 * i + 0] = static_cast<uint32_t>(F(i, 0));
        surface.mesh.triangles[3 * i + 1] = static_cast<uint32_t>(F(i, 1));
        surface.mesh.triangles[3 * i + 2] = static_cast<uint32_t>(F(i, 2));
    }
    surface.triangle_count = surface.mesh.triangle_count();
    surface.ok = surface.triangle_count > 0;
    return surface;
}

double AutoVoxel(const Bbox& b) {
    const double span = std::max({b.mx[0] - b.mn[0], b.mx[1] - b.mn[1], b.mx[2] - b.mn[2], 0.0});
    return span > 0.0 ? span / 48.0 : 1.0;
}

}  // namespace

std::vector<Point3> MergeFrames(const std::vector<FrameCloud>& frames) {
    std::vector<Point3> cloud;
    for (const FrameCloud& f : frames)
        for (const Point3& p : f.points) cloud.push_back(Apply(f.pose, p));
    return cloud;
}

Surface FastSurface::Reconstruct(const std::vector<FrameCloud>& frames,
                                 const ReconstructOptions& options) const {
    const std::vector<Point3> cloud = MergeFrames(frames);
    if (cloud.size() < 4) return Surface{};

    const double voxel = options.voxel > 0.0 ? options.voxel : AutoVoxel(BoundsOf(cloud));
    const double shell = options.shell_radius > 0.0 ? options.shell_radius : 1.8 * voxel;
    const double pad = options.pad > 0.0 ? options.pad : shell + 2.0 * voxel;

    // Unsigned distance to the nearest point; the {d == shell} isosurface is the
    // boundary of the union of balls of radius `shell` around the cloud — a
    // single closed shell. No normals needed.
    const NearestNeighborIndex index(cloud, 0.0);
    const auto field = [&index](const Point3& p) {
        double d = 0.0;
        return index.Nearest(p, &d, nullptr) ? d : std::numeric_limits<double>::infinity();
    };
    return RunMarchingCubes(cloud, voxel, pad, shell, options.max_grid_nodes, field);
}

Surface QualitySurface::Reconstruct(const std::vector<FrameCloud>& frames,
                                    const ReconstructOptions& options) const {
    const std::vector<Point3> cloud = MergeFrames(frames);
    if (cloud.size() < 4) return Surface{};

    const double voxel = options.voxel > 0.0 ? options.voxel : AutoVoxel(BoundsOf(cloud));
    const double normal_radius = options.normal_radius > 0.0 ? options.normal_radius : 3.0 * voxel;
    const double pad = options.pad > 0.0 ? options.pad : 3.0 * voxel;

    // Cloud centroid, used to orient the normals outward.
    Point3 centroid{};
    for (const Point3& p : cloud) { centroid.x += p.x; centroid.y += p.y; centroid.z += p.z; }
    const double inv = 1.0 / static_cast<double>(cloud.size());
    centroid.x *= inv; centroid.y *= inv; centroid.z *= inv;

    const NearestNeighborIndex index(cloud, 0.0);

    // PCA normal per point: the eigenvector of the smallest eigenvalue of the
    // local covariance, oriented outward from the centroid.
    std::vector<Point3> normals(cloud.size());
    std::vector<int> nbr;
    for (std::size_t i = 0; i < cloud.size(); ++i) {
        const Point3& p = cloud[i];
        index.Within(p, normal_radius, &nbr);
        Point3 normal{};
        if (nbr.size() >= 3) {
            Point3 mean{};
            for (int j : nbr) { mean.x += cloud[j].x; mean.y += cloud[j].y; mean.z += cloud[j].z; }
            const double m = 1.0 / static_cast<double>(nbr.size());
            mean.x *= m; mean.y *= m; mean.z *= m;
            Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
            for (int j : nbr) {
                const double ax = cloud[j].x - mean.x, ay = cloud[j].y - mean.y, az = cloud[j].z - mean.z;
                Eigen::Vector3d d(ax, ay, az);
                cov += d * d.transpose();
            }
            Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(cov);
            const Eigen::Vector3d v = es.eigenvectors().col(0);  // smallest eigenvalue
            normal = {v.x(), v.y(), v.z()};
        } else {
            normal = {p.x - centroid.x, p.y - centroid.y, p.z - centroid.z};
        }
        // Orient outward from the centroid.
        const double dot = normal.x * (p.x - centroid.x) + normal.y * (p.y - centroid.y) +
                           normal.z * (p.z - centroid.z);
        if (dot < 0.0) { normal.x = -normal.x; normal.y = -normal.y; normal.z = -normal.z; }
        const double len = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (len > 0.0) { normal.x /= len; normal.y /= len; normal.z /= len; }
        normals[i] = normal;
    }

    // Signed distance to the surface: the point-to-plane distance at the
    // nearest point.
    const auto field = [&index, &cloud, &normals](const Point3& p) {
        double d = 0.0;
        int idx = -1;
        if (!index.Nearest(p, &d, &idx)) return std::numeric_limits<double>::infinity();
        const Point3& q = cloud[static_cast<std::size_t>(idx)];
        const Point3& nrm = normals[static_cast<std::size_t>(idx)];
        return (p.x - q.x) * nrm.x + (p.y - q.y) * nrm.y + (p.z - q.z) * nrm.z;
    };
    return RunMarchingCubes(cloud, voxel, pad, /*iso=*/0.0, options.max_grid_nodes, field);
}

}  // namespace openscanner::recon
