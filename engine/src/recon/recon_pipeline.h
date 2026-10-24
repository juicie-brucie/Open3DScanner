#pragma once
// The end-to-end Phase C pipeline (WO-B2): posed per-frame clouds -> fused
// model -> surface mesh, in both tiers. This wires the landed stages
// (triangulate upstream, fuse, mesh) into one orchestrator.
//
// The front of the chain — a recorded RCRP frame -> laser_line centres ->
// triangulate -> a per-frame cloud — is the tool's job (tools/recon_pipeline),
// because it needs the capture reader and the calibration. The reusable,
// device-free orchestration here takes posed FrameClouds (the richest input,
// per mesh_surface.h) and runs fuse + reconstruct:
//   * RunReconPipeline (post-scan): fuse all frames and build the quality mesh.
//   * LiveReconPipeline (live): feed frames as they arrive; the incremental
//     fuser keeps the point proxy, and LiveSurface() returns the current
//     FastSurface shell — the surface for the viewport. Finalize() builds the
//     quality mesh from everything accumulated.
//
// Device-free; the heavy mesh work is host-side (libigl). The real/vendor A/B
// is gated on the #207 paired recording; the calibration (#204/#508) and the
// per-frame pose/association (#206) are the tool's gated inputs.

#include <cstddef>
#include <vector>

#include "recon/fuse.h"           // FrameCloud, IncrementalFuser
#include "recon/mesh_surface.h"   // SurfaceReconstructor, Surface, ReconstructOptions
#include "recon/triangulate.h"    // Point3

namespace openscanner::recon {

struct PipelineConfig {
    double fuse_voxel = 0.0;       // live/accumulate voxel; <= 0 keeps every point
    ReconstructOptions recon;      // surface reconstruction knobs
};

struct PipelineOutput {
    std::vector<Point3> cloud;             // the fused model
    openscanner::mesh::MeshBuffers mesh;   // the quality surface mesh
    std::size_t triangle_count = 0;
    bool ok = false;                       // false: no frames, or reconstruction produced nothing
};

// Post-scan: fuse every posed frame and build the quality mesh.
PipelineOutput RunReconPipeline(const std::vector<FrameCloud>& frames, const PipelineConfig& config = {});

// Live: feed posed frames as they arrive. The incremental fuser bounds memory
// by the scanned volume; LiveSurface() returns the cheap FastSurface shell for
// the viewport each time it is called, and Finalize() builds the quality mesh
// from everything accumulated so far.
class LiveReconPipeline {
public:
    explicit LiveReconPipeline(const PipelineConfig& config = {});

    // Integrate one posed frame into the live model.
    void AddFrame(const FrameCloud& frame);

    // The current live proxy point cloud (the voxel-bounded fused model).
    std::vector<Point3> LiveCloud() const;

    // The current live surface: a FastSurface shell over the accumulated model.
    // Cheap enough to call per tick; empty until enough points have arrived.
    Surface LiveSurface() const;

    // The final quality mesh over everything accumulated.
    PipelineOutput Finalize() const;

    std::size_t frame_count() const { return frames_; }

private:
    PipelineConfig config_;
    IncrementalFuser fuser_;
    std::size_t frames_ = 0;
};

}  // namespace openscanner::recon
