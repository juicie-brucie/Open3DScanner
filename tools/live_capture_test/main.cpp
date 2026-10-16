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
//
// Environment:
//   OPENSCANNER_EXPOSURE_SWEEP=1  instead of holding one fixed exposure,
//                                 step obscan_scan_config_combined_
//                                 exposure_and_laser_current across its
//                                 full [1, 1000] range (~3s per step),
//                                 polling obscan_scan_get_raw_cloud at
//                                 each step and reporting which value(s)
//                                 actually produced points — for
//                                 characterizing marker detection on a
//                                 device/lighting setup we haven't
//                                 tested before, rather than guessing a
//                                 single value again.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "openscanner/obscan_loader.h"
#include "openscanner/worker_process.h"

namespace {

constexpr const char* kWorkerFlag = "--live-capture-worker";
// 15 * 300ms (4.5s) was fine for exercising the API shape, but nowhere
// near enough real time for a human to actually move the scanner around
// an object — bumped to a real scan-length window (~30s).
constexpr int kPollCount = 60;
constexpr int kPollIntervalMs = 500;
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
    // Marker mode reliably gets real per-frame laser points (confirmed:
    // hundreds of nonzero "lasere total_points_num" lines) — Markerless
    // didn't produce any laser points at all in its own retest, with
    // OrbbecFusion explicitly logging "total is zero". Back to Marker;
    // the raw_cloud/mesh-never-reflecting-real-points question is
    // tracked separately (see README) and may need
    // obscan_session_import_marker_framework_datas, which we don't call.
    scan_config.mode_config.fuse_mode = OB_SCAN_FUSE_MODE_MARKER;
    scan_config.mode_config.scan_fov = OB_SCAN_FOV_NORMAL;                  // LaserScanFOVProperty: 0
    scan_config.mode_config.object_material = OB_SCAN_OBJECT_MATERIAL_STANDARD;  // ObjectMaterialProperty: 0
    // CROSS (ScanLightTypeProperty: 1) wasn't actually the cause of the
    // obscan_scan_start hang — PARALLEL hung identically. Restored.
    // Cross (ScanLightTypeProperty: 1, real ground truth) is NOT the
    // cause of the light_type-0/BLAE-skip/zero-points runs — a log from
    // earlier tonight (right after a power cycle, before any crashes
    // since) used this exact value and got real non-zero laser point
    // counts within 10ms of obscan_scan_start. Switching to Parallel
    // and still getting zero points confirmed it: this is the same
    // stale-device-state issue as before, re-triggered by crashes since
    // the last power cycle, not a config field.
    scan_config.mode_config.light_type = OB_SCAN_LIGHT_TYPE_CROSS;
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
    // obscan_scan_preview takes BOTH a progress and a status callback —
    // passing only one (or NULL) misaligned the real signature; see
    // obscan_api.h for how we found this.
    status = lib.obscan_scan_preview(scan, &OnPreviewProgress, &OnPreviewStatus);
    Report("obscan_scan_preview", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    // Confirmed against the real CrealityScan app: with this device's
    // default/auto exposure (~500 on a 1-1000 scale), physical markers
    // for fuse_mode=Marker went completely undetected — the real app
    // only found them after the user turned exposure up well above
    // default. Set it high before waiting for frames, so every frame
    // used for marker detection is already bright enough.
    status = lib.obscan_scan_config_combined_exposure_and_laser_current(scan, 900);
    Report("obscan_scan_config_combined_exposure_and_laser_current", status);

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

    // OPENSCANNER_EXPOSURE_SWEEP=1: for future marker-detection testing
    // on a device/lighting setup we haven't characterized yet, step
    // obscan_scan_config_combined_exposure_and_laser_current through a
    // range instead of holding one fixed value, polling raw_cloud at
    // each step and summarizing which value(s) actually produced
    // points. Markers have no dedicated "detected" log line we've
    // found, so a growing raw_cloud (now that obscan_scan_stop/pause
    // actually work — see obscan_api.h) is the best proxy we have.
    const char* sweep_env = std::getenv("OPENSCANNER_EXPOSURE_SWEEP");
    if (sweep_env != nullptr && std::string(sweep_env) == "1") {
        // Range is [1, 1000] per OBScanRealTimeConfig.combined_exposure_
        // and_laser_current (obscan_api.h); ground truth default/last-
        // used was ~500-540, and marker detection needed "pretty high"
        // per interactive testing — step across the full range to map
        // it properly instead of guessing a single value again.
        const int kSweepValues[] = {100, 200, 300, 400, 500, 600, 700, 800, 900, 1000};
        constexpr int kHoldMs = 3000;
        constexpr int kHoldPolls = 6;  // ~500ms apart, within kHoldMs.
        int best_value = -1;
        uint32_t best_points = 0;
        for (int sweep_value : kSweepValues) {
            status = lib.obscan_scan_config_combined_exposure_and_laser_current(scan, sweep_value);
            std::printf("[worker] exposure_sweep: set combined_exposure_and_laser_current=%d -> %s\n", sweep_value,
                        StatusName(status));
            std::fflush(stdout);
            uint32_t max_points_this_step = 0;
            for (int poll = 0; poll < kHoldPolls; ++poll) {
#if defined(__APPLE__)
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, (kHoldMs / kHoldPolls) / 1000.0, false);
#else
                std::this_thread::sleep_for(std::chrono::milliseconds(kHoldMs / kHoldPolls));
#endif
                ObScanPointClouds cloud = {};
                status = lib.obscan_scan_get_raw_cloud(scan, &cloud);
                std::printf("[worker] exposure_sweep: value=%d poll=%d/%d obscan_scan_get_raw_cloud -> %s, "
                            "num_points=%u\n",
                            sweep_value, poll + 1, kHoldPolls, StatusName(status), cloud.num_points);
                std::fflush(stdout);
                if (status == OB_SCAN_SUCCESS) {
                    max_points_this_step = std::max(max_points_this_step, cloud.num_points);
                }
            }
            if (max_points_this_step > best_points) {
                best_points = max_points_this_step;
                best_value = sweep_value;
            }
        }
        std::printf("[worker] exposure_sweep: best combined_exposure_and_laser_current=%d (%u points seen)\n",
                    best_value, best_points);
        std::fflush(stdout);
    } else {
        for (int i = 0; i < kPollCount; ++i) {
#if defined(__APPLE__)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, kPollIntervalMs / 1000.0, false);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
#endif
            // Setting this once wasn't enough — the native log showed
            // exposure_time decaying from our requested 900 down to a
            // steady 500 over ~15s (auto-exposure, IrAutoMode,
            // overriding it back to its own target). No dedicated
            // auto/manual toggle call exists in the ABI, so re-assert
            // every poll instead.
            lib.obscan_scan_config_combined_exposure_and_laser_current(scan, 900);

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
    }

    // OBScanTarget.PauseScan() takes no parameters — see obscan_api.h.
    // Our earlier is_stop_stream bool was invented, not real ABI.
    status = lib.obscan_scan_pause(scan);
    Report("obscan_scan_pause", status);

    // obscan_scan_get_realtime_model_mesh is only valid while paused —
    // see obscan_api.h. Calling it during the active poll loop above
    // reliably hit OB_SCAN_ERROR_MULTI_CALLED ("get model mesh must
    // first pause scan !"); read it here instead, now that it's legal.
    {
        ObScanTriangleMesh mesh = {};
        ObScanStatus mesh_status = lib.obscan_scan_get_realtime_model_mesh(scan, &mesh);
        std::printf("[worker] obscan_scan_get_realtime_model_mesh -> %s (%d), num_vertices=%u, num_triangles=%u\n",
                    StatusName(mesh_status), static_cast<int>(mesh_status), mesh.num_vertices, mesh.num_triangles);
        std::fflush(stdout);
    }

    // The real app's own captured call log had a ~5s gap between pause
    // and stop (20:35:44 -> 20:35:49); tried adding the same gap here as
    // a first hypothesis for OB_SCAN_ERROR_INVALID_SCAN (845283618), but
    // it made no difference — the real cause turned out to be a wrong
    // obscan_scan_stop signature (wrong arg count AND order; see
    // obscan_api.h). Kept anyway: cheap, and harmless if unnecessary.
    for (int i = 0; i < 25; ++i) {
#if defined(__APPLE__)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, false);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
#endif
    }

    // CORRECTED (again): real shape is (scan, progress_cb, status_cb,
    // is_block) — see obscan_api.h for the full story of how the 2-arg
    // version that was briefly here got caught as a regression (it
    // returned SUCCESS but did nothing real, leaving both destroys
    // refused with MULTI_CALLED). NULL callbacks are rejected.
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
    // here without returning, building Creality's SuperPoint ncnn+
    // Vulkan network it never uses — the vulkan_disable_interpose fix is
    // meant to make that fail fast instead.
    //
    // CAVEAT this timing alone can't resolve: every "confirmed working,
    // 0ms" run so far used the now-known-broken 2-arg obscan_scan_stop,
    // which never actually stopped anything — session_destroy then hit
    // OB_SCAN_ERROR_MULTI_CALLED and refused immediately, before
    // context_destroy could ever reach the SuperPoint/Vulkan path at
    // all. A 0ms destroy in those runs proves nothing about whether the
    // Vulkan fix works; it needs re-validating now that stop() actually
    // stops the scan and destroy has real work to do.
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

#if defined(__APPLE__) && defined(VULKAN_DISABLE_INTERPOSE_DYLIB)
    // See vulkan_disable_interpose.c for the full story. Two things must
    // both be present by the time our interposer binds, or dyld aborts
    // with "symbol not found in flat namespace": MoltenVK's real
    // vkCreateInstance/vkCreateDevice (it's only otherwise loaded much
    // later, transitively, when the worker dlopen()s liborbbec_scan.dylib)
    // and our own interposer. DYLD_INSERT_LIBRARIES loads a colon-
    // separated list in order, so inserting CrealityScan's own,
    // unmodified libMoltenVK.dylib (a sibling of the obscan library)
    // first makes its symbols available before our interposer needs
    // them — we're pointing dyld at a vendor file that already exists
    // on disk, not copying or altering it.
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
        std::printf("[launcher] DYLD_INSERT_LIBRARIES=%s\n", insert.c_str());
    } else {
        std::printf("[launcher] Could not derive libMoltenVK.dylib path from '%s'; skipping Vulkan-disable fix.\n",
                     library_path.c_str());
    }
#endif

    std::printf("[launcher] Running live capture session in a worker process...\n");
    int worker_exit_code = -1;
    std::string crash_reason;
    // Budget: device-connect wait (<=6s) + frame-ready wait (<=20s) +
    // kPollCount polls (~30s) + pause->stop settle (5s) + a now-real
    // session_destroy/context_destroy. 90s wasn't enough once
    // obscan_scan_stop actually stopped something for real (it was only
    // ever "fast" before because a broken stop() left both destroys
    // refused with MULTI_CALLED, never reaching real teardown work) —
    // bumped with real margin while staying far short of the 15+ minutes
    // a stuck context_destroy has been confirmed to run unbounded on
    // this hardware (see worker_process.h and README.md "Known issues").
    // Past this we treat it the same as a crash rather than keep waiting.
    constexpr int kWorkerTimeoutSeconds = 180;
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
