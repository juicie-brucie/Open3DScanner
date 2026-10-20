// A test double for the OBScan SDK.
//
// Why it exists: row #130's contract says a hardware test must tell "the
// precondition is not met" (skip, exit 2) from "the run failed" (exit 1).
// The failure half needs a library that loads but refuses to create a
// context — the vendor SDK cannot be put in that state on demand, and it
// must not be loaded for a device-free check anyway.
//
// What it is NOT: it models no vendor behaviour. Every entry point except
// obscan_context_create() returns OB_SCAN_ERROR_UNSUPPORTED_CONFIGURATION,
// and obscan_context_create() returns OB_SCAN_ERROR_FATAL. A hardware run
// never uses it: the real SDK is the one under test there (see
// tests/hardware/precondition_evidence.sh, which is the only caller).
//
// The loader resolves each entry point by name and casts it to the header's
// fn_obscan_* type, so every stub is checked against that type: a header
// change breaks this build instead of letting the stub drift.

#include <type_traits>

#include "openscanner/obscan_api.h"

namespace {

constexpr ObScanStatus kStubStatus = OB_SCAN_ERROR_UNSUPPORTED_CONFIGURATION;

}  // namespace

#define ASSERT_SIGNATURE(name, fn_type) \
    static_assert(std::is_same<decltype(&name), fn_type>::value, #name " drifted from the header")

extern "C" {

ObScanStatus obscan_version(const char** out_version) {
    if (out_version != nullptr) *out_version = "stub-0.0";
    return OB_SCAN_SUCCESS;
}
ASSERT_SIGNATURE(obscan_version, fn_obscan_version);

// The one entry point with a meaning here: the SDK is there and it refuses.
ObScanStatus obscan_context_create(ObScanContextHandle* out_context, ObScanContextConfig* config) {
    if (out_context != nullptr) *out_context = nullptr;
    return OB_SCAN_ERROR_FATAL;
}
ASSERT_SIGNATURE(obscan_context_create, fn_obscan_context_create);

ObScanStatus obscan_context_destroy(ObScanContextHandle* context) { return kStubStatus; }
ASSERT_SIGNATURE(obscan_context_destroy, fn_obscan_context_destroy);

ObScanStatus obscan_context_get_device_management(ObScanContextHandle context,
                                                   ObScanDeviceManagementHandle* out_device_handle) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_context_get_device_management, fn_obscan_context_get_device_management);

ObScanStatus obscan_context_add_data_msg_listener(ObScanContextHandle context,
                                                   ObScanDataMessageListener* listener) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_context_add_data_msg_listener, fn_obscan_context_add_data_msg_listener);

ObScanStatus obscan_context_add_process_msg_listener(ObScanContextHandle context,
                                                      ObScanProcessMessageListener* listener) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_context_add_process_msg_listener, fn_obscan_context_add_process_msg_listener);

ObScanStatus obscan_context_remove_data_msg_listener(ObScanContextHandle context,
                                                      ObScanDataMessageListener const* listener) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_context_remove_data_msg_listener, fn_obscan_context_remove_data_msg_listener);

ObScanStatus obscan_context_remove_process_msg_listener(ObScanContextHandle context,
                                                         ObScanProcessMessageListener const* listener) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_context_remove_process_msg_listener, fn_obscan_context_remove_process_msg_listener);

ObScanStatus obscan_device_management_get_device_num(ObScanDeviceManagementHandle device_handle,
                                                      int32_t* out_device_num) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_device_management_get_device_num, fn_obscan_device_management_get_device_num);

ObScanStatus obscan_device_management_get_device_info(ObScanDeviceManagementHandle device_handle, int32_t idx,
                                                       ObScanDeviceInfo* out_device_info) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_device_management_get_device_info, fn_obscan_device_management_get_device_info);

ObScanStatus obscan_device_management_get_active_idx(ObScanDeviceManagementHandle device_handle,
                                                      int32_t* out_active_idx) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_device_management_get_active_idx, fn_obscan_device_management_get_active_idx);

ObScanStatus obscan_device_management_select_active_device(ObScanDeviceManagementHandle device_handle,
                                                            int32_t active_idx) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_device_management_select_active_device, fn_obscan_device_management_select_active_device);

ObScanStatus obscan_device_management_add_device_msg_listener(ObScanDeviceManagementHandle device_handle,
                                                               ObScanDeviceMessageListener* listener) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_device_management_add_device_msg_listener,
                 fn_obscan_device_management_add_device_msg_listener);

ObScanStatus obscan_context_create_session(ObScanContextHandle context, ObScanSessionConfig* config,
                                            ObScanSessionHandle* out_session) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_context_create_session, fn_obscan_context_create_session);

ObScanStatus obscan_session_destroy(ObScanSessionHandle* session) { return kStubStatus; }
ASSERT_SIGNATURE(obscan_session_destroy, fn_obscan_session_destroy);

ObScanStatus obscan_session_create_scan(ObScanSessionHandle session, ObScanConfig* config,
                                         ObScanScanHandle* out_scan) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_session_create_scan, fn_obscan_session_create_scan);

ObScanStatus obscan_scan_preview(ObScanScanHandle scan, ObScanProgressCallback progress_callback,
                                   ObScanStatusCallback status_callback) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_scan_preview, fn_obscan_scan_preview);

ObScanStatus obscan_scan_start(ObScanScanHandle scan) { return kStubStatus; }
ASSERT_SIGNATURE(obscan_scan_start, fn_obscan_scan_start);

ObScanStatus obscan_scan_pause(ObScanScanHandle scan) { return kStubStatus; }
ASSERT_SIGNATURE(obscan_scan_pause, fn_obscan_scan_pause);

ObScanStatus obscan_scan_stop(ObScanScanHandle scan, ObScanProgressCallback progress_callback,
                                ObScanStatusCallback status_callback, bool is_block) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_scan_stop, fn_obscan_scan_stop);

ObScanStatus obscan_scan_get_raw_cloud(ObScanScanHandle scan, ObScanPointClouds* out_cloud) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_scan_get_raw_cloud, fn_obscan_scan_get_raw_cloud);

ObScanStatus obscan_scan_get_model_point_color_clouds(ObScanScanHandle scan, ObScanPointColorClouds* out_clouds,
                                                        ObScanCloudBbox* out_bbox, uint32_t* out_count) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_scan_get_model_point_color_clouds, fn_obscan_scan_get_model_point_color_clouds);

ObScanStatus obscan_scan_model_clouds_release(ObScanScanHandle scan) { return kStubStatus; }
ASSERT_SIGNATURE(obscan_scan_model_clouds_release, fn_obscan_scan_model_clouds_release);

ObScanStatus obscan_scan_get_realtime_model_mesh(ObScanScanHandle scan, ObScanTriangleMesh* out_mesh) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_scan_get_realtime_model_mesh, fn_obscan_scan_get_realtime_model_mesh);

ObScanStatus obscan_scan_realtime_model_mesh_release(ObScanScanHandle scan) { return kStubStatus; }
ASSERT_SIGNATURE(obscan_scan_realtime_model_mesh_release, fn_obscan_scan_realtime_model_mesh_release);

ObScanStatus obscan_scan_get_mode_config(ObScanScanHandle scan, ObScanModeConfig* out_config) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_scan_get_mode_config, fn_obscan_scan_get_mode_config);

ObScanStatus obscan_scan_plane_remove(ObScanScanHandle scan, bool enabled) { return kStubStatus; }
ASSERT_SIGNATURE(obscan_scan_plane_remove, fn_obscan_scan_plane_remove);

ObScanStatus obscan_scan_turntable_plane_remove(ObScanScanHandle scan, bool enabled) { return kStubStatus; }
ASSERT_SIGNATURE(obscan_scan_turntable_plane_remove, fn_obscan_scan_turntable_plane_remove);

ObScanStatus obscan_scan_config_manaul_remove_plane(ObScanScanHandle scan, const ObScanManualRPConfig* config) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_scan_config_manaul_remove_plane, fn_obscan_scan_config_manaul_remove_plane);

ObScanStatus obscan_scan_get_global_pose(ObScanScanHandle scan, ObScanPose* out_pose) { return kStubStatus; }
ASSERT_SIGNATURE(obscan_scan_get_global_pose, fn_obscan_scan_get_global_pose);

ObScanStatus obscan_scan_get_registration_pose(ObScanScanHandle scan, ObScanPose* out_pose) { return kStubStatus; }
ASSERT_SIGNATURE(obscan_scan_get_registration_pose, fn_obscan_scan_get_registration_pose);

ObScanStatus obscan_frame_get_frame_tracking_status(ObScanFrameHandle frame, int32_t* out_status) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_frame_get_frame_tracking_status, fn_obscan_frame_get_frame_tracking_status);

ObScanStatus obscan_frame_release(ObScanFrameHandle frame) { return kStubStatus; }
ASSERT_SIGNATURE(obscan_frame_release, fn_obscan_frame_release);

ObScanStatus obscan_scan_config_combined_exposure_and_laser_current(ObScanScanHandle scan, int32_t value) {
    return kStubStatus;
}
ASSERT_SIGNATURE(obscan_scan_config_combined_exposure_and_laser_current,
                 fn_obscan_scan_config_combined_exposure_and_laser_current);

}  // extern "C"
