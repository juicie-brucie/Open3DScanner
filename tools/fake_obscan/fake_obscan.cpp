// A stand-in for the vendor's liborbbec_scan, exporting the same obscan_*
// symbols with the same signatures as obscan_api.h, so the live-capture
// control flow (init ordering, waits, timeouts, teardown) can be exercised
// without a physical scanner. It models the vendor behaviors this project
// has actually observed on the Raptor Pro, not an idealized SDK:
//
//   - Device detection only starts once a device-message listener is
//     registered; the connect message arrives later, on a vendor thread,
//     after a synchronous DEVICE_DISCONNECT for the initial state.
//   - Preview streams RGB/depth/IR/FRAME_READY data messages on a vendor
//     thread and reports its result through the status callback.
//   - obscan_scan_start before preview has settled fails with
//     OB_SCAN_ERROR_INVALID_ARGUMENT ("cannot called start !").
//   - A context that is never destroyed leaves the device "wedged": the
//     next run's preview produces no frames at all, until the device is
//     "power cycled" (delete the state file). This is modelled with a
//     marker file, because the real failure also outlives the process.
//   - Vendor threads keep calling into the host's listeners until
//     stop/destroy — a host that unloads the library or returns past its
//     listener structs first is exercising undefined behavior, as it would
//     be with the real SDK.
//
// Scenario is picked with the FAKE_OBSCAN_SCENARIO environment variable:
//   ok (default)    clean device, frames flow, start succeeds
//   no_device       the connect message never arrives
//   late_device     connect arrives after ~8s
//   no_frames       preview succeeds but no data messages ever arrive
//   preview_fail    the preview status callback reports a failure
//   start_hang      obscan_scan_start never returns
//   destroy_hang    obscan_context_destroy never returns
// FAKE_OBSCAN_STATE_FILE overrides the wedged-device marker path.
//
// Test-only. Never shipped; never loaded by anything but the test tools.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "openscanner/obscan_api.h"

#if defined(_WIN32)
#define FAKE_EXPORT extern "C" __declspec(dllexport)
#else
#define FAKE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

std::string Scenario() {
    const char* s = std::getenv("FAKE_OBSCAN_SCENARIO");
    return s != nullptr ? s : "ok";
}

std::string StateFile() {
    const char* s = std::getenv("FAKE_OBSCAN_STATE_FILE");
    return s != nullptr ? s : "/tmp/fake_obscan_device_claimed";
}

bool FileExists(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    std::fclose(f);
    return true;
}

void Log(const char* fmt, const char* arg = "") {
    std::fprintf(stderr, "[fake_obscan] ");
    std::fprintf(stderr, fmt, arg);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

void SleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

struct Scan;

struct Context {
    ObScanDataMessageListener* data_listener = nullptr;
    ObScanProcessMessageListener* process_listener = nullptr;
    ObScanDeviceMessageListener* device_listener = nullptr;
    std::atomic<int32_t> device_num{0};
    std::atomic<bool> shutting_down{false};
    bool wedged = false;
    std::thread watcher;
    std::vector<Scan*> scans;
    std::mutex mu;
};

struct Session {
    Context* ctx = nullptr;
};

struct Scan {
    Context* ctx = nullptr;
    std::atomic<bool> streaming{false};
    std::atomic<bool> started{false};
    std::atomic<int> frames{0};
    std::atomic<uint32_t> points{0};
    std::thread stream;
    std::vector<ObScanPoint> cloud;
    std::mutex mu;
};

int g_device_mgmt_tag = 0;

void StopStream(Scan* scan) {
    scan->streaming = false;
    if (scan->stream.joinable()) scan->stream.join();
}

}  // namespace

FAKE_EXPORT ObScanStatus obscan_version(const char** out_version) {
    if (out_version == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    *out_version = "fake-obscan-0.0.0";
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_context_create(ObScanContextHandle* out_context, ObScanContextConfig* config) {
    if (out_context == nullptr || config == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    auto* ctx = new Context();
    ctx->wedged = FileExists(StateFile());
    if (ctx->wedged) {
        Log("device was never released by a previous process — streaming will be dead (delete %s to 'power cycle')",
            StateFile().c_str());
    }
    *out_context = ctx;
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_context_get_device_management(ObScanContextHandle context,
                                                              ObScanDeviceManagementHandle* out_device_handle) {
    if (context == nullptr || out_device_handle == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    *out_device_handle = context;  // Device management is just a view of the context here.
    (void)g_device_mgmt_tag;
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_context_add_data_msg_listener(ObScanContextHandle context,
                                                              ObScanDataMessageListener* listener) {
    if (context == nullptr || listener == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    static_cast<Context*>(context)->data_listener = listener;
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_context_add_process_msg_listener(ObScanContextHandle context,
                                                                 ObScanProcessMessageListener* listener) {
    if (context == nullptr || listener == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    static_cast<Context*>(context)->process_listener = listener;
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_device_management_add_device_msg_listener(ObScanDeviceManagementHandle device_handle,
                                                                          ObScanDeviceMessageListener* listener) {
    auto* ctx = static_cast<Context*>(device_handle);
    if (ctx == nullptr || listener == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    ctx->device_listener = listener;
    // The real SDK reports the initial "no device" state synchronously.
    ObScanDeviceMessage initial = {};
    initial.msg_id = OB_SCAN_MESSAGE_ID_DEVICE_DISCONNECT;
    listener->onMessage(initial);
    ctx->watcher = std::thread([ctx] {
    
        const std::string scenario = Scenario();
        if (scenario == "no_device") return;
        int delay_ms = scenario == "late_device" ? 8000 : 400;
        for (int waited = 0; waited < delay_ms; waited += 50) {
            if (ctx->shutting_down) return;
            SleepMs(50);
        }
        // The device is "claimed" from the moment it is opened; only a real
        // obscan_context_destroy releases it again.
        if (FILE* f = std::fopen(StateFile().c_str(), "wb")) std::fclose(f);
        ctx->device_num = 1;
        static int fake_device_payload = 1;
        ObScanDeviceMessage connect = {};
        connect.msg_id = OB_SCAN_MESSAGE_ID_DEVICE_CONNECT;
        connect.data = &fake_device_payload;
        ctx->device_listener->onMessage(connect);
    });
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_device_management_get_device_num(ObScanDeviceManagementHandle device_handle,
                                                                 int32_t* out_device_num) {
    auto* ctx = static_cast<Context*>(device_handle);
    if (ctx == nullptr || out_device_num == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    *out_device_num = ctx->device_num.load();
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_device_management_get_device_info(ObScanDeviceManagementHandle device_handle,
                                                                  int32_t idx, ObScanDeviceInfo* out_device_info) {
    auto* ctx = static_cast<Context*>(device_handle);
    if (ctx == nullptr || out_device_info == nullptr || idx < 0 || idx >= ctx->device_num.load()) {
        return OB_SCAN_ERROR_INVALID_ARGUMENT;
    }
    *out_device_info = {};
    out_device_info->camera_pid = 0x0c03;
    out_device_info->camera_name = "Fake Raptor Pro";
    out_device_info->camera_serial_number = "FAKE0000";
    out_device_info->camera_connection_type = "USB3.0";
    out_device_info->camera_firmware_version = "0.0.0";
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_device_management_get_active_idx(ObScanDeviceManagementHandle device_handle,
                                                                 int32_t* out_active_idx) {
    auto* ctx = static_cast<Context*>(device_handle);
    if (ctx == nullptr || out_active_idx == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    *out_active_idx = ctx->device_num.load() > 0 ? 0 : -1;
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_device_management_select_active_device(ObScanDeviceManagementHandle device_handle,
                                                                      int32_t active_idx) {
    auto* ctx = static_cast<Context*>(device_handle);
    if (ctx == nullptr || active_idx < 0 || active_idx >= ctx->device_num.load()) {
        return OB_SCAN_ERROR_INVALID_ARGUMENT;
    }
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_context_create_session(ObScanContextHandle context, ObScanSessionConfig* config,
                                                       ObScanSessionHandle* out_session) {
    auto* ctx = static_cast<Context*>(context);
    if (ctx == nullptr || config == nullptr || out_session == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    if (ctx->device_num.load() == 0) return OB_SCAN_ERROR_DEVICE_DISCONNECT;
    auto* session = new Session();
    session->ctx = ctx;
    *out_session = session;
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_session_create_scan(ObScanSessionHandle session, ObScanConfig* config,
                                                    ObScanScanHandle* out_scan) {
    auto* s = static_cast<Session*>(session);
    if (s == nullptr || config == nullptr || out_scan == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    auto* scan = new Scan();
    scan->ctx = s->ctx;
    {
        std::lock_guard<std::mutex> lock(s->ctx->mu);
        s->ctx->scans.push_back(scan);
    }
    *out_scan = scan;
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_scan_preview(ObScanScanHandle scan_handle, ObScanProgressCallback progress_callback,
                                             ObScanStatusCallback status_callback) {
    auto* scan = static_cast<Scan*>(scan_handle);
    if (scan == nullptr || progress_callback == nullptr || status_callback == nullptr) {
        return OB_SCAN_ERROR_INVALID_ARGUMENT;
    }
    Context* ctx = scan->ctx;
    if (ctx->data_listener == nullptr || ctx->process_listener == nullptr) return OB_SCAN_ERROR_LISTENER_NOT_FOUND;
    if (scan->streaming) return OB_SCAN_ERROR_MULTI_CALLED;

    const std::string scenario = Scenario();
    const bool dead = ctx->wedged || scenario == "no_frames";
    scan->streaming = true;
    scan->stream = std::thread([scan, ctx, progress_callback, status_callback, scenario, dead] {
        progress_callback(0.5f);
        SleepMs(150);
        progress_callback(1.0f);
        status_callback(scenario == "preview_fail" ? OB_SCAN_ERROR_CAMERA_STREAMING_RETRY_FAILED : OB_SCAN_SUCCESS);
        if (dead || scenario == "preview_fail") {
            while (scan->streaming) SleepMs(20);
            return;
        }
        while (scan->streaming) {
            const int32_t ids[] = {OB_SCAN_MESSAGE_ID_NEW_RGB_IMAGE, OB_SCAN_MESSAGE_ID_NEW_DEPTH_IMAGE,
                                   OB_SCAN_MESSAGE_ID_NEW_IR_IMAGE, OB_SCAN_MESSAGE_ID_FRAME_READY};
            for (int32_t id : ids) {
                ObScanDataMessage msg = {};
                msg.msg_id = id;
                msg.data = scan;
                ctx->data_listener->onMessage(msg);
            }
            ++scan->frames;
            if (scan->started) {
                std::lock_guard<std::mutex> lock(scan->mu);
                for (int i = 0; i < 50; ++i) {
                    float t = static_cast<float>(scan->cloud.size());
                    scan->cloud.push_back({t * 0.001f, 0.0f, 0.3f, 1.0f});
                }
                scan->points = static_cast<uint32_t>(scan->cloud.size());
            }
            SleepMs(33);
        }
    });
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_scan_start(ObScanScanHandle scan_handle) {
    auto* scan = static_cast<Scan*>(scan_handle);
    if (scan == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    if (Scenario() == "start_hang") {
        for (;;) SleepMs(1000);
    }
    // Mirrors the native "cannot called start !" guard.
    if (!scan->streaming || scan->frames.load() < 5) {
        Log("cannot called start !");
        return OB_SCAN_ERROR_INVALID_ARGUMENT;
    }
    scan->started = true;
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_scan_pause(ObScanScanHandle scan_handle) {
    auto* scan = static_cast<Scan*>(scan_handle);
    if (scan == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    if (!scan->started) return OB_SCAN_ERROR_SESSION_NOT_PAUSED;
    scan->started = false;
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_scan_stop(ObScanScanHandle scan_handle, ObScanProgressCallback progress_callback,
                                          ObScanStatusCallback status_callback, bool is_block) {
    auto* scan = static_cast<Scan*>(scan_handle);
    if (scan == nullptr || progress_callback == nullptr || status_callback == nullptr) {
        return OB_SCAN_ERROR_INVALID_ARGUMENT;
    }
    (void)is_block;
    scan->started = false;
    progress_callback(0.1f);
    StopStream(scan);
    progress_callback(1.0f);
    status_callback(OB_SCAN_SUCCESS);
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_scan_get_raw_cloud(ObScanScanHandle scan_handle, ObScanPointClouds* out_cloud) {
    auto* scan = static_cast<Scan*>(scan_handle);
    if (scan == nullptr || out_cloud == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(scan->mu);
    out_cloud->num_points = static_cast<uint32_t>(scan->cloud.size());
    out_cloud->points = scan->cloud.empty() ? nullptr : scan->cloud.data();
    out_cloud->normals = nullptr;
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_scan_get_realtime_model_mesh(ObScanScanHandle scan_handle,
                                                             ObScanTriangleMesh* out_mesh) {
    auto* scan = static_cast<Scan*>(scan_handle);
    if (scan == nullptr || out_mesh == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    if (scan->started) {
        Log("get model mesh must first pause scan !");
        return OB_SCAN_ERROR_MULTI_CALLED;
    }
    *out_mesh = {};
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_scan_config_combined_exposure_and_laser_current(ObScanScanHandle scan_handle,
                                                                               int32_t value) {
    if (scan_handle == nullptr || value < 1 || value > 1000) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_scan_get_mode_config(ObScanScanHandle scan_handle, ObScanModeConfig* out_config) {
    if (scan_handle == nullptr || out_config == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    *out_config = {};
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_session_destroy(ObScanSessionHandle* session) {
    if (session == nullptr || *session == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    delete static_cast<Session*>(*session);
    *session = nullptr;
    return OB_SCAN_SUCCESS;
}

FAKE_EXPORT ObScanStatus obscan_context_destroy(ObScanContextHandle* context) {
    if (context == nullptr || *context == nullptr) return OB_SCAN_ERROR_INVALID_ARGUMENT;
    auto* ctx = static_cast<Context*>(*context);
    if (Scenario() == "destroy_hang") {
        for (;;) SleepMs(1000);
    }
    ctx->shutting_down = true;
    if (ctx->watcher.joinable()) ctx->watcher.join();
    {
        std::lock_guard<std::mutex> lock(ctx->mu);
        for (Scan* scan : ctx->scans) {
            StopStream(scan);
            delete scan;
        }
        ctx->scans.clear();
    }
    // Device released: the next process gets a clean device.
    std::remove(StateFile().c_str());
    delete ctx;
    *context = nullptr;
    return OB_SCAN_SUCCESS;
}
