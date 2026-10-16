#include "openscanner/obscan_mesh_bridge.h"

#include <stdexcept>
#include <string>

namespace openscanner {

// Vendor buffers are untrusted input: the struct layout is recovered, not
// documented, and real meshes now come back from hardware. Reject anything
// that would read through a null pointer or hand out-of-range indices to
// Manifold/libigl, rather than letting that become undefined behavior there.
mesh::MeshBuffers from_obscan_mesh(const ObScanTriangleMesh& captured) {
    if (captured.num_vertices > 0 && captured.vertices == nullptr) {
        throw std::invalid_argument("from_obscan_mesh: num_vertices > 0 but vertices is null");
    }
    if (captured.num_triangles > 0 && captured.triangles == nullptr) {
        throw std::invalid_argument("from_obscan_mesh: num_triangles > 0 but triangles is null");
    }
    for (uint32_t i = 0; i < captured.num_triangles; ++i) {
        const ObScanTriangle& t = captured.triangles[i];
        if (t.v0 >= captured.num_vertices || t.v1 >= captured.num_vertices || t.v2 >= captured.num_vertices) {
            throw std::invalid_argument("from_obscan_mesh: triangle " + std::to_string(i) +
                                        " references a vertex index >= num_vertices (" +
                                        std::to_string(captured.num_vertices) + ")");
        }
    }

    mesh::MeshBuffers result;

    result.vertices.reserve(static_cast<size_t>(captured.num_vertices) * 3);
    for (uint32_t i = 0; i < captured.num_vertices; ++i) {
        const ObScanPoint& p = captured.vertices[i];
        result.vertices.push_back(static_cast<double>(p.x));
        result.vertices.push_back(static_cast<double>(p.y));
        result.vertices.push_back(static_cast<double>(p.z));
    }

    result.triangles.reserve(static_cast<size_t>(captured.num_triangles) * 3);
    for (uint32_t i = 0; i < captured.num_triangles; ++i) {
        const ObScanTriangle& t = captured.triangles[i];
        result.triangles.push_back(t.v0);
        result.triangles.push_back(t.v1);
        result.triangles.push_back(t.v2);
    }

    return result;
}

CapturedPointCloud from_obscan_point_cloud(const ObScanPointClouds& captured) {
    if (captured.num_points > 0 && captured.points == nullptr) {
        throw std::invalid_argument("from_obscan_point_cloud: num_points > 0 but points is null");
    }
    CapturedPointCloud result;
    result.positions.reserve(static_cast<size_t>(captured.num_points) * 3);
    for (uint32_t i = 0; i < captured.num_points; ++i) {
        const ObScanPoint& p = captured.points[i];
        result.positions.push_back(static_cast<double>(p.x));
        result.positions.push_back(static_cast<double>(p.y));
        result.positions.push_back(static_cast<double>(p.z));
    }
    return result;
}

}  // namespace openscanner
