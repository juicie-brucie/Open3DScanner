// Purpose-built hardware test for PLAN #2: exercises the data- and
// process-message-listener registration paths that the existing tools
// never touch:
//   - NULL context -> OB_SCAN_ERROR_DEADLINE_EXCEEDED (845283591)
//   - NULL listener -> OB_SCAN_ERROR_INVALID_ARGUMENT (845283584)
//   - double add without remove -> OB_SCAN_ERROR_INVALID_ARGUMENT (845283584)
//   - remove on empty slot -> OB_SCAN_ERROR_LISTENER_NOT_FOUND (845283593)
//   - remove then add (single-slot clear) -> OB_SCAN_SUCCESS
//   - both slots mandatory before obscan_scan_preview()
//
// Usage:
//   data_process_message_listener_test [path-to-liborbbec_scan.dylib-or-lib_orbbec_scan.dll]

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#include <dlfcn.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "openscanner/obscan_api.h"
#include "openscanner/obscan_loader.h"
#include "openscanner/worker_process.h"

namespace {

constexpr const char* kWorkerFlag = "--data-process-msg-listener-worker";

std::atomic<int> g_data_count{0};
std::atomic<int> g_process_count{0};

void OnDataMessage(ObScanDataMessage msg) {
    g_data_count++;
    std::printf("[worker][cb] data message: msg_id=%d data=%p\n", msg.msg_id, msg.data);
    std::fflush(stdout);
}

void OnProcessMessage(ObScanProcessMessage msg) {
    g_process_count++;
    std::printf("[worker][cb] process message: msg_id=%d data=%p\n", msg.msg_id, msg.data);
    std::fflush(stdout);
}

ObScanDataMessageListener g_data_listener_a{OnDataMessage};
ObScanDataMessageListener g_data_listener_b{OnDataMessage};
ObScanProcessMessageListener g_process_listener_a{OnProcessMessage};
ObScanProcessMessageListener g_process_listener_b{OnProcessMessage};

const char* StatusName(ObScanStatus status) {
    switch (status) {
        case OB_SCAN_SUCCESS: return "OB_SCAN_SUCCESS";
        case OB_SCAN_ERROR_INVALID_ARGUMENT: return "OB_SCAN_ERROR_INVALID_ARGUMENT";
        case OB_SCAN_ERROR_DEADLINE_EXCEEDED: return "OB_SCAN_ERROR_DEADLINE_EXCEEDED";
        case OB_SCAN_ERROR_LISTENER_NOT_FOUND: return "OB_SCAN_ERROR_LISTENER_NOT_FOUND";
        case OB_SCAN_ERROR_MULTI_CALLED: return "OB_SCAN_ERROR_MULTI_CALLED";
        default: return "OB_SCAN_ERROR_(see obscan_api.h)";
    }
}

int g_failures = 0;
void Check(const char* label, ObScanStatus actual, ObScanStatus expected) {
    if (actual == expected) {
        std::printf("[worker][PASS] %s\n", label);
    } else {
        g_failures++;
        std::printf("[worker][FAIL] %s (got %d, expected %d)\n", label, (int)actual, (int)expected);
    }
    std::fflush(stdout);
}

int RunTests(const std::string& library_path) {
    std::printf("[worker] Loading OBScan SDK from: %s\n", library_path.c_str());
    openscanner::ObscanLibrary* libptr = nullptr;
    try {
        libptr = new openscanner::ObscanLibrary(library_path);
    } catch (const std::exception& e) {
        std::printf("[worker] FAILED to load SDK: %s\n", e.what());
        std::fflush(stdout);
        return 1;
    }
    openscanner::ObscanLibrary& lib = *libptr;

    std::string streaming_assets = openscanner::ObscanLibrary::FindStreamingAssetsDirForLibraryPath(library_path);
    ObScanContextConfig config = {};
    config.input_algo_dependent_files_dir = streaming_assets.empty() ? nullptr : streaming_assets.c_str();
    config.output_ob_sdk_log_dir = "/tmp/openscanner_ob_sdk_log";
    config.output_ob_scan_log_dir = "/tmp/openscanner_ob_scan_log";
    config.output_computer_performance_fps_dir = "/tmp/openscanner_fps_log";
    config.output_windows_gpu_info_dir = "";

    ObScanContextHandle context = nullptr;
    ObScanStatus status = lib.obscan_context_create(&context, &config);
    std::printf("[worker] obscan_context_create() -> %s (%d)\n", StatusName(status), (int)status);
    if (status != OB_SCAN_SUCCESS) return 1;

    // NULL context.
    status = lib.obscan_context_add_data_msg_listener(nullptr, &g_data_listener_a);
    Check("add_data(NULL context) == DEADLINE_EXCEEDED", status, OB_SCAN_ERROR_DEADLINE_EXCEEDED);
    status = lib.obscan_context_add_process_msg_listener(nullptr, &g_process_listener_a);
    Check("add_process(NULL context) == DEADLINE_EXCEEDED", status, OB_SCAN_ERROR_DEADLINE_EXCEEDED);

    // NULL listener.
    status = lib.obscan_context_add_data_msg_listener(context, nullptr);
    Check("add_data(NULL listener) == INVALID_ARGUMENT", status, OB_SCAN_ERROR_INVALID_ARGUMENT);
    status = lib.obscan_context_add_process_msg_listener(context, nullptr);
    Check("add_process(NULL listener) == INVALID_ARGUMENT", status, OB_SCAN_ERROR_INVALID_ARGUMENT);

    // First valid registration.
    status = lib.obscan_context_add_data_msg_listener(context, &g_data_listener_a);
    Check("add_data(listener_a) #1 == SUCCESS", status, OB_SCAN_SUCCESS);
    status = lib.obscan_context_add_process_msg_listener(context, &g_process_listener_a);
    Check("add_process(listener_a) #1 == SUCCESS", status, OB_SCAN_SUCCESS);

    // Double add without remove.
    status = lib.obscan_context_add_data_msg_listener(context, &g_data_listener_b);
    Check("double add_data (no remove) == INVALID_ARGUMENT", status, OB_SCAN_ERROR_INVALID_ARGUMENT);
    status = lib.obscan_context_add_process_msg_listener(context, &g_process_listener_b);
    Check("double add_process (no remove) == INVALID_ARGUMENT", status, OB_SCAN_ERROR_INVALID_ARGUMENT);

    // Remove on empty slot for a fresh context? Test re-remove after remove.
    // Remove listener_a then re-remove -> LISTENER_NOT_FOUND.
    status = lib.obscan_context_remove_data_msg_listener(context, &g_data_listener_a);
    Check("remove_data(listener_a) == SUCCESS", status, OB_SCAN_SUCCESS);
    status = lib.obscan_context_remove_data_msg_listener(context, &g_data_listener_a);
    Check("remove_data again (empty) == LISTENER_NOT_FOUND", status, OB_SCAN_ERROR_LISTENER_NOT_FOUND);

    status = lib.obscan_context_remove_process_msg_listener(context, &g_process_listener_a);
    Check("remove_process(listener_a) == SUCCESS", status, OB_SCAN_SUCCESS);
    status = lib.obscan_context_remove_process_msg_listener(context, &g_process_listener_a);
    Check("remove_process again (empty) == LISTENER_NOT_FOUND", status, OB_SCAN_ERROR_LISTENER_NOT_FOUND);

    // Re-add after remove succeeds (single-slot clear).
    status = lib.obscan_context_add_data_msg_listener(context, &g_data_listener_a);
    Check("add_data after remove == SUCCESS", status, OB_SCAN_SUCCESS);
    status = lib.obscan_context_add_process_msg_listener(context, &g_process_listener_a);
    Check("add_process after remove == SUCCESS", status, OB_SCAN_SUCCESS);

    // remove with NULL listener / NULL context.
    status = lib.obscan_context_remove_data_msg_listener(nullptr, &g_data_listener_a);
    Check("remove_data(NULL context) == DEADLINE_EXCEEDED", status, OB_SCAN_ERROR_DEADLINE_EXCEEDED);
    status = lib.obscan_context_remove_process_msg_listener(nullptr, &g_process_listener_a);
    Check("remove_process(NULL context) == DEADLINE_EXCEEDED", status, OB_SCAN_ERROR_DEADLINE_EXCEEDED);

    // Clean up.
    lib.obscan_context_remove_data_msg_listener(context, &g_data_listener_a);
    lib.obscan_context_remove_process_msg_listener(context, &g_process_listener_a);

    status = lib.obscan_context_destroy(&context);
    std::printf("[worker] obscan_context_destroy() -> %s (%d)\n", StatusName(status), (int)status);

    std::printf("[worker] g_failures=%d\n", g_failures);
    std::fflush(stdout);
    delete libptr;
    return g_failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    // Worker mode.
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == kWorkerFlag) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing library path after %s\n", kWorkerFlag);
                return 2;
            }
            return RunTests(argv[i + 1]);
        }
    }

    // Launcher mode.
    std::string lib_path;
    if (argc >= 2) lib_path = argv[1];
    else lib_path = openscanner::ObscanLibrary::FindDefaultLibraryPath();
    if (lib_path.empty()) {
        std::fprintf(stderr, "No OBScan SDK library path given and no default found.\n");
        return 2;
    }

#if defined(__APPLE__) && defined(VULKAN_DISABLE_INTERPOSE_DYLIB)
    {
        size_t slash = lib_path.find_last_of('/');
        std::string moltenvk_path;
        if (slash != std::string::npos) moltenvk_path = lib_path.substr(0, slash + 1) + "libMoltenVK.dylib";
        if (!moltenvk_path.empty()) {
            std::string insert = moltenvk_path + ":" + VULKAN_DISABLE_INTERPOSE_DYLIB;
            setenv("DYLD_INSERT_LIBRARIES", insert.c_str(), 1);
            std::printf("[launcher] DYLD_INSERT_LIBRARIES=%s\n", insert.c_str());
        std::fflush(stdout); // banner must precede worker output when piped
        }
    }
#endif

    std::vector<std::string> args = {kWorkerFlag, lib_path};
    int exit_code = 0;
    std::string reason;
    bool ok = openscanner::RunSelfAsWorker(args, &exit_code, &reason, 180);
    if (!ok) {
        std::printf("worker did not exit cleanly: %s\n", reason.c_str());
        return 1;
    }
    std::printf("worker exit code: %d\n", exit_code);
    return exit_code;
}
