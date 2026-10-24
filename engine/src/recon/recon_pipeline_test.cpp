// WO-B2's device-free end-to-end test: a synthetic sphere, split into frames
// that "arrive" over time, through the whole pipeline (fuse -> mesh), in both
// the live and the post-scan tiers. The known geometry (radius R) is the
// answer; the live-accumulated final and the batch final are A/B'd for
// agreement. No device, no recorded data.
#include "recon/recon_pipeline.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "recon/fuse.h"
#include "recon/mesh_compare.h"
#include "recon/pose.h"

namespace recon = openscanner::recon;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

std::vector<recon::Point3> SphereCloud(int n, double R) {
    std::vector<recon::Point3> v;
    const double golden = 3.14159265358979323846 * (3.0 - std::sqrt(5.0));
    for (int i = 0; i < n; ++i) {
        const double y = 1.0 - 2.0 * (i + 0.5) / n;
        const double r = std::sqrt(std::max(0.0, 1.0 - y * y));
        const double theta = golden * i;
        v.push_back({R * r * std::cos(theta), R * y, R * r * std::sin(theta)});
    }
    return v;
}

double MeanRadius(const openscanner::mesh::MeshBuffers& m) {
    double sum = 0.0;
    const size_t n = m.vertex_count();
    for (size_t i = 0; i < n; ++i) {
        const double x = m.vertices[3 * i], y = m.vertices[3 * i + 1], z = m.vertices[3 * i + 2];
        sum += std::sqrt(x * x + y * y + z * z);
    }
    return n ? sum / n : 0.0;
}

}  // namespace

int main() {
    const double R = 1.0;
    const auto sphere = SphereCloud(2000, R);

    // Split into 10 interleaved frames that "arrive" over time (identity pose:
    // the poses are the track stage's job and are exercised in pose_test).
    const int kFrames = 10;
    std::vector<recon::FrameCloud> frames(kFrames);
    for (int i = 0; i < static_cast<int>(sphere.size()); ++i) frames[i % kFrames].points.push_back(sphere[i]);

    // --- live tier: feed frames, surface updates, finalize ----------------
    recon::PipelineOutput live_final;
    {
        recon::LiveReconPipeline live;  // voxel 0: keep every point
        for (int f = 0; f < kFrames; ++f) {
            live.AddFrame(frames[f]);
            if (f == kFrames / 2) {
                const recon::Surface s = live.LiveSurface();
                Check(s.ok && s.triangle_count > 0, "live: a surface is available mid-scan");
            }
        }
        Check(live.frame_count() == static_cast<size_t>(kFrames), "live: all frames integrated");
        Check(!live.LiveCloud().empty(), "live: the point proxy is non-empty");
        live_final = live.Finalize();
        Check(live_final.ok && recon::IsWatertight(live_final.mesh), "live: finalized mesh is watertight");
        Check(std::fabs(MeanRadius(live_final.mesh) - R) < 0.05 * R, "live: final mesh sits at the true radius");
    }

    // --- post-scan tier: batch the same frames ----------------------------
    recon::PipelineOutput batch;
    {
        batch = recon::RunReconPipeline(frames);
        Check(batch.ok && recon::IsWatertight(batch.mesh), "batch: mesh is watertight");
        Check(std::fabs(MeanRadius(batch.mesh) - R) < 0.05 * R, "batch: mesh sits at the true radius");
        std::printf("[pipeline] batch: cloud=%zu tris=%zu meanR=%.3f\n", batch.cloud.size(),
                    batch.triangle_count, MeanRadius(batch.mesh));
    }

    // --- A/B the live-accumulated final against the batch final -----------
    {
        const recon::MeshMetrics m = recon::CompareMeshes(live_final.mesh, batch.mesh);
        Check(m.valid, "A/B: metrics valid");
        Check(m.hausdorff < 0.1 * R, "A/B: live-accumulated and batch meshes agree");
        std::printf("[pipeline] live-vs-batch: hausdorff=%.4f rms=%.4f\n", m.hausdorff, m.rms_surface);
    }

    // --- no frames is not ok ----------------------------------------------
    {
        Check(!recon::RunReconPipeline({}).ok, "empty input -> not ok");
    }

    std::printf("[pipeline] %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
