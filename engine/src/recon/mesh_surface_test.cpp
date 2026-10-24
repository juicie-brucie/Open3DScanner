// WO-B1's device-free reconstruction test: a synthetic sphere point cloud
// through both reconstructors, with the known geometry (radius R) as the
// answer. No device, no recorded data.
#include "recon/mesh_surface.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "recon/fuse.h"          // FrameCloud
#include "recon/mesh_compare.h"  // IsWatertight
#include "recon/pose.h"
#include "recon/triangulate.h"   // Point3

namespace recon = openscanner::recon;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

// Evenly-distributed points on a sphere of radius R (Fibonacci sphere).
std::vector<recon::Point3> SphereCloud(int n, double R) {
    std::vector<recon::Point3> v;
    const double golden = 3.14159265358979323846 * (3.0 - std::sqrt(5.0));
    for (int i = 0; i < n; ++i) {
        const double y = 1.0 - 2.0 * (i + 0.5) / n;
        const double r = std::sqrt(std::max(0.0, 1.0 - y * y));
        const double theta = golden * i;
        v.push_back({R * r * std::cos(theta), R * y, R * r * std::sin(theta)});
    }
    return v;
}

// Min / max / mean vertex radius from the origin.
void RadiusStats(const openscanner::mesh::MeshBuffers& m, double* lo, double* hi, double* mean) {
    *lo = 1e300; *hi = 0.0; double sum = 0.0;
    const size_t n = m.vertex_count();
    for (size_t i = 0; i < n; ++i) {
        const double x = m.vertices[3 * i], y = m.vertices[3 * i + 1], z = m.vertices[3 * i + 2];
        const double rad = std::sqrt(x * x + y * y + z * z);
        *lo = std::min(*lo, rad); *hi = std::max(*hi, rad); sum += rad;
    }
    *mean = n ? sum / n : 0.0;
}

}  // namespace

int main() {
    const double R = 1.0;
    std::vector<recon::FrameCloud> frames = {{SphereCloud(1500, R), recon::Pose{}}};

    // --- FastSurface: a closed proxy shell around the points --------------
    {
        const recon::FastSurface fast;
        const recon::Surface s = fast.Reconstruct(frames);
        Check(s.ok && s.triangle_count > 0, "fast: produced a mesh");
        Check(recon::IsWatertight(s.mesh), "fast: the shell is watertight");
        double lo, hi, mean;
        RadiusStats(s.mesh, &lo, &hi, &mean);
        // The {distance == shell} isosurface sits just inside and outside R.
        Check(lo > 0.80 * R && hi < 1.20 * R, "fast: the shell hugs the sphere (radius ~ R +/- shell)");
        std::printf("[mesh-surface] fast: tris=%zu radius[%.3f..%.3f]\n", s.triangle_count, lo, hi);
    }

    // --- QualitySurface: a thin surface through the points ----------------
    {
        const recon::QualitySurface quality;
        const recon::Surface s = quality.Reconstruct(frames);
        Check(s.ok && s.triangle_count > 0, "quality: produced a mesh");
        Check(recon::IsWatertight(s.mesh), "quality: watertight");
        double lo, hi, mean;
        RadiusStats(s.mesh, &lo, &hi, &mean);
        // A thin surface through the points sits at ~R.
        Check(std::fabs(mean - R) < 0.05 * R, "quality: the surface sits at the true radius");
        Check(hi - lo < 0.15 * R, "quality: it is thin (not a thick shell)");
        std::printf("[mesh-surface] quality: tris=%zu radius[%.3f..%.3f] mean=%.3f\n", s.triangle_count, lo,
                    hi, mean);
    }

    // --- the seam: drive both through the base interface ------------------
    {
        const recon::FastSurface fast;
        const recon::QualitySurface quality;
        const recon::SurfaceReconstructor* r1 = &fast;
        const recon::SurfaceReconstructor* r2 = &quality;
        Check(std::string(r1->name()) == "fast-shell" && std::string(r2->name()) == "signed-sdf",
              "seam: both reconstructors callable via SurfaceReconstructor");
        Check(r1->Reconstruct(frames).ok && r2->Reconstruct(frames).ok, "seam: both reconstruct via the interface");
    }

    // --- too few points is not ok -----------------------------------------
    {
        const recon::FastSurface fast;
        std::vector<recon::FrameCloud> tiny = {{{{0, 0, 0}, {1, 0, 0}}, recon::Pose{}}};
        Check(!fast.Reconstruct(tiny).ok, "fast: fewer than 4 points -> not ok");
    }

    std::printf("[mesh-surface] %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
