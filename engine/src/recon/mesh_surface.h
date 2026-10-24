#pragma once
// Surface reconstruction from the fused point cloud (WO-B1, Phase C mesh).
//
// This is a laser-line scanner: each frame is a sparse set of stripe points,
// not a dense depth image, so the natural representation is the accumulated
// point cloud (WO-A's fuse), and a surface is reconstructed from it — not a
// TSDF depth volume.
//
// Reconstruction sits behind a SurfaceReconstructor seam ("adaptability"), so
// the method is swappable without touching callers. Two tiers ship here,
// mirroring the live/post-scan split:
//   * FastSurface (live) — a coarse unsigned-distance shell through marching
//     cubes: cheap, re-runnable each tick, and needs NO normal orientation. It
//     shrink-wraps the cloud into a closed proxy surface so the viewport can
//     show the surface forming. Blobby/slightly inflated by design.
//   * QualitySurface (post-scan) — a signed-distance field from PCA-estimated
//     oriented normals: a thin, accurate surface for the final mesh and the
//     vendor A/B.
// A heavier method (screened Poisson) or a per-frame one (TSDF) can drop in
// behind the same seam later; the input is the posed frames so a per-frame
// method has what it needs.
//
// This stage is host-side and deliberately uses the mesh stack (libigl's
// marching cubes + Eigen); the zero-dependency live kernels (triangulate,
// pose, fuse) are unaffected — the dependency boundary is kept here.

#include <cstddef>
#include <vector>

#include "openscanner/mesh_tools.h"  // MeshBuffers
#include "recon/fuse.h"              // FrameCloud
#include "recon/triangulate.h"       // Point3

namespace openscanner::recon {

struct ReconstructOptions {
    double voxel = 0.0;          // grid cell size; <= 0 picks ~1/48 of the cloud extent
    double pad = 0.0;            // margin added around the cloud; <= 0 picks from voxel
    double shell_radius = 0.0;   // FastSurface iso offset; <= 0 picks ~1.8 * voxel
    double normal_radius = 0.0;  // QualitySurface PCA neighbourhood; <= 0 picks ~3 * voxel
    std::size_t max_grid_nodes = 8'000'000;  // cap; the voxel is coarsened to fit
};

struct Surface {
    openscanner::mesh::MeshBuffers mesh;
    bool ok = false;  // false: too few points, or marching cubes produced nothing
    std::size_t triangle_count = 0;
};

// The reconstruction seam. The input is the posed frames — the richest form,
// and the pipeline's source of truth: every proxy is a projection of it (the
// merged cloud, the voxel-bounded live model, the decimated point splats, the
// FastSurface shell, the QualitySurface mesh, and a future per-frame TSDF
// volume). A flattened cloud would discard exactly what the deeper proxies
// need — which point came from which pose: view direction, per-frame
// confidence, temporal (last-N-frame) previews, selective re-fusion, visibility
// carving. So the seam takes frames even though today's two distance-field
// impls only need the merged cloud (that richness is latent here, kept so a
// per-frame method can use it later without reworking callers).
class SurfaceReconstructor {
public:
    virtual ~SurfaceReconstructor() = default;
    virtual Surface Reconstruct(const std::vector<FrameCloud>& frames,
                                const ReconstructOptions& options = {}) const = 0;
    virtual const char* name() const = 0;
};

// Live tier: a coarse unsigned-distance shell (the {distance == shell_radius}
// isosurface, i.e. the boundary of the union of balls around the points).
// Closed and watertight, needs no normals, cheap to recompute.
class FastSurface : public SurfaceReconstructor {
public:
    Surface Reconstruct(const std::vector<FrameCloud>& frames,
                        const ReconstructOptions& options = {}) const override;
    const char* name() const override { return "fast-shell"; }
};

// Post-scan tier: a signed-distance field from PCA normals oriented outward
// from the cloud centroid (fine for the closed shapes under test; a global MST
// orientation is the refinement for arbitrary partial scans).
class QualitySurface : public SurfaceReconstructor {
public:
    Surface Reconstruct(const std::vector<FrameCloud>& frames,
                        const ReconstructOptions& options = {}) const override;
    const char* name() const override { return "signed-sdf"; }
};

// Posed frames -> one model-frame point cloud (each point placed by its pose).
std::vector<Point3> MergeFrames(const std::vector<FrameCloud>& frames);

}  // namespace openscanner::recon
