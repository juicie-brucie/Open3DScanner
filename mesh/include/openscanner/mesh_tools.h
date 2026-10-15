#pragma once

#include <cstdint>
#include <vector>

// Thin, dependency-isolating wrapper around the mesh-editing toolbox
// (Manifold for boolean/CSG ops, libigl for smoothing/decimation). Callers
// use this plain buffer type so nothing outside mesh/src/mesh_tools.cpp has
// to include <manifold/manifold.h> or Eigen directly.
//
// See docs/architecture/mesh-editing-stack.md for why these two libraries
// and not CGAL/MeshLab/vcglib.

namespace openscanner::mesh {

// A plain triangle mesh: flat vertex positions (x0,y0,z0, x1,y1,z1, ...) and
// triangle indices (stride 3) into that vertex array.
struct MeshBuffers {
    std::vector<double> vertices;    // size = 3 * vertex_count
    std::vector<uint32_t> triangles; // size = 3 * triangle_count

    [[nodiscard]] size_t vertex_count() const { return vertices.size() / 3; }
    [[nodiscard]] size_t triangle_count() const { return triangles.size() / 3; }
};

enum class BooleanOp { kUnion, kDifference, kIntersection };

// Axis-aligned box centered at the origin, size x*y*z. A minimal, always-
// manifold primitive — useful for exercising the boolean pipeline without
// needing a scanned mesh as input yet.
MeshBuffers make_box(double size_x, double size_y, double size_z);

// Boolean combine two manifold meshes (Manifold, Apache-2.0). Both inputs
// must already be valid 2-manifold meshes (e.g. scanned meshes that have
// been through hole-filling/repair first) — Manifold does not attempt to
// repair non-manifold input.
MeshBuffers mesh_boolean(const MeshBuffers& a, const MeshBuffers& b, BooleanOp op);

// Greedy edge-collapse decimation down to at most `target_triangle_count`
// triangles (igl::decimate, libigl MPL-2.0 core). Useful for reducing a dense
// scan mesh before export/printing.
MeshBuffers decimate(const MeshBuffers& mesh, int target_triangle_count);

}  // namespace openscanner::mesh
