// Proves the mesh-editing toolbox wiring (Manifold + libigl) actually links
// and runs: build two boxes, boolean-subtract one from the other, then
// decimate the result. No scanned input needed — this only exercises the
// library plumbing, same spirit as tools/smoke_test for the OBScan bindings.
#include <openscanner/mesh_tools.h>

#include <cstdio>
#include <cstdlib>

int main() {
    using namespace openscanner::mesh;

    const MeshBuffers box_a = make_box(2.0, 2.0, 2.0);
    const MeshBuffers box_b = make_box(1.2, 1.2, 1.2);
    std::printf(
        "box_a: %zu verts / %zu tris, box_b: %zu verts / %zu tris\n",
        box_a.vertex_count(), box_a.triangle_count(), box_b.vertex_count(),
        box_b.triangle_count());

    if (box_a.vertex_count() == 0 || box_a.triangle_count() == 0) {
        std::fprintf(stderr, "FAIL: make_box produced an empty mesh\n");
        return EXIT_FAILURE;
    }

    MeshBuffers combined;
    try {
        combined = mesh_boolean(box_a, box_b, BooleanOp::kDifference);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: mesh_boolean threw: %s\n", e.what());
        return EXIT_FAILURE;
    }
    std::printf(
        "box_a - box_b: %zu verts / %zu tris\n", combined.vertex_count(),
        combined.triangle_count());

    if (combined.triangle_count() == 0) {
        std::fprintf(stderr, "FAIL: boolean difference produced an empty mesh\n");
        return EXIT_FAILURE;
    }
    // A cube with a smaller cube cut out of it is no longer convex, so it
    // must have picked up more triangles than either input box alone.
    if (combined.triangle_count() <= box_a.triangle_count()) {
        std::fprintf(
            stderr,
            "FAIL: expected the cut mesh to have more triangles than the "
            "uncut box (got %zu vs %zu)\n",
            combined.triangle_count(), box_a.triangle_count());
        return EXIT_FAILURE;
    }

    const int target_tris = static_cast<int>(combined.triangle_count() / 2);
    MeshBuffers decimated;
    try {
        decimated = decimate(combined, target_tris);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: decimate threw: %s\n", e.what());
        return EXIT_FAILURE;
    }
    std::printf(
        "decimated to target %d tris: %zu verts / %zu tris\n", target_tris,
        decimated.vertex_count(), decimated.triangle_count());

    if (decimated.triangle_count() == 0 ||
        decimated.triangle_count() > combined.triangle_count()) {
        std::fprintf(
            stderr,
            "FAIL: decimate produced %zu tris, expected >0 and <= %zu\n",
            decimated.triangle_count(), combined.triangle_count());
        return EXIT_FAILURE;
    }

    std::printf("OK: Manifold boolean + libigl decimate both wired up correctly\n");
    return EXIT_SUCCESS;
}
