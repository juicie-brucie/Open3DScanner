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
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
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

// --- Control channel (UI -> server) and status channel (server -> UI) -----
// Text frames over the same WebSocket, independent of the binary
// point-cloud frames above. Hand-parsed rather than pulling in a JSON
// library, matching the rest of this file's "implement the small fixed
// shape we actually need" style (see ws_broadcast_server.cpp's SHA-1/
// Base64 for the same reasoning). Only flat {"key": value, ...} objects
// with string/number/bool leaves are supported — nothing here ever nests.

// Finds "key": <value> in `json` and returns the raw, unparsed value text
// (quotes kept on if it was a string). false if the key isn't present.
bool FindJsonValue(const std::string& json, const std::string& key, std::string* out_value) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos += needle.size();
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    size_t end = pos;
    if (end < json.size() && json[end] == '"') {
        end = json.find('"', end + 1);
        if (end == std::string::npos) return false;
        *out_value = json.substr(pos + 1, end - pos - 1);
        return true;
    }
    while (end < json.size() && json[end] != ',' && json[end] != '}') ++end;
    *out_value = json.substr(pos, end - pos);
    // Trim trailing whitespace left over from the scan above.
    while (!out_value->empty() && std::isspace(static_cast<unsigned char>(out_value->back()))) {
        out_value->pop_back();
    }
    return true;
}

bool FindJsonBool(const std::string& json, const std::string& key, bool* out_value) {
    std::string raw;
    if (!FindJsonValue(json, key, &raw)) return false;
    if (raw == "true") {
        *out_value = true;
        return true;
    }
    if (raw == "false") {
        *out_value = false;
        return true;
    }
    return false;
}

bool FindJsonInt(const std::string& json, const std::string& key, int* out_value) {
    std::string raw;
    if (!FindJsonValue(json, key, &raw)) return false;
    if (raw.empty()) return false;
    *out_value = std::atoi(raw.c_str());
    return true;
}

// Combined exposure/laser-current range per obscan_api.h
// (fn_obscan_scan_config_combined_exposure_and_laser_current); reused as
// the UI's manual exposure/brightness slider range for both modes so one
// value means the same thing everywhere.
constexpr int kExposureMin = 1;
constexpr int kExposureMax = 1000;
constexpr int kDefaultExposure = 900;  // Matches the value this file already drove live scans with.

// Shared between the WS reader thread(s) (writer, via HandleControlMessage)
// and whichever thread owns the scan loop (reader, via its tick) — hence
// all-atomic, no locking. Mirrors the synthetic/live scan lifecycle: a
// scan is idle, scanning, or paused; exposure is either auto or a held
// manual value; FOV is a two-position switch on the real scanner (no
// continuous zoom exists in the hardware API — see obscan_api.h's
// ObScanFov) applied at scan creation, so for live mode this field is
// informational only once scanning has started (see RunLiveServer).
struct ControlState {
    std::atomic<bool> scanning{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> finish_requested{false};
    std::atomic<bool> auto_exposure{true};
    std::atomic<int> exposure_value{kDefaultExposure};
    std::atomic<bool> fov_wide{false};
};

// Applies one control command to `state`. Safe to call from any thread;
// callers that also need the command to affect real hardware (RunLiveServer)
// check the same atomics from their own loop rather than acting here, so
// nothing in this function ever touches the vendor SDK directly.
void HandleControlMessage(const std::string& json, ControlState* state) {
    std::string cmd;
    if (!FindJsonValue(json, "cmd", &cmd)) return;
    if (cmd == "start") {
        state->scanning = true;
        state->paused = false;
    } else if (cmd == "pause") {
        state->paused = true;
    } else if (cmd == "finish") {
        state->scanning = false;
        state->paused = false;
        state->finish_requested = true;
    } else if (cmd == "set_exposure") {
        bool is_auto = false;
        if (FindJsonBool(json, "auto", &is_auto) && is_auto) {
            state->auto_exposure = true;
        } else {
            int value = kDefaultExposure;
            if (FindJsonInt(json, "value", &value)) {
                value = std::max(kExposureMin, std::min(kExposureMax, value));
                state->exposure_value = value;
            }
            state->auto_exposure = false;
        }
    } else if (cmd == "set_fov") {
        std::string value;
        if (FindJsonValue(json, "value", &value)) {
            state->fov_wide = (value == "wide");
        }
    }
}

// mode is "synthetic" or "live"; fov_controllable is false once a live
// scan has actually started, since the real SDK has no call to change FOV
// mid-scan (see ControlState's comment above) — the UI uses it to decide
// whether to let the operator touch that control at all.
std::string BuildStatusJson(const char* mode, const ControlState& state, bool fov_controllable) {
    const char* scan_state = !state.scanning.load() ? "idle" : (state.paused.load() ? "paused" : "scanning");
    std::string json = "{\"type\":\"status\",\"mode\":\"";
    json += mode;
    json += "\",\"state\":\"";
    json += scan_state;
    json += "\",\"autoExposure\":";
    json += state.auto_exposure.load() ? "true" : "false";
    json += ",\"exposure\":";
    json += std::to_string(state.exposure_value.load());
    json += ",\"exposureMin\":";
    json += std::to_string(kExposureMin);
    json += ",\"exposureMax\":";
    json += std::to_string(kExposureMax);
    json += ",\"fov\":\"";
    json += state.fov_wide.load() ? "wide" : "normal";
    json += "\",\"fovControllable\":";
    json += fov_controllable ? "true" : "false";
    json += "}";
    return json;
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
    ControlState control;
    control.scanning = true;  // Streams immediately, as before, if no UI ever sends a command.

    server.SetTextMessageHandler([&control, &server](const std::string& msg) {
        HandleControlMessage(msg, &control);
        // FOV always "controllable" here — the synthetic sphere has no
        // hardware FOV constraint, it's purely a render-side parameter
        // ui/app.js reads back out of this same status message.
        server.BroadcastText(BuildStatusJson("synthetic", control, /*fov_controllable=*/true));
    });

    if (!server.Start(port)) {
        std::fprintf(stderr, "Failed to start WebSocket server on port %u (port in use?)\n", port);
        std::exit(1);
    }
    std::printf("Streaming synthetic point-cloud frames on ws://0.0.0.0:%u\n", port);
    std::printf("Open ui/index.html in a browser to view it.\n");

    std::vector<Vec3> base_points = MakeSpherePoints(kPointGridSize);
    uint32_t frame_id = 0;
    double angle = 0.0;
    double last_status_broadcast_ms = 0.0;

    while (true) {
        auto tick_start = std::chrono::steady_clock::now();

        if (control.finish_requested.exchange(false)) {
            // "Finish" resets the demo back to a fresh idle sphere, ready
            // for the next "Start" — there's no real session to tear down
            // in synthetic mode.
            frame_id = 0;
            angle = 0.0;
        }

        double now_ms = NowMs();
        if (now_ms - last_status_broadcast_ms >= 1000.0) {
            server.BroadcastText(BuildStatusJson("synthetic", control, /*fov_controllable=*/true));
            last_status_broadcast_ms = now_ms;
        }

        if (control.scanning.load() && !control.paused.load()) {
            std::vector<Vec3> frame_points = base_points;
            RotateY(&frame_points, angle);
            angle += 0.02;

            std::vector<uint8_t> buf =
                BuildFrame(frame_id++, now_ms, reinterpret_cast<const float*>(frame_points.data()),
                           static_cast<uint32_t>(frame_points.size()));
            if (server.ConnectedClientCount() > 0) {
                server.BroadcastBinary(buf.data(), buf.size());
            }
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

const char* DataMessageName(int32_t msg_id) {
    switch (msg_id) {
        case OB_SCAN_MESSAGE_ID_NEW_RGB_IMAGE: return "NEW_RGB_IMAGE";
        case OB_SCAN_MESSAGE_ID_NEW_DEPTH_IMAGE: return "NEW_DEPTH_IMAGE";
        case OB_SCAN_MESSAGE_ID_NEW_IR_IMAGE: return "NEW_IR_IMAGE";
        case OB_SCAN_MESSAGE_ID_FRAME_READY: return "FRAME_READY";
        default: return nullptr; // unknown until OQ-P3 names it
    }
}

const char* ProcessMessageName(int32_t msg_id) {
    switch (msg_id) {
        case OB_SCAN_MESSAGE_ID_FRAME_BUFFER_FULL: return "FRAME_BUFFER_FULL";
        case OB_SCAN_MESSAGE_ID_MAX_FRAMES_REACHED: return "MAX_FRAMES_REACHED";
        case OB_SCAN_MESSAGE_ID_DETECT_FACE: return "DETECT_FACE";
        case OB_SCAN_MESSAGE_ID_SCANNING_FAILED: return "SCANNING_FAILED";
        case OB_SCAN_MESSAGE_ID_OPTIMIZATION_TO_MESH_NEED_CHANGE_VOXEL_SIZE:
            return "OPTIMIZATION_TO_MESH_NEED_CHANGE_VOXEL_SIZE";
        case OB_SCAN_MESSAGE_ID_PASTED_MARKERS_TOO_REGULAR: return "PASTED_MARKERS_TOO_REGULAR";
        default: return nullptr; // not in the header's process-id table
    }
}

void OnDataMessage(ObScanDataMessage msg) {
    const char* name = DataMessageName(msg.msg_id);
    if (name != nullptr) {
        std::printf("[worker][data] msg_id=%d (%s) data=%p thread=%lu\n", msg.msg_id, name, msg.data,
                    static_cast<unsigned long>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    } else {
        std::printf("[worker][data] msg_id=%d data=%p thread=%lu\n", msg.msg_id, msg.data,
                    static_cast<unsigned long>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    }
    // Any id not in the header's table is logged numerically (do not
    // invent names); the header's id table is the contract.
    std::fflush(stdout);
    if (msg.msg_id == OB_SCAN_MESSAGE_ID_FRAME_READY) {
        ++g_frame_ready_count;
    }
}

void OnProcessMessage(ObScanProcessMessage msg) {
    const char* name = ProcessMessageName(msg.msg_id);
    if (name != nullptr) {
        std::printf("[worker][process] msg_id=%d (%s) data=%p thread=%lu\n", msg.msg_id, name, msg.data,
                    static_cast<unsigned long>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    } else {
        std::printf("[worker][process] msg_id=%d data=%p thread=%lu\n", msg.msg_id, msg.data,
                    static_cast<unsigned long>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    }
    std::fflush(stdout);
}
void OnPreviewStatus(ObScanStatus) {}

// The vendor SDK keeps the listener pointers it's given and calls them from
// its own threads, which can still be running after RunLiveServer returns
// (and during process exit). Static storage keeps them valid for the whole
// process lifetime instead of pointing into a dead stack frame.
ObScanDataMessageListener g_data_listener = {&OnDataMessage};
ObScanProcessMessageListener g_process_listener = {&OnProcessMessage};
ObScanDeviceMessageListener g_device_listener = {&OnDeviceMessage};
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

    status = lib.obscan_context_add_data_msg_listener(context, &g_data_listener);
    Report("obscan_context_add_data_msg_listener", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    status = lib.obscan_context_add_process_msg_listener(context, &g_process_listener);
    Report("obscan_context_add_process_msg_listener", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    status = lib.obscan_device_management_add_device_msg_listener(device_mgmt, &g_device_listener);
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

    ControlState control;
    control.scanning = true;  // obscan_scan_start above has already run.
    control.auto_exposure = false;  // Matches the unconditional 900 re-assert this loop already did.
    control.exposure_value = kDefaultExposure;
    control.fov_wide = (scan_config.mode_config.scan_fov == OB_SCAN_FOV_WIDE);

    openscanner::WsBroadcastServer server;
    server.SetTextMessageHandler([&control, &server](const std::string& msg) {
        HandleControlMessage(msg, &control);
        if (control.finish_requested.load()) g_stop_requested = 1;
        server.BroadcastText(BuildStatusJson("live", control, /*fov_controllable=*/false));
    });
    if (!server.Start(port)) {
        std::fprintf(stderr, "[worker] Failed to start WebSocket server on port %u (port in use?)\n", port);
        // Still try to tear the scan down cleanly below rather than leaving
        // the device mid-scan.
    } else {
        std::printf("[worker] Streaming live scan frames on ws://0.0.0.0:%u (open ui/index.html)\n", port);
        std::printf(
            "[worker] Note: each tick now calls obscan_scan_get_model_point_color_clouds, which publishes the\n"
            "[worker] model cloud, so live frames should be nonzero during scanning. obscan_scan_get_raw_cloud\n"
            "[worker] only reflects the last publish and is logged alongside for contrast; the final pause ->\n"
            "[worker] get_realtime_model_mesh snapshot is still broadcast at the end of this session.\n");
    }
    std::fflush(stdout);

    uint32_t frame_id = 0;
    const int total_ticks = (g_live_session_seconds * 1000) / kLiveTickIntervalMs;
    uint32_t max_points_seen = 0;
    bool hardware_paused = false;
    // Separate from hardware_paused: records what we last *attempted*
    // (regardless of outcome), so a failed resume on fragile hardware is
    // tried exactly once per click, not retried every 100ms tick until it
    // succeeds — a hammering retry loop is its own risk on an SDK with a
    // documented history of needing a power cycle after the wrong call
    // sequence. Pausing again (or clicking Start again) is still a fresh,
    // deliberate single attempt.
    bool last_requested_paused = false;
    for (int tick = 0; tick < total_ticks; ++tick) {
        if (g_stop_requested) {
            std::printf(
                "[worker] Stop requested (Ctrl+C or UI \"finish\") — ending the live loop and tearing down "
                "cleanly.\n");
            std::fflush(stdout);
            break;
        }
#if defined(__APPLE__)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, kLiveTickIntervalMs / 1000.0, false);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(kLiveTickIntervalMs));
#endif

        // Pause/resume only on a state transition, never repeated every
        // tick. obscan_scan_pause is already proven (this file already
        // called it once, at end-of-session, before this change). Resuming
        // via obscan_scan_start on an already-started scan has not been
        // exercised anywhere in this codebase before now — see the PR this
        // landed in for the risk accepted by wiring it up.
        bool want_paused = control.paused.load();
        if (want_paused != last_requested_paused) {
            last_requested_paused = want_paused;
            ObScanStatus toggle_status = want_paused ? lib.obscan_scan_pause(scan) : lib.obscan_scan_start(scan);
            Report(want_paused ? "obscan_scan_pause (control)" : "obscan_scan_start (resume, control)",
                   toggle_status);
            if (toggle_status == OB_SCAN_SUCCESS) hardware_paused = want_paused;
        }

        ObScanPointColorClouds cloud = {};
        ObScanPointClouds raw_cloud = {};
        ObScanCloudBbox bbox = {};
        uint32_t published_count = 0;
        if (!hardware_paused) {
            // No dedicated auto/manual exposure toggle exists in the ABI; the
            // device's own auto-exposure decays a one-time value back toward
            // its own target within ~15s (see obscan_api.h). Hold a manual
            // value by re-asserting every tick; in auto mode, skip the call
            // and let that decay happen.
            if (!control.auto_exposure.load()) {
                lib.obscan_scan_config_combined_exposure_and_laser_current(scan, control.exposure_value.load());
            }

            // The color getter is the publish trigger for the model cloud
            // (see docs/recon/findings/scan-live-data.md §3): calling it
            // swaps the fusion temp cloud into the published show cloud, so
            // get_raw_cloud / the UI finally see live points while scanning.
            // out_count is not validated by the wrapper, so pass a valid
            // pointer; out_bbox is required non-null.
            status = lib.obscan_scan_get_model_point_color_clouds(scan, &cloud, &bbox, &published_count);
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
                // Release is a no-op in the vendor (native `ret`); call it
                // for API symmetry per the header. Buffers are borrowed, so
                // copy out before the next publish.
                lib.obscan_scan_model_clouds_release(scan);
            }
            // Raw getter never publishes; reading it after the color getter
            // shows the last published cloud, demonstrating the mechanism.
            lib.obscan_scan_get_raw_cloud(scan, &raw_cloud);
        }

        if (tick % 10 == 0) {  // ~every 1s
            server.BroadcastText(BuildStatusJson("live", control, /*fov_controllable=*/false));
        }
        if (tick % 50 == 0) {  // ~every 5s
            std::printf("[worker] tick %d/%d: color_cloud num_points=%u raw_cloud num_points=%u (max seen so far: %u), clients=%zu, paused=%d\n",
                        tick, total_ticks, cloud.num_points, raw_cloud.num_points, max_points_seen, server.ConnectedClientCount(),
                        hardware_paused ? 1 : 0);
            std::fflush(stdout);
        }
    }

    if (!hardware_paused) {
        status = lib.obscan_scan_pause(scan);
        Report("obscan_scan_pause", status);
    }

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
