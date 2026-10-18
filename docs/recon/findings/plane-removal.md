# Plane removal — the G1 adoption spec (row #59)

*Static half, plan59 pass. Light reads only (`objdump`-class; no analyzer). The
frozen corpus (`docs/research/plane-removal-reverse-engineering.md`) was used
as a **pointer only** — every fact below was re-derived from the pinned
snapshot and is cited by address.*

## 1. The ABI — the three toggles and their homes

Four exports exist (all four reject a NULL scan with `0x32620107`):

| export | C signature | vtable slot | native chain |
| --- | --- | --- | --- |
| `obscan_scan_plane_remove` (`0xf4b8`) | `(scan, bool)` | `+0x50` | `OrbbecScanSpeckle/Laser::frame_remove_plane(bool)` `0x5c5a4`/`0xc3afc` → `add x0, x0, #0xf0` → **`OBScanDataManagement::update_realtime_auto_rm_plane(bool)`** `0x7016b8`; returns `0` |
| `obscan_scan_turntable_plane_remove` (`0xf560`) | `(scan, bool)` | `+0x60` | `frame_remove_turntable_plane(bool)` `0x5ca30`/`0xc3f88` → `update_realtime_rm_turntable_plane(bool)` `0x70169c` |
| `obscan_scan_config_manaul_remove_plane` (`0xf8cc`) | `(scan, const ObScanManualRPConfig_*)` | `+0x58` | `config_manaul_remove_plane(bool, OBScanPlane_&, int&)` `0x5c5c0`/`0xc3b18` (row #58) |
| `obscan_scan_get_manaul_remove_plane` (`0xf830`) | `(scan, const config*, ObScanPlane_* out)` — the config passed **by value** (a `0x18`-byte copy); its observed shape differs from the setter's (see §4) | direct | `OBScanBase::get_manaul_remove_plane(config, plane*)` `0x6fbe94` |

**The re-homing question — same object, two layers.** The scan-class vtable
methods are thin forwarders to the data-management subobject: `frame_remove_plane`
is literally `add x0, x0, #0xf0; bl OBScanDataManagement::update_realtime_auto_rm_plane(bool);
mov w0, #0` (`0x5c5ac`–`0x5c5b4`). So the corpus's "`OBScanDataManagement`" and
findings §1's "the data-management subobject at `scan+0xf0`" are **the same
object** — the corpus named the state's home, §1 named where that object lives
inside the scan. The live state:
`update_realtime_manual_rm_plane(bool, OBScanPlane_ const&)` (`0x7016d4`,
verified) writes, under the `+0x320` lock, the enabled byte at
`dataMgmt+0x4f` and the 16-byte plane at `dataMgmt+0x50`; `get_realtime_config`
(`0x7016f8`) reads `+0x30`/`+0x50` back under the same lock. **The toggles
carry no step gate** — the wrappers check only the NULL scan, so they are
callable live, at any `OrbbecScanStep`.

## 2. The decision function — `judge_rm_plane_is_need_cut`

`laser::OBscanLaserCommon::judge_rm_plane_is_need_cut(rs_common::math::v3d const& p,
OBScanPlane_ const& plane, double const& threshold) -> bool` (`0x98f70`; `1` =
cut, `0` = keep). Semantics, read instruction by instruction:

1. `d = (a·x + b·y + c·z + d_plane) / ‖(a,b,c)‖` — the **normalized signed
   distance** of the point from the plane (`fcvt` the float plane to double;
   `fsqrt` of the normal's squared length);
2. `if (d >= threshold) → 0` — the near side is kept;
3. otherwise a **fallback**: `z_at_plane = (…)/c` (an intersection computed by
   dividing by the plane's `c`, with `-d_plane`), and `if (p.z > z_at_plane) →
   1 else → 0`.

So the corpus's "hybrid per-point cut" is confirmed: a threshold test on one
plane plus a `c`-division fallback. It is **not** a RANSAC/segmentation — one
plane, one threshold, and the threshold is a parameter the *caller* supplies
internally. Its only static caller is a `std::bind` functor in the
laser-markerless code (`0xac420`, inside `laser::OBScanLaserMarkerless…`), so
the judge belongs to the **markerless/turntable** path — the live toggle path
does not go through it (see §3).

**What a correct cut decision would need** (for the quality the operator wants):
more than one plane (a candidate set from a RANSAC/region fit, then per-point
assignment), a per-point margin rather than a single threshold, and a
**per-frame budget** (cut the points that buy the most resource relief first).
None of that is reachable through the vendor's ABI: the threshold is not a
parameter of any export, and the plane list is one plane.

## 3. The verdict — bind the toggles; reimplement only the decision

**Bind.** The three toggles are live-callable, gate-free, and drive the
vendor's own per-frame removal:
`OrbbecScanSpeckle/Laser::remove_plane_each_frame_data(ProgressReporter const&)`
(`0x67abc`/`0xd2880`; 824 instructions in the Speckle one) is the per-frame
executor, and it works from the **persisted** manual-plane info — it calls
`ProjectFileCache::load_serializable` / `is_resource_ready` with
`SerializableRemovePlaneManualInfo` and re-assigns a
`vector<RemovePlaneManualInfo>` (the same records `save_manual_rm_plane_datas`
`0x66ae0`/`0xccc8c` writes and `OBScanSerialIO::load_remove_plane_manual_info`
`0x6eb194` reads). So for **per-frame resource management** — G1's goal — the
vendor path is usable as-is: enable `plane_remove` (and the manual plane via
`config_manaul_remove_plane`) and the SDK's own pipeline does the per-frame
work.

**Risks of binding:** (a) the *quality* is the vendor's — the operator's "it
does not work at all" is a quality complaint, and binding changes nothing
about it; (b) the manual-plane semantics are opaque: the setter takes a
`{int, bool, plane}` config but the *getter* reads a `{pointer, count}`
point-list shape at the same offsets (§4), so what the plane *means* to the
removal is not fully pinned; (c) the removal's effect is only observable
through the cloud getters.

**Reimplement (our own RANSAC/signed-distance) is the right call for the
*decision*, not for the plumbing** — if and when the goal is removal *quality*
(a later row): fit candidate planes from the live cloud (the color/raw getters
give the data), decide per point with a margin, and act on the cloud we
publish. That is our code end-to-end; nothing in the vendor's ABI exposes a
hook to substitute its decision.

## 4. `OBScanPlane_` and the config — row #58's residue, resolved

- **`OBScanPlane_` = 4 × `float32` = 16 bytes** — the plane equation
  `{a@0x00, b@0x04, c@0x08, d@0x0c}`. Evidence: `judge_rm_plane_is_need_cut`
  reads exactly `s0,s1 = [x1]`, `s4,s6 = [x1+8]` and computes `a·x+b·y+c·z+d`
  (`0x98f70`–`0x98f9c`); `update_realtime_manual_rm_plane` stores exactly one
  `q0` (16 B) at `dataMgmt+0x50`.
- **`OBScanManualRPConfig_` = 0x18 bytes**, the setter's shape
  `{int32_t @0x00; bool enabled @0x04; pad; OBScanPlane_ plane @0x08}` (row
  #58; the wrapper reads `+0x4` as the bool and passes `&+0x8` as the plane).
- **Discrepancy to carry:** the *getter*'s config copy is the same `0x18`
  bytes but read as `{…; pointer @0x08; uint32 count @0x10}` — an array of
  16-byte `{3×f32, pad}` points scaled ×1000.0 (`0x6fbee0`–`0x6fbf1c`). So the
  getter's argument is a *point list* (the manual plane's defining points)
  while the setter's is a *plane + flags*: same size, different shapes. Until
  that is reconciled, the getter stays undeclared (a residual).
- **G1 needs the plane layout** (to construct one for the setter) — it is now
  recovered; the header declarations for the three toggles + the two structs
  are added alongside this spec.

## Evidence

- All addresses are from `.recon-cache/liborbbec_scan_arm64.dylib` (pinned arm64
  slice, sha `184d92a8…c017ec`); listing
  `.recon-cache/listings/liborbbec_scan_arm64.full.asm`.
- Symbols of record: `_obscan_scan_plane_remove` (`0xf4b8`),
  `_obscan_scan_turntable_plane_remove` (`0xf560`),
  `_obscan_scan_config_manaul_remove_plane` (`0xf8cc`),
  `_obscan_scan_get_manaul_remove_plane` (`0xf830`),
  `judge_rm_plane_is_need_cut` (`0x98f70`),
  `remove_plane_each_frame_data` (`0x67abc`/`0xd2880`),
  `update_realtime_{auto,manual,rm_turntable}_rm_plane` (`0x7016b8`/`0x7016d4`/`0x70169c`).
- The frozen corpus was consulted for *names only*; every claim above is
  re-derived (per the order's instruction).
