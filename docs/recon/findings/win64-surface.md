# The Windows surface — CrealityScan 4.3.2 (row #57)

*Table read, plan57 pass. Light tooling only (`innoextract`, `objdump -p`,
`strings`, `xar`) — no decompiler, no analyzer. The artifacts were **copied**
into `.recon-cache/win432/`; the originals in `~/Documents/OpenScanner
Resources/` were not touched.*

## The gate answer: **yes — the Windows build ships the same surface, plus extras**

`CrealityScan_win_4.3.2.exe` (Inno Setup 6.3.0; the launcher is PE32) carries
`app/CrealityScan_Data/Plugins/x86_64/lib_orbbec_scan.dll` — a **PE32+ x86-64**
DLL (88,709,120 B) — with `OrbbecSDK.dll` (8.83 MiB) and `OrbbecGPL.dll`
(3.4 MiB) beside it. The payload is a Unity/IL2CPP app (`GameAssembly.dll`,
`il2cpp_data/global-metadata.dat`) exactly like the macOS bundle; the
installer's PE32-ness is only the launcher — **the payload DLLs are x64**.

`objdump -p` lists **251 `obscan_*` exports**. Against the pinned macOS
dylib's **215** (`.recon-cache/listings/liborbbec_scan_arm64.symbols.txt`):

| | count |
| --- | --- |
| macOS exports | 215 |
| Windows exports | 251 |
| common | **215** |
| **missing on Windows** | **0** |
| Windows-only | **36** |

The Windows-only 36 (absent from the macOS 4.3.2 build):
`obscan_calib_save_data`; `obscan_context_batch_create`,
`obscan_context_device_create`, `obscan_context_device_destory`,
`obscan_context_export_time`, `obscan_context_get_gpu_device_info_list`,
`obscan_context_select_active_gpu_device`; the `obscan_debug_*` family
(`get`/`set_debug_ic`, `get`/`set_debugviz`, `get`/`set_exchange_buffer`,
`get`/`set_is_use_mesh_points`, `get`/`set_merge_threads`,
`get`/`set_temp_markers`, `get_temp_markers_count`, `get`/`set_time_info`,
`get`/`set_uint64`, `global_get`/`set_max_fusion_frame`, `set_config_path`,
`set_net_ip_address`); `obscan_device_management_force_use_device_k3_params`,
`obscan_device_management_open_temperature_compensation`,
`obscan_device_management_read_ahb`, `read_i2c`, `write_ahb`, `write_i2c`;
`obscan_session_export_data_config`, `obscan_session_load_file_cache`.

**Every symbol our header declares or our findings list is present on
Windows.** The full lists are cached beside the binaries:
`.recon-cache/win432/{mac_exports_obscan.txt,win_exports_obscan.txt,win_only_exports.txt}`.

## Version parity

| | macOS 4.3.2 | Windows 4.3.2 |
| --- | --- | --- |
| scan library | `liborbbec_scan.dylib` (arm64) — **3.5.3.260921** | `lib_orbbec_scan.dll` (x86-64) — **3.5.3.260921** (`FileVersion`/`ProductVersion`; ProductName `OBScan`, FileDescription "OB Scan Binary") |
| SDK | `libOrbbecSDK.1.7.1.dylib` (+ `.1.7` and unversioned) | `OrbbecSDK.dll` (8.83 MiB) |
| extra | — | `OrbbecGPL.dll` (3.4 MiB) |

The scan libraries are the **same build** (3.5.3, build 260921) on both
platforms — the Windows figure read from the PE `VS_VERSION_INFO`, the macOS
one from the pinned dylib's own version string. The macOS payload names come
from the 4.3.2 pkg's Payload (gzip+cpio stream listing). Residual: the Windows
`OrbbecSDK.dll` carries no `VS_VERSION_INFO` readable with light tooling — its
version is unconfirmed (the macOS side ships `1.7.1`).

## ABI caveats — do not copy the macOS layouts across

The Windows DLL is MS x64: the C calling convention is RCX/RDX/R8/R9 + stack
(vs x0–x7 on arm64), aggregates larger than 8 bytes are passed by hidden
pointer, `long` is 32-bit (vs 64-bit on LP64), and the toolchains differ. So
every *struct* whose layout we recovered from the macOS slice needs a Windows
confirmation before it is trusted.

What the header already gives us: every struct we declared carries
`_Static_assert` size/offset guards (row #36), so **compiling the header for a
Win64 target verifies or falsifies every recovered layout mechanically** — the
cheapest Windows check available. The declarations that are pointer /
`int32_t` / `int64_t` / `bool`-based port without change.

Per the planner's rule: the header stays **single-sourced in the parent**;
Windows differences become `#ifdef`-guarded declarations with this document
beside the macOS findings.

## What the next Windows recon row would be

1. **Compile the header for a Win64 target** (or run its `_Static_assert`s
   under a Windows cross-compiler): verifies every struct size/offset derived
   from the macOS slice in one shot.
2. **Enum parity**: read the Windows DLL's `*_name` tables (the §7 / §11
   method) to confirm the device/tips/control/calib message enums and the
   status enum.
3. **The 36 Windows-only exports**: signatures + semantics — the
   `obscan_debug_*` family first (an instrumentation surface: temp markers,
   fusion-frame caps, net IP, time info), then
   `obscan_context_device_create`/`batch_create` and
   `obscan_device_management_read/write_i2c/ahb`.
4. **Any Windows struct layout the asserts don't pin** (e.g. offsets the
   guards don't cover).

## Evidence (paths)

- Artifact copies: `.recon-cache/win432/CrealityScan_win_4.3.2.exe`,
  `.recon-cache/win432/CrealityScan_mac_4.3.2.pkg`.
- Extracted DLLs:
  `.recon-cache/win432/extracted/app/CrealityScan_Data/Plugins/x86_64/{lib_orbbec_scan.dll,OrbbecSDK.dll,OrbbecGPL.dll}`.
- Export lists: `.recon-cache/win432/{mac_exports_obscan.txt,win_exports_obscan.txt,win_only_exports.txt}`.
- macOS list source: `.recon-cache/listings/liborbbec_scan_arm64.symbols.txt`
  (the pinned arm64 slice, sha `184d92a8…c017ec`).
- macOS pkg payload: `.recon-cache/win432/macpkg/CrealityScan.pkg/Payload`
  (gzip + cpio; the file list was streamed, not expanded).
