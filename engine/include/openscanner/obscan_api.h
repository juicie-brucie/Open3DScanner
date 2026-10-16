#pragma once

// Clean-room C ABI declarations for Orbbec's "OBScan" scanning SDK
// (OBScan.SDK namespace), as used by CrealityScan 4.3.2.
//
// Provenance: every name, parameter order, type and struct field layout
// below was recovered independently by decompiling CrealityScan's IL2CPP
// metadata (global-metadata.dat + GameAssembly.{dll,dylib}) with
// Il2CppDumper, and cross-checked against the real exported symbol table
// of the shipped liborbbec_scan / lib_orbbec_scan binary (nm -gU). No
// vendor header, source file or binary is reproduced here — this file
// only restates the observed calling contract, the same way any SDK
// binding is written against a third-party ABI.
//
// Struct field offsets are inferred from IL2CPP's recorded managed-struct
// layout and have not yet been independently verified against the native
// struct via disassembly; treat them as a working hypothesis until a
// smoke test confirms round-tripping (see tools/smoke_test).

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Status / error codes (OBScan.SDK.OBScanStatus)
// ---------------------------------------------------------------------------
typedef enum ObScanStatus {
    OB_SCAN_SUCCESS = 0,
    OB_SCAN_ERROR_INVALID_ARGUMENT = 845283584,
    OB_SCAN_ERROR_FATAL = 845283585,
    OB_SCAN_ERROR_DEVICE_DISCONNECT = 845283586,
    OB_SCAN_ERROR_SESSION_PAUSED = 845283587,
    OB_SCAN_ERROR_SESSION_NOT_PAUSED = 845283588,
    OB_SCAN_ERROR_UNSUPPORTED_CONFIGURATION = 845283589,
    OB_SCAN_ERROR_CAMERA_PERMISSION_NOT_GRANTED = 845283590,
    OB_SCAN_ERROR_DEADLINE_EXCEEDED = 845283591,
    OB_SCAN_ERROR_CAMERA_STREAMING_RETRY_FAILED = 845283592,
    OB_SCAN_ERROR_LISTENER_NOT_FOUND = 845283593,
    OB_SCAN_ERROR_MULTI_CALLED = 845283600,
    OB_SCAN_ERROR_CAMERA_NOT_REGISTER = 845283601,
    OB_SCAN_ERROR_OUT_MEMORY = 845283602,
    OB_SCAN_ERROR_FILE_NOT_EXIST = 845283603,
    OB_SCAN_ERROR_FILE_READ_FAILED = 845283604,
    OB_SCAN_ERROR_FILE_WRITE_FAILED = 845283605,
    OB_SCAN_ERROR_GRAPHICS_PROBLEM = 845283606,
    OB_SCAN_ERROR_HAVE_NOT_MARKER = 845283607,
    OB_SCAN_ERROR_IMPORT_EMPTY_PROJ = 845283608,
    OB_SCAN_ERROR_IMPORT_SELF_PROJ = 845283609,
    OB_SCAN_ERROR_NO_PLUG_IN_POWER = 845283617,
    OB_SCAN_ERROR_INVALID_SCAN = 845283618,
    OB_SCAN_ERROR_DEVICE_BUSY = 845283619,
    OB_SCAN_ERROR_CANCEL_RUN = 845283620,
    OB_SCAN_ERROR_CAMERA_PARAMS_INCONSISTENT = 845283621,
    OB_SCAN_ERROR_LASER_MARKER_FRAMEWORK_INVALID_CLOUD_SCAN = 845283622,
    OB_SCAN_ERROR_OUT_HARD_DISK = 845283623,
    OB_SCAN_ERROR_READ_DEVICE_PARAMS = 845283624,
    OB_SCAN_ERROR_IMPORT_NOT_SUPPORT_PROJ = 845283625,
    OB_SCAN_ERROR_IMPORT_HIGH_VERSION_PROJ = 845283632,
    OB_SCAN_ERROR_RESTORE_FACTORY_CRC_CHECK_FAILED = 845283633,
    OB_SCAN_ERROR_RESTORE_FACTORY_DATA_WRITE_FAILED = 845283634,
    OB_SCAN_ERROR_AI_HEAD_RESTORATION_NOT_DETECT_FACE = 845283635,
    OB_SCAN_ERROR_MESH_NOT_FOUND_HOLE = 845283636,
    OB_SCAN_ERROR_PROJ_NOT_WRITE_PERMISSION = 845283637,
    OB_SCAN_ERROR_SET_LED_LASER = 845283638,
    OB_SCAN_ERROR_LOADING_INTR_LASER_PARAMS = 845283639,
    OB_SCAN_ERROR_SET_IR_MODE = 845283640,
    OB_SCAN_ERROR_AI_REPLACE_RGB_FEATS_NOT_OBVIOUS = 845283648,
    OB_SCAN_ERROR_IMPORTED_MESH_SIZE_NOT_SUPPORT = 845283649,
    OB_SCAN_ERROR_MODEL_GENERATION_FAILED_WITH_NETWORK_EXCEPTION = 845283650,
    OB_SCAN_ERROR_ALLOCATE_GPU_RESOURCES_FAILED = 845283651,
} ObScanStatus;

// ---------------------------------------------------------------------------
// Mode-configuration enums (OBScan.SDK.*)
// ---------------------------------------------------------------------------
typedef enum ObScanProjection {
    OB_SCAN_INFRARED = 0,
    OB_SCAN_BLUE_LINE_LASER = 1,
} ObScanProjection;

typedef enum ObScanLightType {
    OB_SCAN_LIGHT_TYPE_PARALLEL = 0,
    OB_SCAN_LIGHT_TYPE_CROSS = 1,
    OB_SCAN_LIGHT_TYPE_SINGLE = 2,
} ObScanLightType;

typedef enum ObScanScanType {
    OB_SCAN_CLOUD_FUSED = 0,
    OB_SCAN_LASER_DETAILED = 1,
    OB_SCAN_MARKER_FRAMEWORK = 2,
} ObScanScanType;

typedef enum ObScanMode {
    OB_SCAN_MODE_FACE = 0,
    OB_SCAN_MODE_OBJECT_SMALL = 1,
    OB_SCAN_MODE_OBJECT_MEDIUM = 2,
    OB_SCAN_MODE_OBJECT_BIG = 3,
    OB_SCAN_MODE_HUMAN = 4,
} ObScanMode;

typedef enum ObScanFuseMode {
    OB_SCAN_FUSE_MODE_GEOMETRY = 0,
    OB_SCAN_FUSE_MODE_RGBD = 1,
    OB_SCAN_FUSE_MODE_MARKER = 2,
    OB_SCAN_FUSE_MODE_MARKERLESS = 3,
} ObScanFuseMode;

typedef enum ObScanAccuracy {
    OB_SCAN_ACCURACY_FAST = 0,
    OB_SCAN_ACCURACY_HIGH = 1,
} ObScanAccuracy;

typedef enum ObScanTexture {
    OB_SCAN_HAVE_TEXTURE = 0,
    OB_SCAN_NO_TEXTURE = 1,
} ObScanTexture;

typedef enum ObScanFov {
    OB_SCAN_FOV_NORMAL = 0,
    OB_SCAN_FOV_WIDE = 1,
} ObScanFov;

typedef enum ObScanFrameRate {
    OB_SCAN_FRAME_RATE_AUTO = 0,
    OB_SCAN_FRAME_RATE_STANDARD = 1,
    OB_SCAN_FRAME_RATE_BALANCED = 2,
    OB_SCAN_FRAME_RATE_FAST = 3,
} ObScanFrameRate;

typedef enum ObScanObjectMaterial {
    OB_SCAN_OBJECT_MATERIAL_STANDARD = 0,
    OB_SCAN_OBJECT_MATERIAL_BLACK = 1,
    OB_SCAN_OBJECT_MATERIAL_WHITE = 2,
    OB_SCAN_OBJECT_MATERIAL_HIGHLY_REFLECTIVE = 3,
} ObScanObjectMaterial;

// ---------------------------------------------------------------------------
// Structs (field order/offsets recovered from IL2CPP struct layout)
// ---------------------------------------------------------------------------

// OBScan.SDK.OBScanModeConfig — 0x38 bytes
typedef struct ObScanModeConfig {
    ObScanTexture post_texture;          // 0x00
    ObScanFrameRate frame_rate;          // 0x04
    ObScanProjection projection;         // 0x08
    ObScanMode scan_mode;                // 0x0C
    ObScanAccuracy accuracy;             // 0x10
    ObScanFuseMode fuse_mode;            // 0x14
    ObScanFov scan_fov;                  // 0x18
    ObScanObjectMaterial object_material;// 0x1C
    double voxel_size;                   // 0x20
    ObScanLightType light_type;          // 0x28
    ObScanScanType scan_type;            // 0x2C
    double detail_voxel_size;            // 0x30
} ObScanModeConfig;

// OBScan.SDK.OBScanContextConfig — all fields are native string pointers.
// Marshaling (UTF-8 vs. platform ANSI) is unconfirmed; validate in the
// smoke test before relying on non-ASCII paths.
typedef struct ObScanContextConfig {
    const char* input_algo_dependent_files_dir;  // 0x00
    const char* output_ob_sdk_log_dir;            // 0x08
    const char* output_ob_scan_log_dir;           // 0x10
    const char* output_computer_performance_fps_dir; // 0x18
    const char* output_windows_gpu_info_dir;      // 0x20 (Windows-only; pass NULL on macOS)
} ObScanContextConfig;

// OBScan.SDK.OBScanDeviceInfo — 0x48 bytes
typedef struct ObScanDeviceInfo {
    int32_t camera_pid;                   // 0x00
    const char* camera_name;              // 0x08
    const char* camera_serial_number;     // 0x10
    const char* camera_connection_type;   // 0x18
    const char* camera_firmware_version;  // 0x20
    int32_t wifi_device_pid;              // 0x28
    const char* wifi_device_ssid;         // 0x30
    const char* wifi_device_firmware_version; // 0x38
    uint32_t device_calibration_timestamp;    // 0x40
} ObScanDeviceInfo;

// OBScan.SDK.OBScanSessionConfig — all three fields are native pointers/bool.
typedef struct ObScanSessionConfig {
    const char* output_dir;          // 0x00
    const char* name;                // 0x08
    bool is_import_mesh_file;        // 0x10
} ObScanSessionConfig;

// OBScan.SDK.OBScanConfig — wraps a mode config plus a display name for one
// scan within a session.
typedef struct ObScanConfig {
    ObScanModeConfig mode_config;    // 0x00
    const char* name;                // 0x38
} ObScanConfig;

// OBScan.SDK.OBScanPoint / OBScanNormal / OBScanColor — per-vertex data used
// by the point-cloud and mesh accessors below.
typedef struct ObScanPoint {
    float x, y, z, intensity;
} ObScanPoint;

typedef struct ObScanNormal {
    float nx, ny, nz, nw;
} ObScanNormal;

typedef struct ObScanColor {
    uint8_t r, g, b, a;
} ObScanColor;

// OBScan.SDK.OBScanPointClouds — the raw live/accumulated point cloud for a
// scan. `points` and `normals` are vendor-owned buffers of `num_points`
// entries each; lifetime/ownership (who frees them, and when they become
// invalid relative to the next SDK call) is not yet confirmed — treat as
// read-only and copy out immediately after the call that returns it.
typedef struct ObScanPointClouds {
    uint32_t num_points;    // 0x00
    ObScanPoint* points;    // 0x08
    ObScanNormal* normals;  // 0x10
} ObScanPointClouds;

// OBScan.SDK.OBScanPointColorClouds — point cloud with per-point color.
typedef struct ObScanPointColorClouds {
    uint32_t num_points;    // 0x00
    ObScanPoint* points;    // 0x08
    ObScanNormal* normals;  // 0x10
    ObScanColor* colors;    // 0x18
} ObScanPointColorClouds;

// OBScan.SDK.OBScanTriangle — a face as three vertex indices into a mesh's
// vertex array.
typedef struct ObScanTriangle {
    uint32_t v0, v1, v2;
} ObScanTriangle;

// OBScan.SDK.OBScanTriangleMesh — the live/preview reconstructed mesh for a
// scan (see obscan_scan_get_realtime_model_mesh). Same buffer-lifetime
// caveat as OBScanPointClouds above.
typedef struct ObScanTriangleMesh {
    uint32_t num_vertices;      // 0x00
    ObScanPoint* vertices;      // 0x08
    ObScanNormal* normals;      // 0x10
    uint32_t num_triangles;     // 0x18
    ObScanTriangle* triangles;  // 0x20
} ObScanTriangleMesh;

// ---------------------------------------------------------------------------
// Opaque handles
// ---------------------------------------------------------------------------
typedef void* ObScanContextHandle;
typedef void* ObScanDeviceManagementHandle;
typedef void* ObScanSessionHandle;
typedef void* ObScanScanHandle;

// ---------------------------------------------------------------------------
// Callback delegate shapes (OBScan.SDK.*CallbackDelegate). IL2CPP records
// ProgressCallbackDelegate/StatusCallbackDelegate as plain C-ABI function
// pointers (StatusCallbackDelegate is explicitly marked Cdecl in the
// metadata; the others follow the same convention). Whether NULL is
// accepted for an "optional-looking" callback parameter is not confirmed —
// that's inferred from the parameter being omittable on the C# call site,
// not verified against the native implementation. Treat as unverified
// until a smoke test confirms it for a given call.
// ---------------------------------------------------------------------------
typedef void (*ObScanProgressCallback)(float progress);
typedef void (*ObScanStatusCallback)(ObScanStatus status);

// ---------------------------------------------------------------------------
// OBScan.SDK.OBScanDeviceMessage / OBScanDeviceMessageDelegate /
// OBScanDeviceMessageListener (IL2CPP TypeDefIndex 12707-12709).
//
// This is the actual trigger for device detection: obscan_context_create()
// and obscan_context_get_device_management() do NOT start the vendor SDK's
// IOKit device-watcher thread by themselves (confirmed by Frida-tracing
// IOServiceAddMatchingNotification/CFRunLoopAddSource against our own
// process: zero registrations happen without a listener). The watcher
// thread is only started as a side effect of registering a real listener
// via obscan_device_management_add_device_msg_listener(). Once a device is
// physically connected, the SDK delivers a message to onMessage (confirmed:
// msg_id=0 with a non-NULL data pointer corresponds to a real connect
// event, and obscan_device_management_get_device_num() called synchronously
// inside that callback correctly returns count=1). A spurious-looking first
// callback (msg_id=308412672, data=NULL) fires immediately upon
// registration on every run so far — likely a registration acknowledgment,
// meaning unspecific/unconfirmed; treat unknown msg_id values as
// non-actionable rather than assuming they're new connect/disconnect types.
//
// Known hazard: in standalone testing, the real connect-event callback is
// reproducibly followed by a crash inside the vendor SDK shortly after
// returning from onMessage. This is the same class of fragility as the
// obscan_context_destroy() crash elsewhere in this SDK (vendor code that
// assumes a full Unity/Cocoa runtime context) — callers must run any code
// that registers this listener inside a disposable worker process (see
// worker_process.h), not in a process that cannot afford to crash.
// ---------------------------------------------------------------------------
typedef struct ObScanDeviceMessage {
    int32_t msg_id;  // 0x00 — OBScanDeviceMessageID; only 0 (device connect) confirmed so far.
    int32_t _pad;    // 0x04 — alignment padding to 8-byte boundary for `data`.
    void* data;      // 0x08 — NULL on the spurious registration callback; non-NULL on a real connect event.
} ObScanDeviceMessage;

typedef void (*ObScanDeviceMessageDelegate)(ObScanDeviceMessage msg);

typedef struct ObScanDeviceMessageListener {
    ObScanDeviceMessageDelegate onMessage;  // 0x00
} ObScanDeviceMessageListener;

// OBScan.SDK.OBScanDataMessage* / OBScanProcessMessage* — same shape as the
// device-message family above. obscan_scan_preview() returns
// OB_SCAN_ERROR_LISTENER_NOT_FOUND unless a data-message listener (and,
// per the real app's own call log, a process-message listener too) is
// registered on the context before the scan is created: preview delivers
// new RGB/depth/IR frames and FRAME_READY through this listener rather
// than letting the caller poll for them.
// OBScan.SDK.OBScanDataMessageID.
typedef enum ObScanDataMessageID {
    OB_SCAN_MESSAGE_ID_NEW_RGB_IMAGE = 0,
    OB_SCAN_MESSAGE_ID_NEW_DEPTH_IMAGE = 325189888,
    OB_SCAN_MESSAGE_ID_NEW_IR_IMAGE = 325189889,
    OB_SCAN_MESSAGE_ID_FRAME_READY = 325189890,
} ObScanDataMessageID;

typedef struct ObScanDataMessage {
    int32_t msg_id;  // 0x00 — ObScanDataMessageID.
    int32_t _pad;    // 0x04
    void* data;      // 0x08
} ObScanDataMessage;

typedef void (*ObScanDataMessageDelegate)(ObScanDataMessage msg);

typedef struct ObScanDataMessageListener {
    ObScanDataMessageDelegate onMessage;  // 0x00
} ObScanDataMessageListener;

typedef struct ObScanProcessMessage {
    int32_t msg_id;  // 0x00 — ObScanProcessMessageID (FRAME_BUFFER_FULL, MAX_FRAMES_REACHED, DETECT_FACE, SCANNING_FAILED, ...).
    int32_t _pad;    // 0x04
    void* data;      // 0x08
} ObScanProcessMessage;

typedef void (*ObScanProcessMessageDelegate)(ObScanProcessMessage msg);

typedef struct ObScanProcessMessageListener {
    ObScanProcessMessageDelegate onMessage;  // 0x00
} ObScanProcessMessageListener;

// ---------------------------------------------------------------------------
// Function pointer types for the subset of the obscan_* C ABI needed by the
// v1 smoke test (bootstrap + device enumeration). Resolved at runtime via
// ObscanLibrary (see obscan_loader.h) — this project never links the vendor
// library at build time.
// ---------------------------------------------------------------------------
typedef ObScanStatus (*fn_obscan_version)(const char** out_version);
typedef ObScanStatus (*fn_obscan_context_create)(ObScanContextHandle* out_context, ObScanContextConfig* config);
typedef ObScanStatus (*fn_obscan_context_destroy)(ObScanContextHandle* context);
typedef ObScanStatus (*fn_obscan_context_get_device_management)(ObScanContextHandle context, ObScanDeviceManagementHandle* out_device_handle);

// Registering these on the context is required before obscan_scan_preview()
// will succeed — without them it returns OB_SCAN_ERROR_LISTENER_NOT_FOUND,
// confirmed by diffing our own call sequence against the real app's own
// obscan_* call log. Preview delivers frames/process events through these
// listeners rather than a polling API.
typedef ObScanStatus (*fn_obscan_context_add_data_msg_listener)(ObScanContextHandle context, ObScanDataMessageListener* listener);
typedef ObScanStatus (*fn_obscan_context_add_process_msg_listener)(ObScanContextHandle context, ObScanProcessMessageListener* listener);

typedef ObScanStatus (*fn_obscan_device_management_get_device_num)(ObScanDeviceManagementHandle device_handle, int32_t* out_device_num);
typedef ObScanStatus (*fn_obscan_device_management_get_device_info)(ObScanDeviceManagementHandle device_handle, int32_t idx, ObScanDeviceInfo* out_device_info);
typedef ObScanStatus (*fn_obscan_device_management_get_active_idx)(ObScanDeviceManagementHandle device_handle, int32_t* out_active_idx);
typedef ObScanStatus (*fn_obscan_device_management_select_active_device)(ObScanDeviceManagementHandle device_handle, int32_t active_idx);

// The actual trigger for device detection — see the ObScanDeviceMessage*
// block above. `listener` must stay alive for as long as the SDK might
// still invoke it; this project always owns one for the lifetime of the
// context that registered it.
typedef ObScanStatus (*fn_obscan_device_management_add_device_msg_listener)(ObScanDeviceManagementHandle device_handle, ObScanDeviceMessageListener* listener);

// Session / scan lifecycle — the live-capture path this project actually
// cares about: context -> session -> scan -> start -> poll raw cloud ->
// stop -> destroy. Recovered the same way as the bootstrap subset above
// (IL2CPP decompilation, cross-checked symbol names against the real
// exported binary); struct field offsets are not yet independently
// confirmed via disassembly for this subset the way the bootstrap structs
// were — validate against real capture data before trusting field order
// blindly in a release build.
typedef ObScanStatus (*fn_obscan_context_create_session)(ObScanContextHandle context, ObScanSessionConfig* config, ObScanSessionHandle* out_session);
typedef ObScanStatus (*fn_obscan_session_destroy)(ObScanSessionHandle* session);
typedef ObScanStatus (*fn_obscan_session_create_scan)(ObScanSessionHandle session, ObScanConfig* config, ObScanScanHandle* out_scan);
// The real CrealityScan app always calls this before obscan_scan_start —
// confirmed by capturing its own obscan_* call log during a live scan
// (see live_capture_test.cpp for the full story). obscan_scan_start on a
// scan that was never previewed fails with OB_SCAN_ERROR_INVALID_ARGUMENT
// and the native log reason "cannot called start !": the laser/camera
// must already be actively streaming (a live preview) before the scan
// can transition into the recording state.
//
// IL2CPP's managed wrapper only exposes `Preview(Action<OBScanStatus>
// statusCallback)`, which looks like a single-callback signature — but
// OBScanTarget (the class that wraps this native call) separately defines
// static trampolines for BOTH a ProgressCallbackDelegate and a
// StatusCallbackDelegate ([MonoPInvokeCallback(typeof(...))]). Passing
// only one callback pointer here silently misaligns the real two-
// callback native signature: the garbage "status" values we saw (e.g.
// 10281832) were actually stale integer-register contents from calling
// our function as if it were the *progress* callback (a float, passed
// in an FP register under AAPCS64, read back as a bogus int), and
// passing NULL crashed when the SDK tried to invoke the still-missing
// second (real status) callback.
typedef ObScanStatus (*fn_obscan_scan_preview)(ObScanScanHandle scan, ObScanProgressCallback progress_callback,
                                                ObScanStatusCallback status_callback);
typedef ObScanStatus (*fn_obscan_scan_start)(ObScanScanHandle scan);
// OBScanTarget.PauseScan() takes NO parameters beyond the implicit
// handle — our earlier `is_stop_stream` bool was invented, not
// recovered from the real ABI. Likely harmless under AAPCS64 (an unused
// extra argument just sits in a register the real function never
// reads), but it was never real and is removed for accuracy.
typedef ObScanStatus (*fn_obscan_scan_pause)(ObScanScanHandle scan);
// OBScanTarget.StopScan(bool isBlock = false, Action<OBScanStatus>
// statusCallback) — two params, isBlock FIRST, no progress callback at
// all. Our earlier 4-arg (scan, progress_cb, status_cb, is_block) had
// both the wrong count and the wrong order; this is the likely cause of
// the reliable OB_SCAN_ERROR_INVALID_SCAN seen calling the old shape.
typedef ObScanStatus (*fn_obscan_scan_stop)(ObScanScanHandle scan, bool is_block, ObScanStatusCallback status_callback);
typedef ObScanStatus (*fn_obscan_scan_get_raw_cloud)(ObScanScanHandle scan, ObScanPointClouds* out_cloud);
typedef ObScanStatus (*fn_obscan_scan_get_realtime_model_mesh)(ObScanScanHandle scan, ObScanTriangleMesh* out_mesh);
typedef ObScanStatus (*fn_obscan_scan_get_mode_config)(ObScanScanHandle scan, ObScanModeConfig* out_config);

// OBScanTarget.ConfigCombinedExposureAndLaserCurrent(int value) — a
// single combined IR-exposure/laser-current slider, range [1, 1000]
// (OBScanRealTimeConfig.combined_exposure_and_laser_current; ground
// truth for this device's LaserFrameRenderParam: default ~500, last
// used 540). Confirmed necessary: physical markers for fuse_mode=Marker
// went undetected in the real CrealityScan app until exposure was
// turned up well above default.
typedef ObScanStatus (*fn_obscan_scan_config_combined_exposure_and_laser_current)(ObScanScanHandle scan,
                                                                                   int32_t value);

#ifdef __cplusplus
}  // extern "C"
#endif
