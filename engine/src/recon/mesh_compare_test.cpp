// WO-B1's device-free mesh A/B test: the mesh-compare metrics on known meshes
// (Manifold's exact box primitive), where the answer is known by construction.
#include "recon/mesh_compare.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "openscanner/mesh_tools.h"

namespace recon = openscanner::recon;
namespace mesh = openscanner::mesh;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    // --- a watertight box vs itself: zero distance ------------------------
    {
        const mesh::MeshBuffers box = mesh::make_box(2.0, 2.0, 2.0);
        const recon::MeshMetrics m = recon::CompareMeshes(box, box);
        Check(m.valid, "box vs itself: valid");
        Check(m.hausdorff < 1e-9 && m.rms_surface < 1e-9, "box vs itself: zero surface distance");
        Check(m.watertight_a && m.watertight_b, "box vs itself: both watertight");
        Check(m.tris_a == m.tris_b && m.tris_a > 0, "box vs itself: triangle counts match");
    }

    // --- a box vs a larger box: the distance is the size difference -------
    {
        const mesh::MeshBuffers small = mesh::make_box(2.0, 2.0, 2.0);
        const mesh::MeshBuffers big = mesh::make_box(2.4, 2.4, 2.4);
        const recon::MeshMetrics m = recon::CompareMeshes(small, big);
        Check(m.valid && m.hausdorff > 0.0 && m.rms_surface > 0.0, "box vs larger box: non-zero distance");
        // Each face moved out by 0.2 (half of 0.4); Hausdorff is at least that.
        Check(m.hausdorff > 0.15 && m.hausdorff < 0.6, "box vs larger box: Hausdorff ~ the half-size difference");
        Check(m.watertight_a && m.watertight_b, "box vs larger box: both watertight");
        std::printf("[mesh-compare] box2 vs box2.4: hausdorff=%.3f rms=%.3f\n", m.hausdorff, m.rms_surface);
    }

    // --- watertightness: a box is, a lone triangle is not -----------------
    {
        Check(recon::IsWatertight(mesh::make_box(1, 1, 1)), "watertight: a box is watertight");
        mesh::MeshBuffers tri;
        tri.vertices = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        tri.triangles = {0, 1, 2};
        Check(!recon::IsWatertight(tri), "watertight: a lone triangle is not (open boundary)");
    }

    // --- an empty mesh is invalid -----------------------------------------
    {
        const mesh::MeshBuffers box = mesh::make_box(1, 1, 1);
        Check(!recon::CompareMeshes(mesh::MeshBuffers{}, box).valid, "empty A is invalid");
        Check(!recon::CompareMeshes(box, mesh::MeshBuffers{}).valid, "empty B is invalid");
    }

    std::printf("[mesh-compare] %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
