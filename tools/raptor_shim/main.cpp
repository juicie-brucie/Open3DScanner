// raptor_shim — the Phase D RPC shim (row #172).
//
// Serves the `ob_rpc_*` method set on localhost TCP (protocol-spec.md §1),
// with frames from a FrameSource. Today the source is an offline #132 capture
// bundle; the live USB source (#169/#171) plugs into the same interface.
//
//   raptor_shim --ir-bundle ~/captures/plan132-ir-v2 --depth-bundle ~/captures/plan132-depth-v2
//   raptor_shim --ir-bundle DIR --ir-port 10013 --depth-port 10010 --verbose
//
// It does not touch a device, does not stop the vendor daemon, and binds
// nothing but its two RPC ports (discovery is opt-in, off by default).

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include <unistd.h>

#include "bundle_source.h"
#include "discovery.h"
#include "frame_source.h"
#include "helper_claim.h"
#include "live_source.h"
#include "shim_server.h"

namespace {

volatile sig_atomic_t g_stop = 0;

void OnSignal(int) { g_stop = 1; }

void Usage() {
    std::fprintf(stderr,
                 "usage: raptor_shim [--host IP] [--ir-port N] [--depth-port N]\n"
                 "                   [--ir-bundle DIR] [--depth-bundle DIR]\n"
                 "                   [--no-ir] [--no-depth] [--verbose]\n"
                 "                   [--seconds N] [--discovery-bind IP:PORT]\n"
                 "  --ir-bundle      the #132 capture bundle that serves IR frames\n"
                 "  --depth-bundle   the #132 capture bundle that serves depth frames\n"
                 "  --source MODE    none | bundle | live (default: bundle when a bundle is given)\n"
                 "                   live is refused cleanly until the #178 transport and the\n"
                 "                   helper's claim exist; nothing is guessed\n"
                 "  --helper-service NAME  the helper's Mach service for the claim query\n"
                 "  --helper-status  ask that service for its claim status and exit\n"
                 "                   (the no-sudo check: 0 = the service answered)\n"
                 "  --no-ir/--no-depth  do not listen on that interface\n"
                 "  --seconds N      run for N seconds (0 = until SIGINT/SIGTERM)\n"
                 "  --discovery-bind bind UDP for GVCP DISCOVERY_CMD logging (off by default:\n"
                 "                   a running vendor daemon may own 3956)\n"
                 "  without a bundle an interface answers the no-data form, never a frame\n");
}

int ParseInt(const char* text, int fallback) {
    if (!text) return fallback;
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || value < 0) return fallback;
    return (int)value;
}

}  // namespace

int main(int argc, char** argv) {
    shim::ShimConfig config;
    std::string ir_bundle;
    std::string depth_bundle;
    std::string discovery_bind;
    std::string source_kind;
    std::string helper_service = "org.openscanner.helper";
    bool helper_status = false;
    int seconds = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            Usage();
            return 0;
        } else if (arg == "--host" && i + 1 < argc) {
            config.host = argv[++i];
        } else if (arg == "--ir-port" && i + 1 < argc) {
            config.ir_port = (uint16_t)ParseInt(argv[++i], config.ir_port);
        } else if (arg == "--depth-port" && i + 1 < argc) {
            config.depth_port = (uint16_t)ParseInt(argv[++i], config.depth_port);
        } else if (arg == "--ir-bundle" && i + 1 < argc) {
            ir_bundle = argv[++i];
        } else if (arg == "--depth-bundle" && i + 1 < argc) {
            depth_bundle = argv[++i];
        } else if (arg == "--source" && i + 1 < argc) {
            source_kind = argv[++i];
        } else if (arg == "--helper-service" && i + 1 < argc) {
            helper_service = argv[++i];
        } else if (arg == "--helper-status") {
            helper_status = true;
        } else if (arg == "--no-ir") {
            config.serve_ir = false;
        } else if (arg == "--no-depth") {
            config.serve_depth = false;
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--seconds" && i + 1 < argc) {
            seconds = ParseInt(argv[++i], 0);
        } else if (arg == "--discovery-bind" && i + 1 < argc) {
            discovery_bind = argv[++i];
        } else {
            std::fprintf(stderr, "raptor_shim: unknown argument '%s'\n", arg.c_str());
            Usage();
            return 2;
        }
    }

    config.log = [](const std::string& line) {
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
    };

    if (helper_status) {
        // Row #181: the operator's no-sudo XPC check, using the same channel
        // the live source uses. Exit 0 means the service answered (claimed or
        // not); 1 means it did not, and the detail says why.
        shim::HelperClaimChannel channel(helper_service);
        std::string detail;
        const shim::ClaimChannel::State state = channel.Query(&detail);
        const char* name = state == shim::ClaimChannel::State::kClaimedByHelper
                               ? "claimed-by-helper"
                               : (state == shim::ClaimChannel::State::kNotClaimed ? "not-claimed" : "unknown");
        std::string line = "[shim] helper status on " + helper_service + ": " + name;
        if (!detail.empty()) line += " (" + detail + ")";
        std::printf("%s\n", line.c_str());
        return state == shim::ClaimChannel::State::kUnknown ? 1 : 0;
    }

    if (!source_kind.empty() && source_kind != "none" && source_kind != "bundle" && source_kind != "live") {
        std::fprintf(stderr, "raptor_shim: --source wants none|bundle|live (got '%s')\n", source_kind.c_str());
        return 2;
    }
    if (source_kind == "live" && (!ir_bundle.empty() || !depth_bundle.empty())) {
        std::fprintf(stderr, "raptor_shim: --source live serves the live transport, not a bundle\n");
        return 2;
    }
    if (source_kind.empty()) source_kind = (ir_bundle.empty() && depth_bundle.empty()) ? "none" : "bundle";

    shim::BundleSource ir_source;
    shim::BundleSource depth_source;
    shim::HelperClaimChannel claim_channel(helper_service);
    std::unique_ptr<shim::LiveSource> live_source;
    std::string err;

    if (source_kind == "live") {
        // The live path: ask the helper for the claim, then the transport for
        // its configuration. Every missing piece is named; none is guessed.
        live_source.reset(shim::LiveSourceFactory::Create(&claim_channel, &err));
        if (!live_source) {
            std::fprintf(stderr, "raptor_shim: live source refused: %s\n", err.c_str());
            return 2;
        }
        config.ir_source = live_source.get();
        config.depth_source = live_source.get();
        std::printf("[shim] live source: started\n");
    }

    if (source_kind == "bundle" && !ir_bundle.empty()) {
        if (!ir_source.Open(ir_bundle, shim::IrInterface(), &err)) {
            std::fprintf(stderr, "raptor_shim: IR bundle: %s\n", err.c_str());
            return 2;
        }
        config.ir_source = &ir_source;
        std::printf("[shim] IR frames from %s\n", ir_bundle.c_str());
    }
    if (source_kind == "bundle" && !depth_bundle.empty()) {
        if (!depth_source.Open(depth_bundle, shim::DepthInterface(), &err)) {
            std::fprintf(stderr, "raptor_shim: depth bundle: %s\n", err.c_str());
            return 2;
        }
        config.depth_source = &depth_source;
        std::printf("[shim] depth frames from %s\n", depth_bundle.c_str());
    }

    shim::DiscoveryListener discovery;
    if (!discovery_bind.empty()) {
        const size_t colon = discovery_bind.rfind(':');
        if (colon == std::string::npos) {
            std::fprintf(stderr, "raptor_shim: --discovery-bind wants IP:PORT\n");
            return 2;
        }
        const std::string address = discovery_bind.substr(0, colon);
        const int port = ParseInt(discovery_bind.substr(colon + 1).c_str(), 3956);
        if (!discovery.Start(address, (uint16_t)port, config.log, &err)) {
            // Not fatal: the daemon usually owns this port, and the shim does
            // not take it away from the daemon.
            std::printf("[shim] discovery listener not started: %s\n", err.c_str());
        }
    }

    shim::ShimServer server(config);
    if (!server.Start(&err)) {
        std::fprintf(stderr, "raptor_shim: %s\n", err.c_str());
        return 3;
    }

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
    server.StartThread();

    const auto start = std::chrono::steady_clock::now();
    while (!g_stop) {
        if (seconds > 0) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start)
                    .count();
            if (elapsed >= seconds) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    server.Stop();
    server.Join();
    discovery.Stop();

    const shim::ShimStats stats = server.stats();
    std::printf("[shim] done: requests=%llu get_frame=%llu frames=%llu empty=%llu start=%llu stop=%llu "
                "send_data=%llu enumeration=%llu unknown=%llu bytes_out=%llu\n",
                (unsigned long long)stats.requests, (unsigned long long)stats.get_frame,
                (unsigned long long)stats.frames, (unsigned long long)stats.empty,
                (unsigned long long)stats.start_stream, (unsigned long long)stats.stop_stream,
                (unsigned long long)stats.send_data, (unsigned long long)stats.enumeration,
                (unsigned long long)stats.unknown, (unsigned long long)stats.bytes_out);
    // Every method the run saw, by name: the per-method tally is the log of
    // "each method" even when the per-call line is rate-limited.
    for (const auto& entry : stats.by_method) {
        std::printf("[shim] method tally: %s x%llu\n", entry.first.c_str(),
                    (unsigned long long)entry.second);
    }
    if (source_kind == "bundle" && ir_bundle.size()) {
        std::printf("[shim] IR bundle: served=%llu wraps=%llu skipped=%llu\n",
                    (unsigned long long)ir_source.frames_served(), (unsigned long long)ir_source.wraps(),
                    (unsigned long long)ir_source.skipped());
    }
    return 0;
}
