#pragma once
// A/B compare two triangle meshes (WO-B1, Phase C mesh): our reconstructed mesh
// against a reference (the vendor's exported mesh for the same scene), the
// end-to-end "own the math" check at the mesh level.
//
// Metrics: triangle counts, the symmetric Hausdorff distance, the RMS surface
// distance (our vertices to the reference surface), and watertightness of each
// mesh. Hausdorff and point-to-mesh distance come from libigl; watertightness
// from Manifold (a valid closed 2-manifold). Host-side; the dependency stays in
// this stage, not the live kernels.
//
// The quantitative vendor A/B is gated on a paired recording (the #207 HW
// card); the same harness scores the live proxy vs the quality mesh for free.

#include <cstddef>

#include "openscanner/mesh_tools.h"  // MeshBuffers

namespace openscanner::recon {

struct MeshMetrics {
    std::size_t tris_a = 0;
    std::size_t tris_b = 0;
    double hausdorff = 0.0;      // symmetric Hausdorff distance between the surfaces
    double rms_surface = 0.0;    // RMS distance of A's vertices to B's surface
    double max_surface = 0.0;    // worst of those
    bool watertight_a = false;   // A is a valid closed 2-manifold
    bool watertight_b = false;
    bool valid = false;          // false when either mesh is empty
};

// Is the mesh a valid closed 2-manifold (watertight)?
bool IsWatertight(const openscanner::mesh::MeshBuffers& mesh);

// Score mesh A (ours) against reference mesh B.
MeshMetrics CompareMeshes(const openscanner::mesh::MeshBuffers& a,
                          const openscanner::mesh::MeshBuffers& b);

}  // namespace openscanner::recon
