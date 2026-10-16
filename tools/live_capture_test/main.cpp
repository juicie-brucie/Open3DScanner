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

#include <atomic>
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
constexpr int kDeviceWaitAttempts = 30;    // ~6s at 200ms/attempt.
constexpr int kDeviceWaitIntervalMs = 200;

// obscan_device_management_add_device_msg_listener() is the actual trigger
// for device detection — see the ObScanDeviceMessage* block in obscan_api.h.
// Without a registered listener, the vendor SDK never starts its IOKit
// device-watcher thread and obscan_device_management_get_device_num()
// stays at 0 forever, no matter how much the run loop gets pumped. The
// callback must be a plain C function pointer (no captures), so state is
// communicated back via these globals; that's fine here because this
// process only ever runs one live-capture session.
std::atomic<bool> g_got_connect_message{false};

void OnDeviceMessage(ObScanDeviceMessage msg) {
    std::printf("[worker] device message: msg_id=%d data=%p\n", msg.msg_id, msg.data);
    std::fflush(stdout);
    // msg_id=0 with a non-NULL data pointer is the confirmed real
    // connect-event shape. The first callback on every run so far has
    // fired immediately with msg_id=308412672/data=NULL — treated as a
    // non-actionable registration ack, not a connect event.
    if (msg.msg_id == 0 && msg.data != nullptr) {
        g_got_connect_message = true;
    }
}

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
    std::fflush(stdout);  // Survive a crash on the very next call.
}

void OnPreviewStatus(ObScanStatus status) {
    std::printf("[worker] preview status callback: %s (%d)\n", StatusName(status), static_cast<int>(status));
    std::fflush(stdout);
}

void OnPreviewProgress(float progress) {
    std::printf("[worker] preview progress callback: %.3f\n", progress);
    std::fflush(stdout);
}

std::atomic<int> g_frame_ready_count{0};

void OnDataMessage(ObScanDataMessage msg) {
    std::printf("[worker] data message: msg_id=%d data=%p\n", msg.msg_id, msg.data);
    std::fflush(stdout);
    if (msg.msg_id == OB_SCAN_MESSAGE_ID_FRAME_READY) {
        ++g_frame_ready_count;
    }
}

void OnProcessMessage(ObScanProcessMessage msg) {
    std::printf("[worker] process message: msg_id=%d data=%p\n", msg.msg_id, msg.data);
    std::fflush(stdout);
}

int RunLiveCapture(const std::string& library_path) {
    std::printf("[worker] Loading OBScan SDK from: %s\n", library_path.c_str());
    std::fflush(stdout);

    openscanner::ObscanLibrary lib(library_path);
    std::printf("[worker] Resolved all symbols OK.\n");
    std::fflush(stdout);

    // Leaving this empty crashes the vendor SDK (uncaught std::invalid_
    // argument, "... the file is not exists !") the moment it needs its
    // GPU shader or ML model files — it needs to point at CrealityScan's
    // own Unity StreamingAssets directory (dggv2_laser_macos.metallib,
    // rot_model_core_1.bin, tex_model_core_1.bin all live there).
    std::string algo_dir = openscanner::ObscanLibrary::FindStreamingAssetsDirForLibraryPath(library_path);
    std::printf("[worker] input_algo_dependent_files_dir: %s\n", algo_dir.empty() ? "(not found!)" : algo_dir.c_str());
    std::fflush(stdout);

    ObScanContextConfig context_config = {};
    context_config.input_algo_dependent_files_dir = algo_dir.c_str();
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
    if (status != OB_SCAN_SUCCESS) return 1;

    // The real app always registers these two before touching the device
    // or session; without them obscan_scan_preview() later fails with
    // OB_SCAN_ERROR_LISTENER_NOT_FOUND.
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

    // Registering a device-message listener is what actually starts the
    // vendor SDK's IOKit device-watcher thread (confirmed via Frida
    // tracing — see obscan_api.h). Without this, get_device_num() stays
    // at 0 forever no matter how much the run loop gets pumped, which is
    // why earlier runs of this tool never saw an already-connected
    // device. See README.md "Known issues".
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

    // Ground truth, not another bisection guess: this device's own
    // scanParamConfig.dat (a real CrealityScan project's saved
    // LaserScanParam, dumped as JSON by the real app) gives the actual
    // last-used values for this exact Raptor Pro's blue-line-laser mode.
    // Our earlier bisected voxel_size=0.05/detail_voxel_size=0.001 were
    // both off — 0.05 happened to clear the native floor but sits below
    // the app's own documented valid range (min 0.1), and light_type/
    // accuracy were just guesses.
    ObScanConfig scan_config = {};
    scan_config.mode_config.post_texture = OB_SCAN_NO_TEXTURE;      // TextureModeProperty: 1
    scan_config.mode_config.frame_rate = OB_SCAN_FRAME_RATE_AUTO;    // FrameRateProperty: 0
    // The connected Raptor Pro reports depthMode "Laser Lines Scan" (see
    // the PioneerDevice.cpp log line at session creation), i.e. a blue-
    // line-laser projector, not an infrared structured-light one.
    scan_config.mode_config.projection = OB_SCAN_BLUE_LINE_LASER;    // ScanProjectionProperty: 1
    scan_config.mode_config.scan_mode = OB_SCAN_MODE_OBJECT_MEDIUM;  // not present in scanParamConfig.dat; kept as-is
    scan_config.mode_config.accuracy = OB_SCAN_ACCURACY_HIGH;  // ScanAccuracyProperty: 1 (ground truth)
    // LaserScanFuseModeProperty: 2 == OB_SCAN_FUSE_MODE_MARKER — this
    // device's real saved session used physical markers (confirmed by
    // scanDataConfig.dat's "remove_markers": true during optimization),
    // not Markerless.
    scan_config.mode_config.fuse_mode = OB_SCAN_FUSE_MODE_MARKER;
    scan_config.mode_config.scan_fov = OB_SCAN_FOV_NORMAL;                  // LaserScanFOVProperty: 0
    scan_config.mode_config.object_material = OB_SCAN_OBJECT_MATERIAL_STANDARD;  // ObjectMaterialProperty: 0
    // CROSS (ScanLightTypeProperty: 1) wasn't actually the cause of the
    // obscan_scan_start hang — PARALLEL hung identically. Restored.
    scan_config.mode_config.light_type = OB_SCAN_LIGHT_TYPE_CROSS;  // ScanLightTypeProperty: 1 (ground truth)
    scan_config.mode_config.scan_type = OB_SCAN_CLOUD_FUSED;         // LaserScanTypeProperty: 0
    scan_config.mode_config.detail_voxel_size = 0.05;  // DetailVoxelSizeProperty: 0.05 (ground truth)
    scan_config.mode_config.voxel_size = 0.1;          // VoxelSizeProperty: 0.1 (ground truth)
    // NOTE: a long isolation pass (varying accuracy, light_type, and
    // both voxel sizes one at a time, each rebuilt and retested against
    // the physical device) found that NONE of these mode_config fields
    // explain why preview stopped producing any data messages at all —
    // even restoring the exact combination that worked earlier in this
    // session (accuracy=Fast, light_type=Parallel, voxel_size=0.05,
    // detail_voxel_size=0.001) still produced zero data messages and a
    // hung obscan_scan_start. This points at device/USB state, not
    // configuration — see README.md "Known issues" for the likely
    // cause (repeated hard-kills of a worker mid-stream) and try a
    // physical USB reconnect before debugging config fields further.
    scan_config.name = "openscanner_live_scan";

    ObScanScanHandle scan = nullptr;
    status = lib.obscan_session_create_scan(session, &scan_config, &scan);
    Report("obscan_session_create_scan", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    // Captured from the real CrealityScan app's own obscan_* call log
    // during a live scan: it always calls obscan_scan_preview() (laser
    // on, camera streaming) and only calls obscan_scan_start() once
    // preview is actively flowing frames. Calling start() on a scan that
    // was never previewed is what produced the native "cannot called
    // start !" / OB_SCAN_ERROR_INVALID_ARGUMENT seen before this fix.
    // obscan_scan_preview takes BOTH a progress and a status callback,
    // like obscan_scan_stop — passing only one (or NULL) misaligned the
    // real signature; see obscan_api.h for how we found this.
    status = lib.obscan_scan_preview(scan, &OnPreviewProgress, &OnPreviewStatus);
    Report("obscan_scan_preview", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    // A fixed 2s wait wasn't enough — obscan_scan_start still failed with
    // "cannot called start !" while early frames were still being
    // dropped (size mismatches while the pipeline settles). Calling
    // start on the very first FRAME_READY wasn't enough either — it
    // raced the vendor SDK's own pipeline setup and crashed with an
    // uncaught "mutex lock failed" exception. Wait for several stable
    // FRAME_READY data messages instead, up to 20s.
    constexpr int kWantStableFrames = 10;
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

    // The real app's own captured call log had a ~5s gap between pause
    // and stop (20:35:44 -> 20:35:49); we called them back-to-back with
    // no delay, which is a likely cause of obscan_scan_stop's reliable
    // OB_SCAN_ERROR_INVALID_SCAN (845283618) here — stopping a stream
    // before its pause/teardown has actually settled. Give it the same
    // runway before calling stop.
    for (int i = 0; i < 25; ++i) {
#if defined(__APPLE__)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, false);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
#endif
    }

    // NULL callbacks aren't tolerated here either — see obscan_scan_preview
    // above, where NULL crashed outright. obscan_scan_stop silently
    // returned OB_SCAN_ERROR_INVALID_ARGUMENT with no native log line at
    // all here, consistent with an early argument-validation check.
    status = lib.obscan_scan_stop(scan, &OnPreviewProgress, &OnPreviewStatus, /*is_block=*/true);
    Report("obscan_scan_stop", status);

    auto session_destroy_start = std::chrono::steady_clock::now();
    status = lib.obscan_session_destroy(&session);
    auto session_destroy_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - session_destroy_start)
                                   .count();
    std::printf("[worker] obscan_session_destroy took %lldms\n", static_cast<long long>(session_destroy_ms));
    Report("obscan_session_destroy", status);

    // Known to crash inside the vendor SDK (see README.md "Known issues").
    // That's exactly why this whole function runs in an isolated worker.
    // Timed explicitly: a prior run burned 300%+ CPU for 3.5+ minutes
    // here without returning, after obscan_scan_stop had failed with
    // OB_SCAN_ERROR_INVALID_SCAN — unknown whether that's legitimate
    // (if expensive) cleanup or a real hang/loop. The pause->stop settle
    // delay above is meant to get stop to succeed first, which should
    // leave context_destroy with a cleanly-stopped scan to tear down
    // instead of one still partway through an interrupted stop.
    auto context_destroy_start = std::chrono::steady_clock::now();
    status = lib.obscan_context_destroy(&context);
    auto context_destroy_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - context_destroy_start)
                                   .count();
    std::printf("[worker] obscan_context_destroy took %lldms\n", static_cast<long long>(context_destroy_ms));
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
    // The happy-path budget here is roughly: device-connect wait (<=6s) +
    // frame-ready wait (<=20s) + 15 polls (~4.5s) + pause->stop settle
    // (5s) + session_destroy (sub-second) + context_destroy (normally
    // sub-second, or a fast crash). 90s leaves real margin over that ~40s
    // total while staying far short of the 15+ minutes a stuck
    // context_destroy has been confirmed to run unbounded on this
    // hardware (see worker_process.h and README.md "Known issues") — past
    // 90s we treat it the same as a crash rather than keep waiting.
    constexpr int kWorkerTimeoutSeconds = 90;
    bool worker_ok = openscanner::RunSelfAsWorker({kWorkerFlag, library_path}, &worker_exit_code, &crash_reason,
                                                   kWorkerTimeoutSeconds);

    if (worker_ok) {
        std::printf("[launcher] Worker exited normally with code %d.\n", worker_exit_code);
        return worker_exit_code;
    }

    std::printf("[launcher] Worker crashed or timed out inside the vendor SDK (%s). Launcher is unaffected.\n",
                crash_reason.c_str());
    return 2;
}
