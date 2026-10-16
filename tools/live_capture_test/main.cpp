// Exercises the session/scan lifecycle against a real, physically-
// connected scanner:
//
//   context -> listeners -> wait for DEVICE_CONNECT -> session -> scan
//   -> preview -> wait for preview success + live frames -> start
//   -> scan for a while -> pause -> read cloud/mesh -> stop (wait for it)
//   -> session destroy -> context destroy
//
// The whole session runs in a disposable worker process (see
// worker_process.h): if a vendor call crashes, only the worker dies.
//
// The rule this file is organised around: once obscan_context_create has
// succeeded, every way out of the worker — a failed call, no device, a
// dead preview, Ctrl-C, the launcher's timeout — goes through Teardown(),
// which stops the scan and destroys the session and context in the order
// the vendor SDK accepts. The logs in logs/ show why: a worker that exits
// without the SDK's own stop/release path leaves the scanner streaming
// with nobody attached, and the *next* run's preview then degrades (stream
// restarts, dropped frames) until IR won't start at all and only a scanner
// power cycle helps.
//
// Usage:
//   live_capture_test [path-to-liborbbec_scan.dylib-or-lib_orbbec_scan.dll]
//
// Environment:
//   OPENSCANNER_SCAN_SECONDS        how long to scan after start (default 30)
//   OPENSCANNER_NO_VULKAN_INTERPOSE set to 1 to skip the macOS Vulkan
//                                   interposer (see vulkan_disable_interpose.c)

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <thread>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "openscanner/obscan_loader.h"
#include "openscanner/worker_process.h"

namespace {

constexpr const char* kWorkerFlag = "--live-capture-worker";

// Worker exit codes, so the launcher can say what actually happened.
enum WorkerExit : int {
    kExitOk = 0,
    kExitCallFailed = 1,          // A vendor call failed; device released.
    kExitNoDevice = 3,            // No DEVICE_CONNECT; nothing was opened.
    kExitNoFrames = 4,            // Preview delivered no frames; device released.
    kExitInterrupted = 5,         // SIGINT/SIGTERM; device released.
    kExitDisconnected = 6,        // Device unplugged mid-session.
    kExitTeardownIncomplete = 7,  // Stop/destroy refused: device may still be claimed.
};

// Time budgets. Device connect takes ~2.9s on every logged run; preview
// completes in 0.05-1.5s; on a healthy device 10 FRAME_READYs take ~0.3s
// after that, while a poisoned one never sends any (the vendor's own
// watchdog restarts IR at +3s and gives up after three tries).
constexpr int kDeviceWaitMs = 10000;
constexpr int kPreviewStatusWaitMs = 10000;
constexpr int kFirstFrameWaitMs = 8000;
constexpr int kStableFrameWaitMs = 5000;
constexpr int kWantStableFrames = 10;
constexpr int kStartAttempts = 3;
constexpr int kPollIntervalMs = 500;
constexpr int kPauseSettleMs = 2000;
constexpr int kStopWaitMs = 20000;
constexpr int kDestroyAttempts = 5;
constexpr int kDefaultScanSeconds = 30;
constexpr int32_t kExposure = 900;  // combined exposure/laser current, 1-1000

int ScanSeconds() {
    const char* s = std::getenv("OPENSCANNER_SCAN_SECONDS");
    int v = s != nullptr ? std::atoi(s) : 0;
    return v > 0 ? v : kDefaultScanSeconds;
}

// ---------------------------------------------------------------------------
// State shared with vendor threads. Callbacks fire on SDK-owned threads
// (and, for some, synchronously inside our own calls), so they only touch
// atomics: no stdio, no locks. Printing every data message — ~90/s while
// previewing — from inside the vendor's frame pipeline was how the old
// version of this tool logged; the main thread now reports counts instead.
// ---------------------------------------------------------------------------
volatile std::sig_atomic_t g_stop_requested = 0;

std::atomic<int> g_device_connects{0};
std::atomic<int> g_device_disconnects{0};
std::atomic<int> g_device_other{0};

std::atomic<int> g_depth_msgs{0};
std::atomic<int> g_ir_msgs{0};
std::atomic<int> g_rgb_msgs{0};
std::atomic<int> g_frame_ready{0};
std::atomic<int> g_other_data_msgs{0};
std::atomic<int> g_process_msgs{0};
std::atomic<int> g_last_process_msg_id{0};

// Separate status/progress slots per operation: the old tool routed stop's
// status through the preview callback, so its logs label stop results as
// "preview status".
constexpr int kNoStatus = -1;
std::atomic<int> g_preview_status{kNoStatus};
std::atomic<float> g_preview_progress{0.0f};
std::atomic<int> g_stop_status{kNoStatus};
std::atomic<float> g_stop_progress{0.0f};

void OnDeviceMessage(ObScanDeviceMessage msg) {
    if (msg.msg_id == OB_SCAN_MESSAGE_ID_DEVICE_CONNECT) {
        ++g_device_connects;
    } else if (msg.msg_id == OB_SCAN_MESSAGE_ID_DEVICE_DISCONNECT) {
        ++g_device_disconnects;
    } else {
        ++g_device_other;
    }
}

void OnDataMessage(ObScanDataMessage msg) {
    switch (msg.msg_id) {
        case OB_SCAN_MESSAGE_ID_NEW_DEPTH_IMAGE: ++g_depth_msgs; break;
        case OB_SCAN_MESSAGE_ID_NEW_IR_IMAGE: ++g_ir_msgs; break;
        case OB_SCAN_MESSAGE_ID_NEW_RGB_IMAGE: ++g_rgb_msgs; break;
        case OB_SCAN_MESSAGE_ID_FRAME_READY: ++g_frame_ready; break;
        default: ++g_other_data_msgs; break;
    }
}

void OnProcessMessage(ObScanProcessMessage msg) {
    g_last_process_msg_id = msg.msg_id;
    ++g_process_msgs;
}

void OnPreviewProgress(float progress) { g_preview_progress = progress; }
void OnPreviewStatus(ObScanStatus status) { g_preview_status = static_cast<int>(status); }
void OnStopProgress(float progress) { g_stop_progress = progress; }
void OnStopStatus(ObScanStatus status) { g_stop_status = static_cast<int>(status); }

// The SDK keeps these pointers and calls through them from its own threads
// for as long as the context lives — which, if context destroy is refused
// or crashes, is until the process exits. Stack locals (as before) went out
// of scope while the SDK still held them.
ObScanDataMessageListener g_data_listener = {&OnDataMessage};
ObScanProcessMessageListener g_process_listener = {&OnProcessMessage};
ObScanDeviceMessageListener g_device_listener = {&OnDeviceMessage};

void OnStopSignal(int) { g_stop_requested = 1; }

void InstallStopSignalHandlers() {
#if defined(_WIN32)
    std::signal(SIGINT, &OnStopSignal);
    std::signal(SIGTERM, &OnStopSignal);
#else
    struct sigaction sa = {};
    sa.sa_handler = &OnStopSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
#endif
}

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------
const char* StatusName(int status) {
    switch (status) {
        case OB_SCAN_SUCCESS: return "OB_SCAN_SUCCESS";
        case OB_SCAN_ERROR_INVALID_ARGUMENT: return "OB_SCAN_ERROR_INVALID_ARGUMENT";
        case OB_SCAN_ERROR_FATAL: return "OB_SCAN_ERROR_FATAL";
        case OB_SCAN_ERROR_DEVICE_DISCONNECT: return "OB_SCAN_ERROR_DEVICE_DISCONNECT";
        case OB_SCAN_ERROR_SESSION_PAUSED: return "OB_SCAN_ERROR_SESSION_PAUSED";
        case OB_SCAN_ERROR_SESSION_NOT_PAUSED: return "OB_SCAN_ERROR_SESSION_NOT_PAUSED";
        case OB_SCAN_ERROR_UNSUPPORTED_CONFIGURATION: return "OB_SCAN_ERROR_UNSUPPORTED_CONFIGURATION";
        case OB_SCAN_ERROR_DEADLINE_EXCEEDED: return "OB_SCAN_ERROR_DEADLINE_EXCEEDED";
        case OB_SCAN_ERROR_CAMERA_STREAMING_RETRY_FAILED: return "OB_SCAN_ERROR_CAMERA_STREAMING_RETRY_FAILED";
        case OB_SCAN_ERROR_LISTENER_NOT_FOUND: return "OB_SCAN_ERROR_LISTENER_NOT_FOUND";
        case OB_SCAN_ERROR_MULTI_CALLED: return "OB_SCAN_ERROR_MULTI_CALLED";
        case OB_SCAN_ERROR_CAMERA_NOT_REGISTER: return "OB_SCAN_ERROR_CAMERA_NOT_REGISTER";
        case OB_SCAN_ERROR_FILE_NOT_EXIST: return "OB_SCAN_ERROR_FILE_NOT_EXIST";
        case OB_SCAN_ERROR_HAVE_NOT_MARKER: return "OB_SCAN_ERROR_HAVE_NOT_MARKER";
        case OB_SCAN_ERROR_INVALID_SCAN: return "OB_SCAN_ERROR_INVALID_SCAN";
        case OB_SCAN_ERROR_DEVICE_BUSY: return "OB_SCAN_ERROR_DEVICE_BUSY";
        case OB_SCAN_ERROR_READ_DEVICE_PARAMS: return "OB_SCAN_ERROR_READ_DEVICE_PARAMS";
        case OB_SCAN_ERROR_SET_LED_LASER: return "OB_SCAN_ERROR_SET_LED_LASER";
        case OB_SCAN_ERROR_LOADING_INTR_LASER_PARAMS: return "OB_SCAN_ERROR_LOADING_INTR_LASER_PARAMS";
        case OB_SCAN_ERROR_SET_IR_MODE: return "OB_SCAN_ERROR_SET_IR_MODE";
        case OB_SCAN_ERROR_ALLOCATE_GPU_RESOURCES_FAILED: return "OB_SCAN_ERROR_ALLOCATE_GPU_RESOURCES_FAILED";
        case kNoStatus: return "(no callback)";
        default: return "OB_SCAN_ERROR_(unnamed, see obscan_api.h)";
    }
}

void Report(const char* step, ObScanStatus status) {
    std::printf("[worker] %-45s -> %s (%d)\n", step, StatusName(status), static_cast<int>(status));
    std::fflush(stdout);  // Survive a crash on the very next call.
}

#if defined(__GNUC__) || defined(__clang__)
void Say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#endif
void Say(const char* fmt, ...) {
    std::printf("[worker] ");
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
    std::fflush(stdout);
}

void ReportDataCounts(const char* when) {
    Say("%s: frame_ready=%d depth=%d ir=%d rgb=%d other=%d process_msgs=%d (last id %d)", when, g_frame_ready.load(),
        g_depth_msgs.load(), g_ir_msgs.load(), g_rgb_msgs.load(), g_other_data_msgs.load(), g_process_msgs.load(),
        g_last_process_msg_id.load());
}

// Waits up to `timeout_ms` for `done()`, servicing the main run loop on
// macOS (some vendor notifications are delivered through it). Returns
// false on timeout or if a stop was requested.
bool WaitFor(int timeout_ms, const std::function<bool()>& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!done()) {
        if (g_stop_requested || std::chrono::steady_clock::now() >= deadline) return done();
#if defined(__APPLE__)
        // Returns at once if nothing is attached to the run loop yet;
        // don't spin in that case.
        if (CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false) == kCFRunLoopRunFinished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
#endif
    }
    return true;
}

// Like WaitFor with nothing to wait for; still ends early on a stop request.
void Pump(int ms) {
    WaitFor(ms, [] { return false; });
}

void EnsureDir(const std::string& path) {
#if !defined(_WIN32)
    // Parent first; good enough for the fixed /tmp layouts used here.
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0) mkdir(path.substr(0, slash).c_str(), 0755);
    mkdir(path.c_str(), 0755);
#else
    (void)path;
#endif
}

// ---------------------------------------------------------------------------
// The capture session. Owns every vendor handle it creates and releases
// them all in Teardown(), whichever step failed.
// ---------------------------------------------------------------------------
class LiveCapture {
public:
    explicit LiveCapture(openscanner::ObscanLibrary* lib) : lib_(*lib) {}

    int Run(const std::string& library_path) {
        int result = Setup(library_path);
        if (result == kExitOk) result = Capture();
        if (g_stop_requested && result != kExitDisconnected) {
            Say("Stop requested (signal) — shutting down cleanly.");
            if (result == kExitOk) result = kExitInterrupted;
        }
        bool released = Teardown();
        if (!released && result != kExitNoDevice) return kExitTeardownIncomplete;
        return result;
    }

private:
    int Setup(const std::string& library_path) {
        // Leaving this empty crashes the vendor SDK (uncaught std::invalid_
        // argument, "... the file is not exists !") the moment it needs its
        // GPU shader or ML model files — it needs to point at CrealityScan's
        // own Unity StreamingAssets directory (dggv2_laser_macos.metallib,
        // rot_model_core_1.bin, tex_model_core_1.bin all live there).
        algo_dir_ = openscanner::ObscanLibrary::FindStreamingAssetsDirForLibraryPath(library_path);
        Say("input_algo_dependent_files_dir: %s", algo_dir_.empty() ? "(not found!)" : algo_dir_.c_str());

        // Per-run session directory: every run used to write the same
        // session name into the same directory as the last one.
        char stamp[32];
        std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
        session_dir_ = std::string("/tmp/openscanner_live_session/") + stamp;
        EnsureDir("/tmp/openscanner_live_sdk_log");
        EnsureDir("/tmp/openscanner_live_scan_log");
        EnsureDir("/tmp/openscanner_live_fps_log");
        EnsureDir(session_dir_);

        ObScanContextConfig context_config = {};
        context_config.input_algo_dependent_files_dir = algo_dir_.c_str();
        context_config.output_ob_sdk_log_dir = "/tmp/openscanner_live_sdk_log";
        context_config.output_ob_scan_log_dir = "/tmp/openscanner_live_scan_log";
        context_config.output_computer_performance_fps_dir = "/tmp/openscanner_live_fps_log";
        context_config.output_windows_gpu_info_dir = "";

        ObScanStatus status = lib_.obscan_context_create(&context_, &context_config);
        Report("obscan_context_create", status);
        if (status != OB_SCAN_SUCCESS) {
            context_ = nullptr;
            return kExitCallFailed;
        }

        status = lib_.obscan_context_get_device_management(context_, &device_mgmt_);
        Report("obscan_context_get_device_management", status);
        if (status != OB_SCAN_SUCCESS) return kExitCallFailed;

        // Must be registered before preview, or it fails with
        // OB_SCAN_ERROR_LISTENER_NOT_FOUND ("data listener not found in
        // preview !").
        status = lib_.obscan_context_add_data_msg_listener(context_, &g_data_listener);
        Report("obscan_context_add_data_msg_listener", status);
        if (status != OB_SCAN_SUCCESS) return kExitCallFailed;

        status = lib_.obscan_context_add_process_msg_listener(context_, &g_process_listener);
        Report("obscan_context_add_process_msg_listener", status);
        if (status != OB_SCAN_SUCCESS) return kExitCallFailed;

        // Registering a device-message listener is what actually starts the
        // vendor SDK's device-watcher thread (see obscan_api.h). It reports
        // DEVICE_DISCONNECT synchronously (initial state), then
        // DEVICE_CONNECT ~3s later once the scanner is opened.
        status = lib_.obscan_device_management_add_device_msg_listener(device_mgmt_, &g_device_listener);
        Report("obscan_device_management_add_device_msg_listener", status);
        if (status != OB_SCAN_SUCCESS) return kExitCallFailed;

        // Poll the count too rather than trusting the message alone: the
        // count is what session creation actually depends on.
        int32_t device_num = 0;
        WaitFor(kDeviceWaitMs, [&] {
            lib_.obscan_device_management_get_device_num(device_mgmt_, &device_num);
            return g_device_connects.load() > 0 && device_num > 0;
        });
        Say("device messages: connect=%d disconnect=%d other=%d; device count=%d", g_device_connects.load(),
            g_device_disconnects.load(), g_device_other.load(), device_num);
        if (g_stop_requested) return kExitInterrupted;
        if (device_num == 0) {
            Say("No device detected after %ds — is the scanner plugged in and powered?", kDeviceWaitMs / 1000);
            return kExitNoDevice;
        }

        // No obscan_device_management_get_device_info here: called at this
        // point it segfaulted on the real Raptor Pro on both runs of the
        // first hardware test (logs/branch_claude-clever-albattani-
        // cebx0e/), with no vendor log line for the call at all — the
        // binding (ObScanDeviceInfo layout or signature) has never been
        // verified. The vendor's own scan log already records name, serial
        // and firmware at connect.
        disconnects_at_connect_ = g_device_disconnects.load();

        ObScanSessionConfig session_config = {};
        session_config.output_dir = session_dir_.c_str();
        session_config.name = "openscanner_live_capture_test";
        session_config.is_import_mesh_file = false;
        status = lib_.obscan_context_create_session(context_, &session_config, &session_);
        Report("obscan_context_create_session", status);
        if (status != OB_SCAN_SUCCESS) {
            session_ = nullptr;
            return kExitCallFailed;
        }

        // This device's own scanParamConfig.dat (a real CrealityScan
        // project's saved LaserScanParam) is the ground truth for these.
        // Constraints the vendor enforces at create_scan, from the native
        // logs: line laser rejects voxel_size below 0.05 ("the line laser
        // cannot support voxel_size"), and the raptor rejects ACCURACY_FAST
        // with INFRARED projection. MARKERLESS fuse mode with line laser
        // never produced a single point in six runs — the SDK turns the
        // blue laser off and waits for speckle frames that never come — so
        // this stays on MARKER.
        ObScanConfig scan_config = {};
        scan_config.mode_config.post_texture = OB_SCAN_NO_TEXTURE;       // TextureModeProperty: 1
        scan_config.mode_config.frame_rate = OB_SCAN_FRAME_RATE_AUTO;     // FrameRateProperty: 0
        scan_config.mode_config.projection = OB_SCAN_BLUE_LINE_LASER;     // ScanProjectionProperty: 1
        scan_config.mode_config.scan_mode = OB_SCAN_MODE_OBJECT_MEDIUM;   // not in scanParamConfig.dat
        scan_config.mode_config.accuracy = OB_SCAN_ACCURACY_HIGH;         // ScanAccuracyProperty: 1
        scan_config.mode_config.fuse_mode = OB_SCAN_FUSE_MODE_MARKER;
        scan_config.mode_config.scan_fov = OB_SCAN_FOV_NORMAL;                       // LaserScanFOVProperty: 0
        scan_config.mode_config.object_material = OB_SCAN_OBJECT_MATERIAL_STANDARD;  // ObjectMaterialProperty: 0
        scan_config.mode_config.light_type = OB_SCAN_LIGHT_TYPE_CROSS;    // ScanLightTypeProperty: 1
        scan_config.mode_config.scan_type = OB_SCAN_CLOUD_FUSED;          // LaserScanTypeProperty: 0
        scan_config.mode_config.detail_voxel_size = 0.05;                 // DetailVoxelSizeProperty: 0.05
        scan_config.mode_config.voxel_size = 0.1;                         // VoxelSizeProperty: 0.1
        scan_config.name = "openscanner_live_scan";

        status = lib_.obscan_session_create_scan(session_, &scan_config, &scan_);
        Report("obscan_session_create_scan", status);
        if (status != OB_SCAN_SUCCESS) {
            scan_ = nullptr;
            return kExitCallFailed;
        }
        return kExitOk;
    }

    bool Disconnected() const { return g_device_disconnects.load() > disconnects_at_connect_; }

    int Capture() {
        // obscan_scan_start only works on a scan that is already previewing
        // (otherwise "cannot called start !"). preview returns before it has
        // finished setting up; completion is reported through the status
        // callback (progress reaches 1.0, native "change to preview
        // success !").
        previewed_ = true;  // From here on, Teardown() must stop the scan.
        ObScanStatus status = lib_.obscan_scan_preview(scan_, &OnPreviewProgress, &OnPreviewStatus);
        Report("obscan_scan_preview", status);
        if (status != OB_SCAN_SUCCESS) {
            // Returned failure synchronously: nothing started streaming.
            previewed_ = false;
            return kExitCallFailed;
        }

        // Confirmed against the real CrealityScan app: at this device's
        // default/auto exposure (~500 on a 1-1000 scale) physical markers
        // for fuse_mode=Marker went undetected; the real app only found
        // them once exposure was turned up well above default.
        Report("obscan_scan_config_combined_exposure_and_laser_current",
               lib_.obscan_scan_config_combined_exposure_and_laser_current(scan_, kExposure));

        WaitFor(kPreviewStatusWaitMs, [] { return g_preview_status.load() != kNoStatus; });
        Say("preview status callback: %s (%d), progress %.2f", StatusName(g_preview_status.load()),
            g_preview_status.load(), g_preview_progress.load());
        if (g_stop_requested) return kExitInterrupted;
        if (g_preview_status.load() != OB_SCAN_SUCCESS) return kExitCallFailed;

        // A SUCCESS preview status does not mean frames are flowing: on a
        // device left claimed by an earlier, uncleanly-exited process, IR
        // fails to start right after the callback reports success, and
        // obscan_scan_start then blocks forever waiting for frames. So
        // require actual FRAME_READY messages before calling start, and
        // treat none at all as "this device needs a power cycle" rather
        // than calling into a hang.
        bool got_first = WaitFor(kFirstFrameWaitMs, [] { return g_frame_ready.load() > 0; });
        if (g_stop_requested) return kExitInterrupted;
        if (Disconnected()) return kExitDisconnected;
        if (!got_first) {
            ReportDataCounts("after preview");
            Say("Preview reported success but delivered no frames in %ds. This is the signature of a scanner "
                "left streaming by an earlier process that never released it (crash, kill -9, or refused "
                "destroy). Not calling obscan_scan_start (it hangs in this state). Unplug/replug the scanner "
                "(power cycle) and run again.",
                kFirstFrameWaitMs / 1000);
            return kExitNoFrames;
        }
        WaitFor(kStableFrameWaitMs, [] { return g_frame_ready.load() >= kWantStableFrames; });
        ReportDataCounts("preview settled");
        if (g_stop_requested) return kExitInterrupted;

        for (int attempt = 1; attempt <= kStartAttempts; ++attempt) {
            status = lib_.obscan_scan_start(scan_);
            Report("obscan_scan_start", status);
            if (status == OB_SCAN_SUCCESS) break;
            // "cannot called start !" means preview hadn't settled yet;
            // give it a bit longer rather than giving up immediately.
            if (status != OB_SCAN_ERROR_INVALID_ARGUMENT || attempt == kStartAttempts) return kExitCallFailed;
            Pump(1000);
            if (g_stop_requested) return kExitInterrupted;
        }
        started_ = true;

        const int scan_seconds = ScanSeconds();
        Say("Scanning for %ds — move the scanner around the object now (Ctrl-C to stop early).", scan_seconds);
        const int polls = scan_seconds * 1000 / kPollIntervalMs;
        for (int i = 0; i < polls && !g_stop_requested; ++i) {
            Pump(kPollIntervalMs);
            // The device's auto-exposure decays a one-time value back to
            // ~500 within ~15s and the ABI has no auto/manual toggle, so
            // re-assert it every poll (holds it at ~1500-1680 in the log).
            lib_.obscan_scan_config_combined_exposure_and_laser_current(scan_, kExposure);
            if (Disconnected()) {
                Say("Device disconnected mid-scan.");
                return kExitDisconnected;
            }
            ObScanPointClouds cloud = {};
            status = lib_.obscan_scan_get_raw_cloud(scan_, &cloud);
            if ((i + 1) % 4 == 0 || cloud.num_points > 0) {
                Say("t=%.1fs obscan_scan_get_raw_cloud -> %s, num_points=%u, process_msgs=%d",
                    (i + 1) * kPollIntervalMs / 1000.0, StatusName(status), cloud.num_points,
                    g_process_msgs.load());
            }
        }

        // The realtime mesh is only readable while paused ("get model mesh
        // must first pause scan !" every time it was polled mid-scan).
        status = lib_.obscan_scan_pause(scan_);
        Report("obscan_scan_pause", status);
        if (status == OB_SCAN_SUCCESS) {
            paused_ = true;
            Pump(kPauseSettleMs);
            ReadResults();
        }
        return kExitOk;
    }

    void ReadResults() {
        ObScanPointClouds cloud = {};
        ObScanStatus status = lib_.obscan_scan_get_raw_cloud(scan_, &cloud);
        Say("after pause: obscan_scan_get_raw_cloud -> %s, num_points=%u", StatusName(status), cloud.num_points);
        if (status == OB_SCAN_SUCCESS && cloud.num_points > 0 && cloud.points != nullptr) {
            const ObScanPoint& p = cloud.points[0];
            Say("  points[0] = (%.4f, %.4f, %.4f)", p.x, p.y, p.z);
        }
        ObScanTriangleMesh mesh = {};
        status = lib_.obscan_scan_get_realtime_model_mesh(scan_, &mesh);
        Say("after pause: obscan_scan_get_realtime_model_mesh -> %s (%d), num_vertices=%u, num_triangles=%u",
            StatusName(status), static_cast<int>(status), mesh.num_vertices, mesh.num_triangles);
    }

    // Returns true if the device was released (or never opened).
    bool Teardown() {
        // Signals from here on would only cut the cleanup short.
        g_stop_requested = 0;
        bool released = true;

        if (scan_ != nullptr && previewed_) {
            if (started_ && !paused_) {
                Report("obscan_scan_pause", lib_.obscan_scan_pause(scan_));
                Pump(kPauseSettleMs);
            }
            // is_block=true runs the stop synchronously on this thread in
            // the logged good runs; still wait for the status callback in
            // case it completes asynchronously.
            ObScanStatus status = lib_.obscan_scan_stop(scan_, &OnStopProgress, &OnStopStatus, /*is_block=*/true);
            Report("obscan_scan_stop", status);
            WaitFor(kStopWaitMs, [] { return g_stop_status.load() != kNoStatus; });
            Say("stop status callback: %s (%d), progress %.2f", StatusName(g_stop_status.load()),
                g_stop_status.load(), g_stop_progress.load());
            if (g_stop_status.load() == OB_SCAN_ERROR_INVALID_SCAN && g_stop_progress.load() >= 1.0f) {
                Say("  (INVALID_SCAN after a completed stop is the SDK judging the captured data — e.g. too few "
                    "points/markers — not a failed stop.)");
            }
        }

        // Destroy is refused with MULTI_CALLED while the SDK still considers
        // an async task (stop) in flight; retry briefly instead of
        // abandoning the device.
        auto destroy_with_retry = [](const char* name, const std::function<ObScanStatus()>& destroy) {
            ObScanStatus status = OB_SCAN_ERROR_FATAL;
            for (int attempt = 1; attempt <= kDestroyAttempts; ++attempt) {
                auto t0 = std::chrono::steady_clock::now();
                status = destroy();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
                              .count();
                std::printf("[worker] %s took %lldms\n", name, static_cast<long long>(ms));
                Report(name, status);
                if (status != OB_SCAN_ERROR_MULTI_CALLED) break;
                Pump(1000);
            }
            return status;
        };

        if (session_ != nullptr) {
            ObScanStatus status =
                destroy_with_retry("obscan_session_destroy", [this] { return lib_.obscan_session_destroy(&session_); });
            if (status != OB_SCAN_SUCCESS) released = false;
        }
        if (context_ != nullptr) {
            // May crash or hang inside the vendor SDK after the device is
            // already released (README "Known issues") — that's what the
            // worker process and the launcher's timeout are for.
            ObScanStatus status =
                destroy_with_retry("obscan_context_destroy", [this] { return lib_.obscan_context_destroy(&context_); });
            if (status != OB_SCAN_SUCCESS) released = false;
        }
        if (!released) {
            Say("Teardown was refused by the SDK — the scanner may still be claimed. If the next run's preview "
                "delivers no frames, power-cycle the scanner.");
        }
        return released;
    }

    openscanner::ObscanLibrary& lib_;
    std::string algo_dir_;
    std::string session_dir_;
    ObScanContextHandle context_ = nullptr;
    ObScanDeviceManagementHandle device_mgmt_ = nullptr;
    ObScanSessionHandle session_ = nullptr;
    ObScanScanHandle scan_ = nullptr;
    int disconnects_at_connect_ = 0;
    bool previewed_ = false;
    bool started_ = false;
    bool paused_ = false;
};

int RunWorker(const std::string& library_path) {
    InstallStopSignalHandlers();
    Say("Loading OBScan SDK from: %s", library_path.c_str());
    int result = kExitCallFailed;
    try {
        // Never unloaded (see ObscanLibrary's destructor); deliberately
        // heap-allocated and leaked so nothing runs at scope exit either.
        auto* lib = new openscanner::ObscanLibrary(library_path);
        Say("Resolved all symbols OK.");
        LiveCapture capture(lib);
        result = capture.Run(library_path);
    } catch (const openscanner::ObscanLoadError& e) {
        Say("%s", e.what());
    }
    std::fflush(nullptr);
    // Skip static destructors and atexit handlers: the vendor SDK's own
    // static objects own threads that are still running if its context
    // destroy was refused or skipped, and destroying them aborts the
    // process ("mutex lock failed", or terminate on a joinable thread).
    std::_Exit(result);
}

const char* DescribeExit(int code) {
    switch (code) {
        case kExitOk: return "capture finished; device released";
        case kExitCallFailed: return "a vendor call failed (see log above); device released";
        case kExitNoDevice: return "no scanner detected";
        case kExitNoFrames: return "preview produced no frames — power-cycle the scanner, then retry";
        case kExitInterrupted: return "interrupted; scan stopped and device released";
        case kExitDisconnected: return "scanner disconnected mid-session";
        case kExitTeardownIncomplete: return "the SDK refused stop/destroy — scanner may need a power cycle";
        default: return "unexpected exit code";
    }
}

#if !defined(_WIN32)
// Two workers driving the same scanner (e.g. a second terminal started
// while a run is still tearing down) fight over the device and leave it in
// the same broken state as a crash. One at a time.
bool AcquireDeviceLock(int* out_fd) {
    int fd = open("/tmp/openscanner_device.lock", O_CREAT | O_RDWR, 0644);
    if (fd < 0) return true;  // Can't lock; don't block the run on it.
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return false;
    }
    *out_fd = fd;
    return true;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == kWorkerFlag) {
        return RunWorker(argv[2]);
    }

    std::string library_path = argc > 1 ? argv[1] : openscanner::ObscanLibrary::FindDefaultLibraryPath();
    if (library_path.empty()) {
        std::fprintf(stderr, "No OBScan SDK library found. Pass the path explicitly.\n");
        return 1;
    }

#if !defined(_WIN32)
    int lock_fd = -1;
    if (!AcquireDeviceLock(&lock_fd)) {
        std::fprintf(stderr, "Another live_capture_test is already using the scanner (/tmp/openscanner_device.lock).\n");
        return 1;
    }
#endif

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
    const char* no_interpose = std::getenv("OPENSCANNER_NO_VULKAN_INTERPOSE");
    std::string moltenvk_path;
    {
        size_t slash = library_path.find_last_of('/');
        if (slash != std::string::npos) {
            moltenvk_path = library_path.substr(0, slash + 1) + "libMoltenVK.dylib";
        }
    }
    if (no_interpose != nullptr && std::strcmp(no_interpose, "1") == 0) {
        std::printf("[launcher] OPENSCANNER_NO_VULKAN_INTERPOSE=1; not interposing Vulkan.\n");
    } else if (!moltenvk_path.empty() && access(moltenvk_path.c_str(), R_OK) == 0) {
        std::string insert = moltenvk_path + ":" + VULKAN_DISABLE_INTERPOSE_DYLIB;
        setenv("DYLD_INSERT_LIBRARIES", insert.c_str(), /*overwrite=*/1);
        std::printf("[launcher] DYLD_INSERT_LIBRARIES=%s\n", insert.c_str());
    } else {
        // e.g. tools/fake_obscan: inserting a nonexistent dylib would make
        // dyld abort the worker before main().
        std::printf("[launcher] No libMoltenVK.dylib next to '%s'; skipping Vulkan-disable fix.\n",
                     library_path.c_str());
    }
#endif

    // Happy-path budget: device connect (~3s, up to 10s) + preview (<2s,
    // up to 10s) + frames (<1s, up to 13s) + scan + pause settle (2s) +
    // stop (~0.2s, up to 20s) + destroys (sub-second, up to 10s of
    // retries). Past this the launcher sends SIGTERM, which the worker
    // turns into a normal stop-and-release; SIGKILL only if that also
    // stalls (stuck inside a vendor call).
    const int worker_timeout_seconds = 75 + ScanSeconds();
    constexpr int kTermGraceSeconds = 30;

    std::printf("[launcher] Running live capture session in a worker process (timeout %ds)...\n",
                worker_timeout_seconds);
    std::fflush(stdout);
    int worker_exit_code = -1;
    std::string crash_reason;
    bool worker_ok = openscanner::RunSelfAsWorker({kWorkerFlag, library_path}, &worker_exit_code, &crash_reason,
                                                   worker_timeout_seconds, kTermGraceSeconds);

    if (worker_ok) {
        std::printf("[launcher] Worker exited with code %d: %s.\n", worker_exit_code, DescribeExit(worker_exit_code));
        return worker_exit_code;
    }

    std::printf("[launcher] Worker did not finish normally (%s). Launcher is unaffected.\n", crash_reason.c_str());
    return 2;
}
