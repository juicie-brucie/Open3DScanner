# Scan viewer architecture — active-scan rendering and scanner POV

OpenScanner's UI is a proof of concept: `ui/app.js`, a dependency-free WebGL2 `gl.POINTS`
viewer that renders whatever `tools/scan_server` broadcasts. It has no camera interaction
and no pose. This doc lays out the architecture for a robust interface that renders the
active scan, follows the scanner's point of view, and maps cleanly onto Windows, Linux and
macOS. Mobile is out of scope for now; a streamed viewfinder is noted where it falls out for
free.

**Read this against the board.** The *budget* half of the transport is already landed: a
fixed 20,000-point **stride** sample per tick (`#32`) and a capped final-mesh frame
(`#38`). The protocol rules in §4 — **voxel** decimation, delta encoding, quantization,
backpressure — are a **forward design**, not a description of what ships today; only
backpressure has a row (`#41`).

## 1. Shape: three tiers, one protocol

```
┌─ capture core ──────────┐         ┌─ viewer clients ───────────┐
│  device seam            │         │  desktop (web, WebGPU)     │
│  SDK binding            │◄───────►│  mobile viewfinder (later) │
│  fusion · decimation    │  frame  │  replay / test harness     │
└─────────────────────────┘ protocol└────────────────────────────┘
```

**The protocol is the durable asset; clients are replaceable.** Everything below it is
capture; everything above it is presentation. Designing the protocol first is what buys
OS-agnosticism and the mobile path at once.

## 2. Client platform — web, served locally

**Decision: a web client, served by the local capture process, extended from `ui/`.**

- HTML/CSS/WebGPU is the most OS-agnostic UI stack available; one codebase for macOS,
  Windows and Linux.
- It is the only shape that also serves a phone browser later — the stated mobile goal.
- A thin native shell (Tauri, MIT/Apache-2.0) can wrap the same client later with **no
  protocol change**, if a "real app" is wanted.

Rejected: Qt (LGPL — conflicts with a static MIT build), GTK (per-OS packaging), a bespoke
C++ GUI. Open3D's GUI (Dear ImGui + Filament, MIT/Apache) stays a fallback if in-process
rendering is ever needed.

## 3. Render pipeline — WebGPU, WebGL2 fallback

**Decision: WebGPU, keeping the current WebGL2 path as the fallback.**

- WebGPU maps to Metal (macOS), D3D12 (Windows) and Vulkan (Linux) — one API, three OSes.
- Compute shaders give GPU-side decimation, normal estimation and splatting later.
- Point clouds are a single draw call; the engineering is in LOD and camera, not shading,
  so no rendering framework is required. `three.js` (MIT) is optional if a scene graph
  becomes useful.
- Avoid OpenGL: deprecated on macOS.

## 4. The frame protocol

Today's wire format carries a cloud as

```
frameId(u32) | captureTs(f64) | count(u32) | xyz f32[count*3]
```

— positions only, no version, no message type, no pose. What the server sends has already
been bounded (`#32`'s fixed stride sample); what is missing is the *shape*: versioning,
message types, pose, delta, backpressure.

**Decision: a versioned, typed, binary protocol.**

Envelope: `magic | version | type | seq | t_capture | t_server | payload_len`

| type | payload |
| --- | --- |
| `status` | scan state, mode, FOV, exposure (today's JSON, kept for compatibility) |
| `pose` | scanner pose (translation + orientation) + frame id |
| `cloud_snapshot` | a complete cloud, positions (+ optional normals/colors) |
| `cloud_delta` | points added since `seq` (a scan is append-mostly) |
| `markers` | detected marker positions (diagnostics / POV overlay) |
| `mesh` | the post-pause reconstructed mesh |
| `error` | typed failure |

**Forward design — the rules below are the target, not the current state.**

- **server-side point budget** — voxel-decimate to N points before sending. A coarser
  **stride** sample already ships (`#32`); the voxel rule is the upgrade, and the **server**
  owns the budget, not the client.
- **delta encoding** where the cloud only grows.
- **quantization** (fixed-point relative to a bounding box) to cut bytes 2–4×.
- **backpressure** — the client advertises a budget; the server drops or decimates rather
  than queueing. Row `#41` owns this half.

## 5. Following the scanner's point of view

Two pieces are missing today:

- **A pose channel.** The contract binds neither `obscan_scan_get_global_pose` nor
  `obscan_scan_get_markers`; the research corpus records the real app calling both. Board row
  `#40` specs them, then the protocol carries a `pose` message.
- **A camera model.** `ObScanFov` (Normal/Wide) is already a two-position status value and
  serves as the intrinsic FOV; the pose is the extrinsic.

Camera modes: `follow` (viewfinder — the camera sits at the scanner pose), `orbit` (free
inspection), and optionally a scanner-frustum overlay drawn in orbit mode.

This is also the mobile path: the viewfinder is `pose` + `cloud_delta` at a lower rate.

## 6. The device seam

```cpp
class CaptureSource {                       // agnostic
public:
    virtual Status status() = 0;
    virtual void start(); virtual void pause(); virtual void stop();
    virtual void setExposure(const Exposure&); virtual void setFov(ObScanFov);
    virtual void subscribe(FrameSink&);     // cloud + pose + markers
};
```

Implementations:

- `ObscanCaptureSource` — today's SDK binding.
- `OrbbecSdkCaptureSource` — the open-source, cross-platform path (the README's "drop
  OBScan" direction).
- `ReplayCaptureSource` — a recorded session replayed through the same protocol.

**`ReplayCaptureSource` is the highest-leverage piece here**: it lets the viewer be built
and regression-tested without the scanner and without the serialized hardware room, which
is the loop's scarcest resource.

## 7. Portability, honestly

| Layer | macOS | Windows | Linux |
| --- | --- | --- | --- |
| UI, render, protocol | ✓ | ✓ | ✓ |
| Fusion / mesh (Open3D, Manifold, libigl) | ✓ | ✓ | ✓ |
| Device layer (OBScan binding) | ✓ | vendor SDK required | likely unsupported |
| Device layer (Orbbec SDK directly) | ✓ | ✓ | ✓ |

So: agnostic **above the seam** today; agnostic **at the seam** only when the capture
source moves off OBScan. The seam exists precisely so that move does not touch the UI.

## 8. Licensing

MIT project — every addition must be permissive.

| Component | License | OK to ship |
| --- | --- | --- |
| WebGPU | W3C standard | yes |
| WebGL2 | W3C standard | yes |
| Tauri (optional native shell) | MIT / Apache-2.0 | yes |
| three.js (optional) | MIT | yes |
| Open3D (fusion; optional GUI) | MIT | yes |
| Qt | LGPL / commercial | **no** (static-link conflict) |

Existing decisions hold: Manifold (Apache-2.0) and libigl core (MPL-2.0, never
`igl/copyleft/*`) for mesh editing — see [`mesh-editing-stack.md`](mesh-editing-stack.md).

## 9. Proposed module layout

```
engine/include/openscanner/
  capture_source.h        # the seam
  frame_protocol.h        # envelope + message types (shared by both ends)
engine/src/
  obscan_capture_source.cpp
  replay_capture_source.cpp
tools/scan_server/        # one host of the capture core
ui/
  protocol/               # codec for frame_protocol
  render/                 # WebGPU renderer + WebGL2 fallback
  camera/                 # follow / orbit modes
  controls/               # scan + exposure + FOV
```

## 10. First increment

1. `frame_protocol.h` + a codec on both ends; today's wire shape becomes v1. **(`V1` —
   unfiled; the objective.)**
2. `ReplayCaptureSource` + one recorded session.
3. `pose` message + the `follow` camera mode (blocked on `#40`).
4. ~~Server-side decimation to a point budget~~ — the *stride* budget landed as `#32`; the
   protocol's voxel/quantization rule and `#41`'s backpressure are the remainder.
5. Client: WebGPU renderer + orbit controls, WebGL2 fallback.

## Open questions

- Does `obscan_scan_get_global_pose` return the scanner pose or the model pose, and in
  which coordinate frame? (row `#40`)
- Is the live cloud already in world frame or scanner frame? This decides the follow-POV
  transform.
- Are live normals needed, or positions only? (payload size)
- Mobile: the web client on the phone, or server-side render + video? The web client is far
  cheaper and reuses everything.

---
