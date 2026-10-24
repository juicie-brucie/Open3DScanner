// Device-free live-surface demo: a growing synthetic sphere driven through the
// real LiveReconPipeline, its FastSurface shell streamed as OMSH mesh frames
// (recon::EncodeMeshFrame) over WsBroadcastServer to the WebGL viewer served on
// the same port. It proves the whole live-surface chain — fuse -> FastSurface
// -> wire -> ws -> viewport — with no device and no vendor SDK.
//
//   surface_demo [--port N] [--web <dir>]
//
// This is a demo/harness, not product: the real viewport wiring lives in
// scan_server + ui/, and a real run also needs the gated calibration (#204/
// #508) and per-frame pose/association (#206). Here the input is synthetic.

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "openscanner/ws_broadcast_server.h"
#include "recon/fuse.h"            // FrameCloud
#include "recon/mesh_wire.h"       // EncodeMeshFrame
#include "recon/recon_pipeline.h"  // LiveReconPipeline

namespace recon = openscanner::recon;

namespace {

volatile std::sig_atomic_t g_stop = 0;
void OnSignal(int) { g_stop = 1; }

std::vector<recon::Point3> SphereCloud(int n, double R) {
    std::vector<recon::Point3> v;
    const double golden = 3.14159265358979323846 * (3.0 - std::sqrt(5.0));
    for (int i = 0; i < n; ++i) {
        const double y = 1.0 - 2.0 * (i + 0.5) / n;
        const double r = std::sqrt(std::max(0.0, 1.0 - y * y));
        v.push_back({R * r * std::cos(golden * i), R * y, R * r * std::sin(golden * i)});
    }
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    uint16_t port = 8787;
    std::string web;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = static_cast<uint16_t>(std::atoi(argv[++i]));
        else if (a == "--web" && i + 1 < argc) web = argv[++i];
    }
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    openscanner::WsBroadcastServer server;
    if (!web.empty()) server.SetStaticRoot(web);
    if (!server.Start(port)) {
        std::fprintf(stderr, "[surface-demo] could not bind port %u\n", port);
        return 1;
    }
    std::printf("[surface-demo] serving http://localhost:%u (web=%s)\n", port,
                web.empty() ? "(none)" : web.c_str());
    std::fflush(stdout);

    const double R = 1.0;
    const int kFrames = 24;
    const auto sphere = SphereCloud(2600, R);

    recon::PipelineConfig cfg;
    cfg.fuse_voxel = 0.0;  // keep every point

    while (!g_stop) {
        // One growth cycle: reveal the sphere in kFrames interleaved batches.
        std::vector<recon::FrameCloud> batches(kFrames);
        for (int i = 0; i < static_cast<int>(sphere.size()); ++i)
            batches[i % kFrames].points.push_back(sphere[i]);

        recon::LiveReconPipeline live(cfg);
        for (int f = 0; f < kFrames && !g_stop; ++f) {
            live.AddFrame(batches[f]);
            const recon::Surface s = live.LiveSurface();
            if (s.triangle_count > 0) {
                const std::vector<uint8_t> frame = recon::EncodeMeshFrame(s.mesh);
                server.BroadcastBinary(frame.data(), frame.size());
            }
            server.BroadcastText("{\"frames\":" + std::to_string(f + 1) + ",\"total\":" +
                                 std::to_string(kFrames) + ",\"tris\":" +
                                 std::to_string(s.triangle_count) + ",\"points\":" +
                                 std::to_string(live.LiveCloud().size()) + "}");
            std::this_thread::sleep_for(std::chrono::milliseconds(220));
        }
        // Hold the finished surface, then grow again.
        for (int k = 0; k < 10 && !g_stop; ++k)
            std::this_thread::sleep_for(std::chrono::milliseconds(220));
    }

    server.Stop();
    std::printf("[surface-demo] stopped\n");
    return 0;
}
