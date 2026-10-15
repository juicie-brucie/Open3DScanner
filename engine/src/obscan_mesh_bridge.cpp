#include "openscanner/obscan_mesh_bridge.h"

namespace openscanner {

mesh::MeshBuffers from_obscan_mesh(const ObScanTriangleMesh& captured) {
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
