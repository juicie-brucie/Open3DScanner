// Streams point-cloud frames over a local WebSocket so the web UI
// (ui/index.html) can render them. Two modes:
//
//   scan_server [port]
//       Synthetic, animated sphere — exists so the UI's render/latency
//       pipeline can be built and measured without a physical scanner.
//
//   scan_server --live [port] [path-to-liborbbec_scan.dylib-or-dll]
//       Drives a real OBScan session against a physically-connected
//       scanner (same context/session/scan lifecycle as
//       tools/live_capture_test) and broadcasts whatever it gets back.
//
//       Known limitation, carried over from live_capture_test's own
//       findings (see README.md "Known issues" and
//       logs/exposure_sweep_new_setup_20261016/): obscan_scan_get_raw_cloud
//       has never been observed to return nonzero points *during* active
//       scanning, regardless of config — so the live per-frame broadcast
//       will likely show 0 points throughout the scan. The only call that
//       has ever produced real data is obscan_scan_get_realtime_model_mesh,
//       and only once the scan is paused. This mode broadcasts raw_cloud
//       continuously while scanning (so the UI's "connected and streaming"
//       state is real even if empty), then does one final pause ->
//       get_realtime_model_mesh -> broadcast before tearing down, so the
//       UI gets to show real captured geometry at least once per session.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "openscanner/obscan_loader.h"
#include "openscanner/ws_broadcast_server.h"
#include "openscanner/worker_process.h"

namespace {

constexpr int kPointGridSize = 48;        // kPointGridSize^2 points on a sphere.
constexpr double kFrameIntervalMs = 1000.0 / 30.0;  // ~30 Hz.

struct Vec3 {
    float x, y, z;
};

std::vector<Vec3> MakeSpherePoints(int grid_size) {
    std::vector<Vec3> points;
    points.reserve(static_cast<size_t>(grid_size) * grid_size);
    for (int i = 0; i < grid_size; ++i) {
        double theta = M_PI * (static_cast<double>(i) + 0.5) / grid_size;  // 0..pi
        for (int j = 0; j < grid_size; ++j) {
            double phi = 2.0 * M_PI * static_cast<double>(j) / grid_size;  // 0..2pi
            float x = static_cast<float>(std::sin(theta) * std::cos(phi));
            float y = static_cast<float>(std::cos(theta));
            float z = static_cast<float>(std::sin(theta) * std::sin(phi));
            points.push_back({x, y, z});
        }
    }
    return points;
}

void RotateY(std::vector<Vec3>* points, double angle_rad) {
    float c = static_cast<float>(std::cos(angle_rad));
    float s = static_cast<float>(std::sin(angle_rad));
    for (auto& p : *points) {
        float x = p.x * c + p.z * s;
        float z = -p.x * s + p.z * c;
        p.x = x;
        p.z = z;
    }
}

double NowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(system_clock::now().time_since_epoch()).count();
}

void AppendU32(std::vector<uint8_t>* buf, uint32_t v) {
    buf->push_back(static_cast<uint8_t>(v & 0xFF));
    buf->push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf->push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf->push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void AppendF64(std::vector<uint8_t>* buf, double v) {
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(v), "double must be 8 bytes");
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i) {
        buf->push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
    }
}

void AppendF32(std::vector<uint8_t>* buf, float v) {
    uint32_t bits;
    static_assert(sizeof(bits) == sizeof(v), "float must be 4 bytes");
    std::memcpy(&bits, &v, sizeof(bits));
    AppendU32(buf, bits);
}

// Wire format shared with ui/app.js: u32 frame_id, f64 capture_timestamp_ms,
// u32 point_count, then point_count * (f32 x, f32 y, f32 z).
std::vector<uint8_t> BuildFrame(uint32_t frame_id, double capture_timestamp_ms, const float* xyz,
                                 uint32_t point_count) {
    std::vector<uint8_t> buf;
    buf.reserve(4 + 8 + 4 + static_cast<size_t>(point_count) * 3 * 4);
    AppendU32(&buf, frame_id);
    AppendF64(&buf, capture_timestamp_ms);
    AppendU32(&buf, point_count);
    for (uint32_t i = 0; i < point_count; ++i) {
        AppendF32(&buf, xyz[i * 3 + 0]);
        AppendF32(&buf, xyz[i * 3 + 1]);
        AppendF32(&buf, xyz[i * 3 + 2]);
    }
    return buf;
}

void RunSyntheticServer(uint16_t port) {
    openscanner::WsBroadcastServer server;
    if (!server.Start(port)) {
        std::fprintf(stderr, "Failed to start WebSocket server on port %u (port in use?)\n", port);
        std::exit(1);
    }
    std::printf("Streaming synthetic point-cloud frames on ws://0.0.0.0:%u\n", port);
    std::printf("Open ui/index.html in a browser to view it.\n");

    std::vector<Vec3> base_points = MakeSpherePoints(kPointGridSize);
    uint32_t frame_id = 0;
    double angle = 0.0;

    while (true) {
        auto tick_start = std::chrono::steady_clock::now();

        std::vector<Vec3> frame_points = base_points;
        RotateY(&frame_points, angle);
        angle += 0.02;

        std::vector<uint8_t> buf =
            BuildFrame(frame_id++, NowMs(), reinterpret_cast<const float*>(frame_points.data()),
                       static_cast<uint32_t>(frame_points.size()));
        if (server.ConnectedClientCount() > 0) {
            server.BroadcastBinary(buf.data(), buf.size());
        }

        auto elapsed = std::chrono::steady_clock::now() - tick_start;
        auto sleep_for = std::chrono::milliseconds(static_cast<int64_t>(kFrameIntervalMs)) -
                          std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        if (sleep_for > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(sleep_for);
        }
    }
}

// --- Live (real hardware) mode ---------------------------------------------
// Mirrors tools/live_capture_test's lifecycle; see that file for the full
// history of why each step is shaped the way it is. Runs inside a worker
// process (same crash/hang isolation as live_capture_test) because this
// exercises the same obscan_context_destroy teardown path known to crash
// or hang on this hardware without it.

constexpr const char* kWorkerFlag = "--live-worker";
constexpr int kDeviceWaitAttempts = 30;    // ~6s at 200ms/attempt.
constexpr int kDeviceWaitIntervalMs = 200;
constexpr int kWantStableFrames = 10;
// Bounded live-session length: this mode is for watching the UI respond to
// a real device, not for running an unattended multi-hour capture. Stop
// streaming after this long and go through the normal pause/stop/destroy
// teardown automatically. Overridable (scan_server --live <port> <path>
// <seconds>) so a short test run doesn't require killing the process
// before teardown — see README.md "Known issues": killing this worker
// before it reaches obscan_scan_stop/obscan_context_destroy skips the
// same teardown that's required a physical power cycle to recover from
// in the past. Let it finish on its own; don't pkill it.
int g_live_session_seconds = 300;
constexpr int kLiveTickIntervalMs = 100;  // ~10Hz raw_cloud polling.

std::atomic<bool> g_got_connect_message{false};
std::atomic<int> g_frame_ready_count{0};

// Set by a SIGINT/SIGTERM handler so Ctrl+C (or a plain `kill`/pkill, which
// sends SIGTERM by default) breaks out of the streaming loop into the
// normal pause -> get_realtime_model_mesh -> stop -> session_destroy ->
// context_destroy teardown below, instead of abruptly killing the worker
// mid-scan. Skipping that teardown is exactly what has left this hardware
// needing a physical power cycle before (see README.md "Known issues") —
// a signal handler doing real work isn't async-signal-safe in general, but
// setting a sig_atomic_t flag is explicitly allowed and is all this does.
volatile std::sig_atomic_t g_stop_requested = 0;

void OnStopSignal(int) {
    g_stop_requested = 1;
}

void OnDeviceMessage(ObScanDeviceMessage msg) {
    // msg_id=0 with non-NULL data is the confirmed real connect-event shape
    // (see obscan_api.h / live_capture_test.cpp).
    if (msg.msg_id == 0 && msg.data != nullptr) {
        g_got_connect_message = true;
    }
}

void OnDataMessage(ObScanDataMessage msg) {
    if (msg.msg_id == OB_SCAN_MESSAGE_ID_FRAME_READY) {
        ++g_frame_ready_count;
    }
}

void OnProcessMessage(ObScanProcessMessage) {}
void OnPreviewStatus(ObScanStatus) {}
void OnPreviewProgress(float) {}

const char* StatusName(ObScanStatus status) {
    switch (status) {
        case OB_SCAN_SUCCESS:
            return "OB_SCAN_SUCCESS";
        case OB_SCAN_ERROR_INVALID_ARGUMENT:
            return "OB_SCAN_ERROR_INVALID_ARGUMENT";
        case OB_SCAN_ERROR_CAMERA_NOT_REGISTER:
            return "OB_SCAN_ERROR_CAMERA_NOT_REGISTER";
        case OB_SCAN_ERROR_DEVICE_DISCONNECT:
            return "OB_SCAN_ERROR_DEVICE_DISCONNECT";
        case OB_SCAN_ERROR_DEVICE_BUSY:
            return "OB_SCAN_ERROR_DEVICE_BUSY";
        case OB_SCAN_ERROR_UNSUPPORTED_CONFIGURATION:
            return "OB_SCAN_ERROR_UNSUPPORTED_CONFIGURATION";
        default:
            return "OB_SCAN_ERROR_(see obscan_api.h)";
    }
}

void Report(const char* step, ObScanStatus status) {
    std::printf("[worker] %-45s -> %s (%d)\n", step, StatusName(status), static_cast<int>(status));
    std::fflush(stdout);
}

int RunLiveServer(const std::string& library_path, uint16_t port) {
    std::signal(SIGINT, &OnStopSignal);
    std::signal(SIGTERM, &OnStopSignal);

    std::printf("[worker] Loading OBScan SDK from: %s\n", library_path.c_str());
    std::printf("[worker] Press Ctrl+C to stop early — this will run the normal pause/stop/destroy teardown\n");
    std::printf("[worker] instead of being killed mid-scan.\n");
    std::fflush(stdout);

    openscanner::ObscanLibrary lib(library_path);

    std::string algo_dir = openscanner::ObscanLibrary::FindStreamingAssetsDirForLibraryPath(library_path);
    ObScanContextConfig context_config = {};
    context_config.input_algo_dependent_files_dir = algo_dir.c_str();
    context_config.output_ob_sdk_log_dir = "/tmp/openscanner_live_server_sdk_log";
    context_config.output_ob_scan_log_dir = "/tmp/openscanner_live_server_scan_log";
    context_config.output_computer_performance_fps_dir = "/tmp/openscanner_live_server_fps_log";
    context_config.output_windows_gpu_info_dir = "";

    ObScanContextHandle context = nullptr;
    ObScanStatus status = lib.obscan_context_create(&context, &context_config);
    Report("obscan_context_create", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    ObScanDeviceManagementHandle device_mgmt = nullptr;
    status = lib.obscan_context_get_device_management(context, &device_mgmt);
    Report("obscan_context_get_device_management", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    ObScanDataMessageListener data_listener = {};
    data_listener.onMessage = &OnDataMessage;
    status = lib.obscan_context_add_data_msg_listener(context, &data_listener);
    Report("obscan_context_add_data_msg_listener", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    ObScanProcessMessageListener process_listener = {};
    process_listener.onMessage = &OnProcessMessage;
    status = lib.obscan_context_add_process_msg_listener(context, &process_listener);
    Report("obscan_context_add_process_msg_listener", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    ObScanDeviceMessageListener listener = {};
    listener.onMessage = &OnDeviceMessage;
    status = lib.obscan_device_management_add_device_msg_listener(device_mgmt, &listener);
    Report("obscan_device_management_add_device_msg_listener", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    for (int attempt = 0; attempt < kDeviceWaitAttempts && !g_got_connect_message; ++attempt) {
#if defined(__APPLE__)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, kDeviceWaitIntervalMs / 1000.0, false);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(kDeviceWaitIntervalMs));
#endif
    }

    int32_t device_num = 0;
    status = lib.obscan_device_management_get_device_num(device_mgmt, &device_num);
    std::printf("[worker] obscan_device_management_get_device_num -> %s, count=%d\n", StatusName(status),
                device_num);
    std::fflush(stdout);
    if (device_num == 0) {
        std::printf("[worker] No device detected — nothing to stream.\n");
        return 2;
    }

    ObScanSessionConfig session_config = {};
    session_config.output_dir = "/tmp/openscanner_live_server_session";
    session_config.name = "openscanner_scan_server_live";
    session_config.is_import_mesh_file = false;

    ObScanSessionHandle session = nullptr;
    status = lib.obscan_context_create_session(context, &session_config, &session);
    Report("obscan_context_create_session", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    // Ground-truth config, same as live_capture_test (see that file for
    // the full history of how each field was determined).
    ObScanConfig scan_config = {};
    scan_config.mode_config.post_texture = OB_SCAN_NO_TEXTURE;
    scan_config.mode_config.frame_rate = OB_SCAN_FRAME_RATE_AUTO;
    scan_config.mode_config.projection = OB_SCAN_BLUE_LINE_LASER;
    scan_config.mode_config.scan_mode = OB_SCAN_MODE_OBJECT_MEDIUM;
    scan_config.mode_config.accuracy = OB_SCAN_ACCURACY_HIGH;
    scan_config.mode_config.fuse_mode = OB_SCAN_FUSE_MODE_MARKER;
    scan_config.mode_config.scan_fov = OB_SCAN_FOV_NORMAL;
    scan_config.mode_config.object_material = OB_SCAN_OBJECT_MATERIAL_STANDARD;
    scan_config.mode_config.light_type = OB_SCAN_LIGHT_TYPE_CROSS;
    scan_config.mode_config.scan_type = OB_SCAN_CLOUD_FUSED;
    scan_config.mode_config.detail_voxel_size = 0.05;
    scan_config.mode_config.voxel_size = 0.1;
    scan_config.name = "openscanner_scan_server_live_scan";

    ObScanScanHandle scan = nullptr;
    status = lib.obscan_session_create_scan(session, &scan_config, &scan);
    Report("obscan_session_create_scan", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    status = lib.obscan_scan_preview(scan, &OnPreviewProgress, &OnPreviewStatus);
    Report("obscan_scan_preview", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    status = lib.obscan_scan_config_combined_exposure_and_laser_current(scan, 900);
    Report("obscan_scan_config_combined_exposure_and_laser_current", status);

    for (int i = 0; i < 100 && g_frame_ready_count.load() < kWantStableFrames; ++i) {
#if defined(__APPLE__)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, false);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
#endif
    }
    std::printf("[worker] frame_ready_count=%d\n", g_frame_ready_count.load());
    std::fflush(stdout);

    status = lib.obscan_scan_start(scan);
    Report("obscan_scan_start", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    openscanner::WsBroadcastServer server;
    if (!server.Start(port)) {
        std::fprintf(stderr, "[worker] Failed to start WebSocket server on port %u (port in use?)\n", port);
        // Still try to tear the scan down cleanly below rather than leaving
        // the device mid-scan.
    } else {
        std::printf("[worker] Streaming live scan frames on ws://0.0.0.0:%u (open ui/index.html)\n", port);
        std::printf(
            "[worker] Note: obscan_scan_get_raw_cloud has never been observed to return nonzero points while\n"
            "[worker] actively scanning on this hardware (see README.md). Expect 0 points per live frame until\n"
            "[worker] the final pause -> get_realtime_model_mesh snapshot broadcast at the end of this session.\n");
    }
    std::fflush(stdout);

    uint32_t frame_id = 0;
    const int total_ticks = (g_live_session_seconds * 1000) / kLiveTickIntervalMs;
    uint32_t max_points_seen = 0;
    for (int tick = 0; tick < total_ticks; ++tick) {
        if (g_stop_requested) {
            std::printf("[worker] Stop requested (Ctrl+C) — ending the live loop and tearing down cleanly.\n");
            std::fflush(stdout);
            break;
        }
#if defined(__APPLE__)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, kLiveTickIntervalMs / 1000.0, false);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(kLiveTickIntervalMs));
#endif
        // No dedicated auto/manual exposure toggle exists in the ABI; the
        // device's own auto-exposure decays a one-time value back toward
        // its own target within ~15s (see obscan_api.h), so re-assert
        // every tick to hold it.
        lib.obscan_scan_config_combined_exposure_and_laser_current(scan, 900);

        ObScanPointClouds cloud = {};
        status = lib.obscan_scan_get_raw_cloud(scan, &cloud);
        if (status == OB_SCAN_SUCCESS) {
            max_points_seen = std::max(max_points_seen, cloud.num_points);
            if (cloud.num_points > 0 && cloud.points != nullptr) {
                std::vector<float> xyz(static_cast<size_t>(cloud.num_points) * 3);
                for (uint32_t i = 0; i < cloud.num_points; ++i) {
                    xyz[i * 3 + 0] = cloud.points[i].x;
                    xyz[i * 3 + 1] = cloud.points[i].y;
                    xyz[i * 3 + 2] = cloud.points[i].z;
                }
                std::vector<uint8_t> buf = BuildFrame(frame_id++, NowMs(), xyz.data(), cloud.num_points);
                server.BroadcastBinary(buf.data(), buf.size());
            } else {
                // Broadcast an empty frame too, so the UI's data-fps/HUD
                // reflects a real, live connection rather than going idle.
                std::vector<uint8_t> buf = BuildFrame(frame_id++, NowMs(), nullptr, 0);
                server.BroadcastBinary(buf.data(), buf.size());
            }
        }

        if (tick % 50 == 0) {  // ~every 5s
            std::printf("[worker] tick %d/%d: raw_cloud num_points=%u (max seen so far: %u), clients=%zu\n", tick,
                        total_ticks, cloud.num_points, max_points_seen, server.ConnectedClientCount());
            std::fflush(stdout);
        }
    }

    status = lib.obscan_scan_pause(scan);
    Report("obscan_scan_pause", status);

    {
        ObScanTriangleMesh mesh = {};
        ObScanStatus mesh_status = lib.obscan_scan_get_realtime_model_mesh(scan, &mesh);
        std::printf("[worker] obscan_scan_get_realtime_model_mesh -> %s (%d), num_vertices=%u, num_triangles=%u\n",
                    StatusName(mesh_status), static_cast<int>(mesh_status), mesh.num_vertices, mesh.num_triangles);
        std::fflush(stdout);
        if (mesh_status == OB_SCAN_SUCCESS && mesh.num_vertices > 0 && mesh.vertices != nullptr) {
            std::vector<float> xyz(static_cast<size_t>(mesh.num_vertices) * 3);
            for (uint32_t i = 0; i < mesh.num_vertices; ++i) {
                xyz[i * 3 + 0] = mesh.vertices[i].x;
                xyz[i * 3 + 1] = mesh.vertices[i].y;
                xyz[i * 3 + 2] = mesh.vertices[i].z;
            }
            std::vector<uint8_t> buf = BuildFrame(frame_id++, NowMs(), xyz.data(), mesh.num_vertices);
            server.BroadcastBinary(buf.data(), buf.size());
            std::printf("[worker] Broadcast final reconstructed mesh as one point-cloud frame (%u points).\n",
                        mesh.num_vertices);
            // Give any connected client a moment to actually receive it
            // before teardown starts closing sockets.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    for (int i = 0; i < 25; ++i) {
#if defined(__APPLE__)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, false);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
#endif
    }

    status = lib.obscan_scan_stop(scan, &OnPreviewProgress, &OnPreviewStatus, /*is_block=*/true);
    Report("obscan_scan_stop", status);

    status = lib.obscan_session_destroy(&session);
    Report("obscan_session_destroy", status);

    status = lib.obscan_context_destroy(&context);
    Report("obscan_context_destroy", status);

    server.Stop();
    return 0;
}

int RunLiveMode(int argc, char** argv) {
    // Ctrl+C in an interactive terminal delivers SIGINT to this launcher
    // too (same process group as the worker). Ignore it here rather than
    // dying immediately — RunSelfAsWorker below just keeps waiting, and
    // the worker's own handler (see OnStopSignal) is what actually does
    // the graceful pause/stop/destroy teardown before it exits.
    std::signal(SIGINT, SIG_IGN);

    // scan_server --live [port] [library_path] [session_seconds]
    uint16_t port = 8787;
    std::string library_path;
    int session_seconds = g_live_session_seconds;
    if (argc > 2) port = static_cast<uint16_t>(std::atoi(argv[2]));
    if (argc > 3) library_path = argv[3];
    if (argc > 4) session_seconds = std::atoi(argv[4]);
    if (library_path.empty()) library_path = openscanner::ObscanLibrary::FindDefaultLibraryPath();
    if (library_path.empty()) {
        std::fprintf(stderr, "No OBScan SDK library found. Pass the path explicitly: scan_server --live <port> <path>\n");
        return 1;
    }

#if defined(__APPLE__) && defined(VULKAN_DISABLE_INTERPOSE_DYLIB)
    // See tools/live_capture_test/main.cpp and vulkan_disable_interpose.c
    // for the full story — required for obscan_context_destroy to tear
    // down cleanly instead of crashing/hanging on this hardware.
    std::string moltenvk_path;
    {
        size_t slash = library_path.find_last_of('/');
        if (slash != std::string::npos) {
            moltenvk_path = library_path.substr(0, slash + 1) + "libMoltenVK.dylib";
        }
    }
    if (!moltenvk_path.empty()) {
        std::string insert = moltenvk_path + ":" + VULKAN_DISABLE_INTERPOSE_DYLIB;
        setenv("DYLD_INSERT_LIBRARIES", insert.c_str(), /*overwrite=*/1);
    }
#endif

    std::printf("[launcher] Running live scan server in a worker process (up to %ds)...\n", session_seconds);
    std::fflush(stdout);
    int worker_exit_code = -1;
    std::string crash_reason;
    // Budget: device-connect (<=6s) + frame-ready wait (<=20s) +
    // session_seconds of streaming + pause/mesh/stop settle (~10s) +
    // context_destroy. Generous margin past that, same reasoning as
    // live_capture_test's own timeout.
    const int timeout_seconds = session_seconds + 120;
    bool worker_ok = openscanner::RunSelfAsWorker(
        {kWorkerFlag, std::to_string(port), library_path, std::to_string(session_seconds)}, &worker_exit_code,
        &crash_reason, timeout_seconds);
    if (worker_ok) {
        std::printf("[launcher] Worker exited normally with code %d.\n", worker_exit_code);
        return worker_exit_code;
    }
    std::printf("[launcher] Worker crashed or timed out inside the vendor SDK (%s).\n", crash_reason.c_str());
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == kWorkerFlag) {
        uint16_t port = static_cast<uint16_t>(std::atoi(argv[2]));
        std::string library_path = argc > 3 ? argv[3] : "";
        if (argc > 4) g_live_session_seconds = std::atoi(argv[4]);
        return RunLiveServer(library_path, port);
    }

    if (argc >= 2 && std::string(argv[1]) == "--live") {
        return RunLiveMode(argc, argv);
    }

    uint16_t port = argc > 1 ? static_cast<uint16_t>(std::atoi(argv[1])) : 8787;
    RunSyntheticServer(port);
    return 0;
}
