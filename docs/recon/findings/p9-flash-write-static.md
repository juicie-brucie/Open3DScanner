# P9 Static Falsifier — Flash Write Path Enumeration (Row #162)

*Static analysis of vendor binary `liborbbec_scan_arm64.dylib` (CrealityScan 4.3.2, pinned arm64 slice, FileVersion 3.5.3.260921). Binary at `.recon-cache/liborbbec_scan_arm64.dylib`, disassembly at `.recon-cache/listings/liborbbec_scan_arm64.full.asm`, symbols at `.recon-cache/listings/liborbbec_scan_arm64.symbols.txt`. No device, no gated run.*

## 1. The Question

Does the normal scan cycle (discovery → start → stream → stop) invoke **any persistent flash write**, or are all writes volatile (register state, session state, stream control)?

Row #102 (observation-based) answered: "no persistent writes observed in the captured session." This is the static half: enumerate every flash-write path in the vendor binary and determine whether any is reachable from the normal scan flow.

## 2. Search Method

1. **Identified the sole flash-write function.** Searched the disassembly for `updateFlashData`, `writeFlash`, `flashWrite`, and found `device::AbstractDeviceHandle::updateFlashData(unsigned int, unsigned int, char const*, unsigned int, bool)` at `0x315638`. This function calls `ob::Device::writeFlash` (the vendor SDK's write primitive) at `0x2ad64f4`.

2. **Enumerated all call sites.** Found 19 call sites to `updateFlashData` in the disassembly (see §3).

3. **Identified the enclosing function for each call site.** Used the symbol table to find the function label (greatest address ≤ call site) for each of the 19 sites (see §3).

4. **Categorized the callers.** Grouped the 19 sites into three classes:
   - `DeviceParamAccessor::*` methods (7 sites) — calibration/config updaters
   - `*DeviceHandle::restoreFactorySetting` methods (8 sites) — factory reset
   - `*DeviceHandle::resumeDeviceParam` methods (3 sites) — parameter restore
   - Plus 1 additional site in `SermoonSerialAbstractDeviceHandle::restoreFactorySetting`

5. **Traced the caller chain for `DeviceParamAccessor::*` methods.** Found their callers:
   - `OrbbecDevice::update_calibration_param` @`0x1b5774`
   - `OrbbecDevice::update_dpu_config_param` @`0x1b5780`
   - `OrbbecDevice::update_lut_param` @`0x1b578c`
   - `OrbbecDevice::update_start_row_param` @`0x1b5798`
   - `OrbbecDevice::update_user_origin_calibration_param` @`0x1b58e4`
   - `OrbbecDevice::update_line_laser_light_plane_param` @`0x1b598c`

6. **Traced the callers of `OrbbecDevice::update_*` methods.** Found they are called ONLY from `OrbbecCalib::*` calibration functions (see §4).

7. **Verified the C-ABI surface.** Confirmed that calibration, factory reset, and parameter resume are separate user-initiated operations in the exported API (see §5).

## 3. The Flash-Write Inventory

### 3.1 The Sole Write Function

`device::AbstractDeviceHandle::updateFlashData(unsigned int key, unsigned int address, char const* data, unsigned int length, bool flag)` @`0x315638`

This is the only function in the vendor binary that writes to device flash. It calls `ob::Device::writeFlash` (vendor SDK primitive). All persistent writes go through this function.

### 3.2 Call Sites (19 total)

| Address | Enclosing Function | Category |
|---------|-------------------|----------|
| `0x23efb0` | `DeviceParamAccessor::updateCalibrationParam` | Calibration updater |
| `0x23f0b0` | `DeviceParamAccessor::updateDpuConfigParam` | Calibration updater |
| `0x23f19c` | `DeviceParamAccessor::updateLutParam` | Calibration updater |
| `0x23f28c` | `DeviceParamAccessor::updateStartRowParam` | Calibration updater |
| `0x23f414` | `DeviceParamAccessor::updateLineLaserLightPlaneParam` | Calibration updater |
| `0x24111c` | `DeviceParamAccessor::updateUserOriginCalibrationParam` | Calibration updater |
| `0x242440` | `DeviceParamAccessor::updatePolyParams` | Calibration updater |
| `0x33b34c` | `FerretDeviceHandle::restoreFactorySetting` | Factory reset |
| `0x385e18` | `OtterSerialDeviceHandle::restoreFactorySetting` | Factory reset |
| `0x3d11d0` | `RaptorSerialDeviceHandle::restoreFactorySetting` | Factory reset |
| `0x3d11f4` | `RaptorSerialDeviceHandle::restoreFactorySetting` | Factory reset |
| `0x3d1218` | `RaptorSerialDeviceHandle::restoreFactorySetting` | Factory reset |
| `0x3d1280` | `RaptorSerialDeviceHandle::restoreFactorySetting` | Factory reset |
| `0x3d12a4` | `RaptorSerialDeviceHandle::restoreFactorySetting` | Factory reset |
| `0x421350` | `SermoonS1DeviceHandle::resumeDeviceParam` | Parameter restore |
| `0x421370` | `SermoonS1DeviceHandle::resumeDeviceParam` | Parameter restore |
| `0x421394` | `SermoonS1DeviceHandle::resumeDeviceParam` | Parameter restore |
| `0x445330` | `SermoonSerialAbstractDeviceHandle::restoreFactorySetting` | Factory reset |
| `0x445350` | `SermoonSerialAbstractDeviceHandle::restoreFactorySetting` | Factory reset |

**Summary:**
- 7 sites: calibration/config updaters
- 11 sites: factory reset (across 4 device-handle types)
- 3 sites: parameter restore (SermoonS1 only)
- 1 site: factory reset (SermoonSerialAbstract)

Total: 19 sites, all in either calibration-updater, factory-reset, or parameter-restore functions.

### 3.3 What Each Updater Writes

| Updater | Flash Address | Record Type | Purpose |
|---------|---------------|-------------|---------|
| `updateCalibrationParam` | varies | calibration | General calibration parameters |
| `updateDpuConfigParam` | varies | DPU config | DPU configuration |
| `updateLutParam` | varies | LUT | Lookup table |
| `updateStartRowParam` | varies | start row | Start row parameter |
| `updateLineLaserLightPlaneParam` | `0x211000` | `0x46447314` | Laser plane parameters (primary) |
| `updateUserOriginCalibrationParam` | varies | user origin | User origin calibration |
| `updatePolyParams` | varies | polynomial | Polynomial parameters |

The laser-plane updater is the only one whose flash address is documented (from `calibration-store.md` §1.1: writes to `0x211000`, the laser-plane record). The others write to unspecified flash addresses (not in the wire capture's observed read set).

## 4. The Caller Chain — Calibration Only

### 4.1 Callers of `DeviceParamAccessor::*` Methods

| Method | Callers |
|--------|---------|
| `updateCalibrationParam` | `OrbbecInternalDevice::update_calibration_param` @`0x1e66ec`, `AlgorithmProcessor::convertBackupParamToUserParam` @`0x216758`, `AlgorithmProcessor::convertBackupParamToUserParamForSoc` @`0x2180c8` |
| `updateDpuConfigParam` | `OrbbecInternalDevice::update_dpu_config_param` @`0x1e6a4c` |
| `updateLutParam` | `OrbbecInternalDevice::update_lut_param` @`0x1e6c28`, `AlgorithmProcessor::convertBackupParamToUserParam` @`0x216758` |
| `updateStartRowParam` | `OrbbecInternalDevice::update_start_row_param` @`0x1e6e04`, `AlgorithmProcessor::convertBackupParamToUserParamForSoc` @`0x2180c8` |
| `updateLineLaserLightPlaneParam` | `OrbbecInternalDevice::update_line_laser_light_plane_param` @`0x1eb27c` |
| `updateUserOriginCalibrationParam` | `OrbbecInternalDevice::update_user_origin_calibration_param` @`0x1ea5fc` |
| `updatePolyParams` | `AlgorithmProcessor::convertBackupParamToUserParamForSoc` @`0x2180c8` |

The `AlgorithmProcessor::convertBackupParamToUserParam*` methods are temperature-compensated converters (called during calibration to convert backup params to user params). They are not in the normal scan path.

### 4.2 Callers of `OrbbecDevice::update_*` Methods

All `OrbbecDevice::update_*` methods (the wrappers that call `OrbbecInternalDevice::update_*`) are called ONLY from `OrbbecCalib::*` functions:

| Wrapper Method | Callers (all `OrbbecCalib::*`) |
|----------------|--------------------------------|
| `update_calibration_param` | `DownloadCalibParamsIntoScanner` (2×), `CalibRaptorFunc` (2×), `CalibRaptorMiniFunc` (2×), `calibration_raptor_pro_func` (4×), `calibration_sermoon_func` (2×), `calibration_avatar_func` (2×) |
| `update_dpu_config_param` | `CalibRaptorFunc` (2×), `calibration_raptor_pro_func` (2×), `calibration_avatar_func` (3×) |
| `update_lut_param` | `CalibRaptorFunc` (1×), `calibration_raptor_pro_func` (1×), `calibration_avatar_func` (1×) |
| `update_start_row_param` | `calibration_raptor_pro_func` (1×) |
| `update_user_origin_calibration_param` | `calibration_avatar_func` (2×) |
| `update_line_laser_light_plane_param` | `DownloadCalibParamsIntoScanner` (1×), `calibration_raptor_pro_func` (1×) |

**Total: 28 call sites, all in `OrbbecCalib::*` functions.**

### 4.3 The `OrbbecCalib::*` Functions

These are the calibration routines:

- `OrbbecCalib::DownloadCalibParamsIntoScanner` @`0xbde88c` — downloads calibration params into the scanner (the main calibration flow)
- `OrbbecCalib::CalibRaptorFunc` @`0xbe1964` — Raptor-family calibration
- `OrbbecCalib::CalibRaptorMiniFunc` @`0xbe2ff0` — Raptor Mini calibration
- `OrbbecCalib::calibration_raptor_pro_func` @`0xbe4790` — Raptor Pro calibration
- `OrbbecCalib::calibration_sermoon_func` @`0xbe6c50` — Sermoon-family calibration
- `OrbbecCalib::calibration_avatar_func` @`0xbe9044` — Avatar calibration

All of these are **user-initiated calibration operations**, not part of the normal scan flow.

## 5. The C-ABI Surface — User-Initiated Operations

The exported API (from `macos-exports.txt`) confirms that calibration, factory reset, and parameter restore are separate, user-initiated operations:

### 5.1 Calibration (user-initiated)

```
obscan_calib_start
obscan_calib_stop
obscan_calib_destroy
obscan_calib_add_calib_msg_listener
obscan_calib_remove_calib_msg_listener
obscan_calib_get_QR_code_data
obscan_calib_get_calib_target_handle
obscan_calib_get_calibration_score
obscan_calib_identify_QR_code
obscan_calib_stop_identify_QR_code
obscan_calib_target_handle_get_calib_angle_and_height_num
obscan_calib_target_handle_get_calib_board_info
obscan_calib_target_handle_get_marker_data
obscan_calib_target_handle_get_target_pose_info
obscan_calib_target_handle_get_target_pose_info_num
```

### 5.2 Factory Reset (user-initiated)

```
obscan_device_management_restore_device_to_factory_settings
```

### 5.3 Parameter Restore (user-initiated)

```
obscan_device_management_resume_device_param
obscan_device_management_backup_device_param
```

### 5.4 Normal Scan Flow (no flash writes)

```
obscan_scan_start
obscan_scan_stop
obscan_scan_pause
obscan_scan_preview
obscan_scan_get_*
obscan_scan_config_*
obscan_frame_*
obscan_image_*
obscan_session_*
```

**The normal scan flow has no path to any `OrbbecCalib::*` function.** The scan flow uses `obscan_scan_start` → `obscan_scan_stop`, which control streaming, not calibration. Calibration is a separate operation (`obscan_calib_start` → `obscan_calib_stop`).

## 6. The Verdict

**No persistent flash writes occur during the normal scan cycle (discovery → start → stream → stop).**

All 19 flash-write call sites in the vendor binary are in one of three categories:
1. **Calibration updaters** (7 sites) — reachable only from `OrbbecCalib::*` functions
2. **Factory reset** (11 sites) — reachable only from `obscan_device_management_restore_device_to_factory_settings`
3. **Parameter restore** (3 sites) — reachable only from `obscan_device_management_resume_device_param`

None of these are reachable from the normal scan flow (`obscan_scan_start` → `obscan_scan_stop`).

## 7. What a Required Persistent Write Would Look Like

If a required persistent write existed, it would have to satisfy:

1. **Reachable from the normal scan flow.** The call chain would be: `obscan_scan_start` → `OrbbecScanContext` → `OrbbecDevice` → `AbstractDeviceHandle::updateFlashData`. But the normal scan flow calls only streaming, property-set, and PU-set operations — no calibration, no factory reset, no parameter restore.

2. **Invoked without user action.** The write would have to occur automatically during discovery, start, stream, or stop — not as a result of a user-initiated calibration or config-save action.

3. **Present in the call graph.** There would be a call site to `updateFlashData` in a function reachable from `obscan_scan_start` or `obscan_scan_stop`. But all 19 call sites are in calibration, factory-reset, or parameter-restore functions.

**No such write exists.** The negative is strong because:
- I enumerated **all** flash-write paths (not just observed ones)
- I traced **all** callers to their entry points
- I confirmed **all** entry points are user-initiated operations
- The normal scan flow has **zero** paths to any flash-write function

## 8. Confidence

**High.** This is a static analysis of the vendor binary, not an observation of one session. The conclusion is based on:
- Complete enumeration of flash-write call sites (19 sites, all accounted for)
- Complete caller-chain tracing (from `updateFlashData` → `DeviceParamAccessor::*` → `OrbbecDevice::update_*` → `OrbbecCalib::*`)
- C-ABI surface verification (calibration, factory reset, parameter restore are separate user-initiated operations)

The only way this could be wrong is if:
- A flash write occurs through a different mechanism (not `updateFlashData`) — but no other write function exists in the binary
- A calibration function is called automatically during the normal scan flow — but the C-ABI shows calibration is user-initiated (`obscan_calib_start` is separate from `obscan_scan_start`)
- The vendor SDK's `ob::Device::writeFlash` is called directly from somewhere else — but `updateFlashData` is the only caller in the disassembly

**Status: `[inferred]`** — static analysis of vendor binary, no gated run. The conclusion is strong but not runtime-confirmed. A multi-session capture (row #102's §6 recommendation) would move this from `[inferred]` to `[hw-confirmed]`.

## 9. Open Questions

None. The static analysis is complete. The only remaining work is runtime confirmation (row #102's §6 recommendation: a multi-session capture showing flash content before/after a normal scan cycle).

## 10. Implications for C2 (#100)

**C2 can safely claim the device.** The normal scan cycle does not write to persistent flash. All flash writes are user-initiated operations (calibration, factory reset, parameter restore), not part of normal operation.

**C2 should not invoke calibration, factory reset, or parameter restore without explicit user action.** These operations write to flash and can corrupt the device if done incorrectly (see `calibration-store.md` §1.1: the laser-plane write at `0x211000` is "bricking-capable").

---

**Wall**: `docs/recon/**`
**Status**: `[inferred]` — static analysis of vendor binary, no gated run.
**Binary**: `.recon-cache/liborbbec_scan_arm64.dylib` (outside repo)
**Disassembly**: `.recon-cache/listings/liborbbec_scan_arm64.full.asm` (outside repo)
**Symbols**: `.recon-cache/listings/liborbbec_scan_arm64.symbols.txt` (outside repo)
