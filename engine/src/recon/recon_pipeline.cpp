#include "recon/recon_pipeline.h"

#include "recon/pose.h"  // Pose

namespace openscanner::recon {

PipelineOutput RunReconPipeline(const std::vector<FrameCloud>& frames, const PipelineConfig& config) {
    PipelineOutput out;
    if (frames.empty()) return out;
    out.cloud = FuseBatch(frames, config.fuse_voxel);
    if (out.cloud.empty()) return out;
    const QualitySurface quality;
    const std::vector<FrameCloud> one = {{out.cloud, Pose{}}};  // already in the model frame
    const Surface s = quality.Reconstruct(one, config.recon);
    out.mesh = s.mesh;
    out.triangle_count = s.triangle_count;
    out.ok = s.ok;
    return out;
}

LiveReconPipeline::LiveReconPipeline(const PipelineConfig& config)
    : config_(config), fuser_(config.fuse_voxel) {}

void LiveReconPipeline::AddFrame(const FrameCloud& frame) {
    fuser_.Add(frame.points, frame.pose);
    ++frames_;
}

std::vector<Point3> LiveReconPipeline::LiveCloud() const { return fuser_.Model(); }

Surface LiveReconPipeline::LiveSurface() const {
    const FastSurface fast;
    const std::vector<FrameCloud> one = {{fuser_.Model(), Pose{}}};
    return fast.Reconstruct(one, config_.recon);
}

PipelineOutput LiveReconPipeline::Finalize() const {
    PipelineOutput out;
    out.cloud = fuser_.Model();
    if (out.cloud.empty()) return out;
    const QualitySurface quality;
    const std::vector<FrameCloud> one = {{out.cloud, Pose{}}};
    const Surface s = quality.Reconstruct(one, config_.recon);
    out.mesh = s.mesh;
    out.triangle_count = s.triangle_count;
    out.ok = s.ok;
    return out;
}

}  // namespace openscanner::recon
