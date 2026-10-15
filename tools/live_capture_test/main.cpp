// Exercises the newly-recovered session/scan lifecycle
// (obscan_context_create_session -> obscan_session_create_scan ->
// obscan_scan_start -> poll obscan_scan_get_raw_cloud -> obscan_scan_stop
// -> obscan_session_destroy -> obscan_context_destroy) against a real,
// physically-connected scanner. This is new, previously-unexercised
// binding surface — several of these calls (session/scan teardown,
// callback-accepting calls with NULL callbacks) have not been verified
// the way the bootstrap subset in tools/smoke_test has, so the whole
// session runs in a disposable worker process via the same pattern as
// smoke_test: if any step crashes inside the vendor SDK, only the worker
// dies and this prints what it learned before that happened.
//
// Usage:
//   live_capture_test [path-to-liborbbec_scan.dylib-or-lib_orbbec_scan.dll]

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "openscanner/obscan_loader.h"
#include "openscanner/worker_process.h"

namespace {

constexpr const char* kWorkerFlag = "--live-capture-worker";
constexpr int kPollCount = 15;
constexpr int kPollIntervalMs = 300;

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
        default:
            return "OB_SCAN_ERROR_(see obscan_api.h)";
    }
}

void Report(const char* step, ObScanStatus status) {
    std::printf("[worker] %-45s -> %s\n", step, StatusName(status));
    std::fflush(stdout);  // Survive a crash on the very next call.
}

int RunLiveCapture(const std::string& library_path) {
    std::printf("[worker] Loading OBScan SDK from: %s\n", library_path.c_str());
    std::fflush(stdout);

    openscanner::ObscanLibrary lib(library_path);
    std::printf("[worker] Resolved all symbols OK.\n");
    std::fflush(stdout);

    ObScanContextConfig context_config = {};
    context_config.input_algo_dependent_files_dir = "";
    context_config.output_ob_sdk_log_dir = "/tmp/openscanner_live_sdk_log";
    context_config.output_ob_scan_log_dir = "/tmp/openscanner_live_scan_log";
    context_config.output_computer_performance_fps_dir = "/tmp/openscanner_live_fps_log";
    context_config.output_windows_gpu_info_dir = "";

    ObScanContextHandle context = nullptr;
    ObScanStatus status = lib.obscan_context_create(&context, &context_config);
    Report("obscan_context_create", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    ObScanDeviceManagementHandle device_mgmt = nullptr;
    status = lib.obscan_context_get_device_management(context, &device_mgmt);
    Report("obscan_context_get_device_management", status);

    // Device discovery here is IOKit notification-driven, not purely
    // synchronous: confirmed via Frida tracing the real CrealityScan
    // binary that obscan_device_management_get_device_num() returns 0
    // immediately after context creation and only becomes nonzero once
    // something has pumped the process's run loop in between (IOKit
    // delivers device-arrival notifications through it). A bare sleep()
    // loop never services that run loop and stays stuck at 0 forever,
    // which is why earlier runs of this tool never saw an already-
    // connected device. See README.md "Known issues".
    int32_t device_num = 0;
    if (status == OB_SCAN_SUCCESS) {
        for (int attempt = 0; attempt < 30; ++attempt) {
            status = lib.obscan_device_management_get_device_num(device_mgmt, &device_num);
            if (device_num > 0) break;
#if defined(__APPLE__)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, false);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
#endif
        }
        std::printf("[worker] obscan_device_management_get_device_num -> %s, count=%d\n", StatusName(status),
                     device_num);
        std::fflush(stdout);
    }
    if (device_num == 0) {
        std::printf("[worker] No device detected — stopping here (nothing to scan).\n");
        std::fflush(stdout);
        return 2;
    }

    ObScanSessionConfig session_config = {};
    session_config.output_dir = "/tmp/openscanner_live_session";
    session_config.name = "openscanner_live_capture_test";
    session_config.is_import_mesh_file = false;

    ObScanSessionHandle session = nullptr;
    status = lib.obscan_context_create_session(context, &session_config, &session);
    Report("obscan_context_create_session", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    ObScanConfig scan_config = {};
    scan_config.mode_config.post_texture = OB_SCAN_NO_TEXTURE;
    scan_config.mode_config.frame_rate = OB_SCAN_FRAME_RATE_AUTO;
    scan_config.mode_config.projection = OB_SCAN_INFRARED;
    scan_config.mode_config.scan_mode = OB_SCAN_MODE_OBJECT_MEDIUM;
    scan_config.mode_config.accuracy = OB_SCAN_ACCURACY_FAST;
    scan_config.mode_config.fuse_mode = OB_SCAN_FUSE_MODE_GEOMETRY;
    scan_config.mode_config.scan_fov = OB_SCAN_FOV_NORMAL;
    scan_config.mode_config.object_material = OB_SCAN_OBJECT_MATERIAL_STANDARD;
    scan_config.mode_config.voxel_size = 0.003;
    scan_config.mode_config.light_type = OB_SCAN_LIGHT_TYPE_PARALLEL;
    scan_config.mode_config.scan_type = OB_SCAN_CLOUD_FUSED;
    scan_config.mode_config.detail_voxel_size = 0.001;
    scan_config.name = "openscanner_live_scan";

    ObScanScanHandle scan = nullptr;
    status = lib.obscan_session_create_scan(session, &scan_config, &scan);
    Report("obscan_session_create_scan", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    status = lib.obscan_scan_start(scan);
    Report("obscan_scan_start", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    for (int i = 0; i < kPollCount; ++i) {
#if defined(__APPLE__)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, kPollIntervalMs / 1000.0, false);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
#endif
        ObScanPointClouds cloud = {};
        status = lib.obscan_scan_get_raw_cloud(scan, &cloud);
        std::printf("[worker] poll %2d/%d: obscan_scan_get_raw_cloud -> %s, num_points=%u\n", i + 1, kPollCount,
                     StatusName(status), cloud.num_points);
        if (status == OB_SCAN_SUCCESS && cloud.num_points > 0 && cloud.points != nullptr) {
            const ObScanPoint& p = cloud.points[0];
            std::printf("[worker]   points[0] = (%.4f, %.4f, %.4f)\n", p.x, p.y, p.z);
        }
        std::fflush(stdout);
    }

    status = lib.obscan_scan_pause(scan, /*is_stop_stream=*/true);
    Report("obscan_scan_pause", status);

    status = lib.obscan_scan_stop(scan, /*progress_callback=*/nullptr, /*status_callback=*/nullptr,
                                   /*is_block=*/true);
    Report("obscan_scan_stop", status);

    status = lib.obscan_session_destroy(&session);
    Report("obscan_session_destroy", status);

    // Known to crash inside the vendor SDK (see README.md "Known issues").
    // That's exactly why this whole function runs in an isolated worker.
    status = lib.obscan_context_destroy(&context);
    Report("obscan_context_destroy", status);

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == kWorkerFlag) {
        return RunLiveCapture(argv[2]);
    }

    std::string library_path = argc > 1 ? argv[1] : openscanner::ObscanLibrary::FindDefaultLibraryPath();
    if (library_path.empty()) {
        std::fprintf(stderr, "No OBScan SDK library found. Pass the path explicitly.\n");
        return 1;
    }

    std::printf("[launcher] Running live capture session in a worker process...\n");
    int worker_exit_code = -1;
    std::string crash_reason;
    bool worker_ok = openscanner::RunSelfAsWorker({kWorkerFlag, library_path}, &worker_exit_code, &crash_reason);

    if (worker_ok) {
        std::printf("[launcher] Worker exited normally with code %d.\n", worker_exit_code);
        return worker_exit_code;
    }

    std::printf("[launcher] Worker crashed inside the vendor SDK (%s). Launcher is unaffected.\n",
                crash_reason.c_str());
    return 2;
}
