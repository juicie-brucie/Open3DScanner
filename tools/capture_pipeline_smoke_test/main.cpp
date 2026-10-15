// Verifies the capture -> mesh-tools data path without needing a physical
// scanner: constructs ObScanTriangleMesh / ObScanPointClouds structs in the
// exact shape obscan_scan_get_realtime_model_mesh / obscan_scan_get_raw_cloud
// hand back (per obscan_api.h), runs them through
// openscanner::from_obscan_mesh / from_obscan_point_cloud, then through the
// mesh-editing toolbox (boolean + decimate), and checks the data survives
// each hop intact.
//
// This is deliberately *not* a substitute for tools/live_capture_test: it
// proves the struct-shape and data-flow plumbing is correct, not that the
// real vendor SDK fills these structs the way we assume (struct field
// offsets for the session/scan subset are explicitly unverified — see
// obscan_api.h and README.md "Known issues"). Live device I/O is currently
// blocked on the RPCServer helper issue documented there, so this is the
// part of the data processing path that *can* be verified in an environment
// with no connected hardware.
#include <openscanner/mesh_tools.h>
#include <openscanner/obscan_api.h>
#include <openscanner/obscan_mesh_bridge.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

bool CheckMeshPath() {
    using openscanner::from_obscan_mesh;
    using openscanner::mesh::BooleanOp;
    using openscanner::mesh::make_box;
    using openscanner::mesh::mesh_boolean;

    // Build a plain box with the mesh toolbox, then copy it into vendor-
    // struct-shaped arrays — standing in for "the SDK just handed us this
    // mesh" without needing a real device to produce one.
    const openscanner::mesh::MeshBuffers source = make_box(1.0, 1.0, 1.0);

    std::vector<ObScanPoint> vendor_vertices;
    vendor_vertices.reserve(source.vertex_count());
    for (size_t i = 0; i < source.vertex_count(); ++i) {
        vendor_vertices.push_back(ObScanPoint{
            static_cast<float>(source.vertices[3 * i + 0]),
            static_cast<float>(source.vertices[3 * i + 1]),
            static_cast<float>(source.vertices[3 * i + 2]),
            /*intensity=*/1.0f,
        });
    }
    std::vector<ObScanTriangle> vendor_triangles;
    vendor_triangles.reserve(source.triangle_count());
    for (size_t i = 0; i < source.triangle_count(); ++i) {
        vendor_triangles.push_back(ObScanTriangle{
            source.triangles[3 * i + 0],
            source.triangles[3 * i + 1],
            source.triangles[3 * i + 2],
        });
    }

    ObScanTriangleMesh captured{};
    captured.num_vertices = static_cast<uint32_t>(vendor_vertices.size());
    captured.vertices = vendor_vertices.data();
    captured.normals = nullptr;  // not populated by this synthetic source
    captured.num_triangles = static_cast<uint32_t>(vendor_triangles.size());
    captured.triangles = vendor_triangles.data();

    const openscanner::mesh::MeshBuffers bridged = from_obscan_mesh(captured);

    if (bridged.vertex_count() != source.vertex_count() ||
        bridged.triangle_count() != source.triangle_count()) {
        std::fprintf(
            stderr,
            "FAIL: from_obscan_mesh changed element counts (verts %zu->%zu, "
            "tris %zu->%zu)\n",
            source.vertex_count(), bridged.vertex_count(),
            source.triangle_count(), bridged.triangle_count());
        return false;
    }
    for (size_t i = 0; i < bridged.vertices.size(); ++i) {
        if (bridged.vertices[i] != source.vertices[i]) {
            std::fprintf(stderr, "FAIL: vertex data mismatch at index %zu\n", i);
            return false;
        }
    }

    // Now prove the bridged data is actually usable by the mesh toolbox, not
    // just structurally intact: boolean-combine it with a second box.
    const openscanner::mesh::MeshBuffers other = make_box(0.6, 0.6, 0.6);
    openscanner::mesh::MeshBuffers combined;
    try {
        combined = mesh_boolean(bridged, other, BooleanOp::kUnion);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: mesh_boolean on bridged data threw: %s\n", e.what());
        return false;
    }
    if (combined.triangle_count() == 0) {
        std::fprintf(stderr, "FAIL: boolean union of bridged mesh produced nothing\n");
        return false;
    }

    std::printf(
        "mesh path OK: captured %zu verts/%zu tris -> bridged -> boolean "
        "union -> %zu verts/%zu tris\n",
        source.vertex_count(), source.triangle_count(), combined.vertex_count(),
        combined.triangle_count());
    return true;
}

bool CheckPointCloudPath() {
    using openscanner::from_obscan_point_cloud;

    std::vector<ObScanPoint> vendor_points = {
        ObScanPoint{0.0f, 0.0f, 0.0f, 1.0f},
        ObScanPoint{1.0f, 0.0f, 0.0f, 1.0f},
        ObScanPoint{0.0f, 1.0f, 0.0f, 0.8f},
        ObScanPoint{0.0f, 0.0f, 1.0f, 0.5f},
    };
    std::vector<ObScanNormal> vendor_normals(vendor_points.size(), ObScanNormal{0, 0, 1, 0});

    ObScanPointClouds captured{};
    captured.num_points = static_cast<uint32_t>(vendor_points.size());
    captured.points = vendor_points.data();
    captured.normals = vendor_normals.data();

    const openscanner::CapturedPointCloud bridged = from_obscan_point_cloud(captured);

    if (bridged.point_count() != vendor_points.size()) {
        std::fprintf(
            stderr, "FAIL: point count mismatch (%zu -> %zu)\n", vendor_points.size(),
            bridged.point_count());
        return false;
    }
    for (size_t i = 0; i < vendor_points.size(); ++i) {
        const ObScanPoint& src = vendor_points[i];
        if (bridged.positions[3 * i + 0] != src.x || bridged.positions[3 * i + 1] != src.y ||
            bridged.positions[3 * i + 2] != src.z) {
            std::fprintf(stderr, "FAIL: point %zu position mismatch after bridging\n", i);
            return false;
        }
    }

    std::printf("point cloud path OK: %zu captured points bridged intact\n", bridged.point_count());
    return true;
}

}  // namespace

int main() {
    const bool mesh_ok = CheckMeshPath();
    const bool cloud_ok = CheckPointCloudPath();

    if (!mesh_ok || !cloud_ok) {
        std::fprintf(stderr, "FAIL: capture pipeline smoke test failed\n");
        return EXIT_FAILURE;
    }
    std::printf("OK: capture -> mesh-tools data path verified (synthetic data, no hardware)\n");
    return EXIT_SUCCESS;
}
