Open3DScanner — feasibility of fully replacing Creality's proprietary stack
Date: 2026-10-18
Scope: a feasibility assessment and a path, for replacing every closed component
between the application and the CR-Scan Raptor Pro: the OBScan scanning library,
Orbbec's device library, Creality's root RPC daemon, and the scanner's firmware.
Evidence base: this session's work — the public-SDK device probe, two localhost
protocol captures of a full CrealityScan session, the public OrbbecSDK v2 source,
and recon's calibration-store findings (row #94 / OQ-S15). Nothing here was read
from a Creality binary; it is wire observation plus MIT-licensed public source.

A note on the firmware line item: the user has extracted the latest Raptor
firmware. This report treats firmware replacement as a stated goal and assesses
it, but see section 7 — it is a different kind of undertaking from the host-side
work, with its own risk profile, and it is NOT required for host-side
independence.

=============================================================================
0. BOTTOM LINE
=============================================================================

Replacing the HOST-SIDE proprietary stack (OBScan + Orbbec lib + Creality
daemon) is FEASIBLE and well-bounded. The protocol is now fully observed, the
device is a "dumb" sensor whose intelligence lives on the host, and calibration
is readable off the device over a transport we have already watched on the wire.
The hard parts are known and finite: a privileged USB helper on macOS, two
host-side control loops (laser sequencing + auto-exposure), and the on-device
reconstruction math (laser-line extraction, triangulation, fusion).

Replacing the FIRMWARE is a SEPARATE, much larger, and riskier goal that buys
little for a 3D-scanning product and carries a real bricking risk. Recommended
only as a research track, explicitly after host independence is achieved, and
gated on a recovery path. It is not on the critical path to an open scanner.

Tiered verdict:

  Tier                                   Feasibility     Effort     On critical path?
  -----------------------------------    ------------    -------    -----------------
  Replace OBScan (scanning algorithms)   HIGH            medium     yes
  Replace the Orbbec device library      HIGH            low-med    yes
  Replace the Creality RPC daemon        MEDIUM          medium     yes (for true independence)
  Replace the firmware                   LOW-MEDIUM      high       no — research track

=============================================================================
1. WHAT THE PROPRIETARY STACK IS (four layers)
=============================================================================

From top to bottom, with how each was characterised this session:

  L4  OBScan (liborbbec_scan, "OBScan") — the closed scanning library.
      Laser-line extraction, stereo triangulation with thermal-compensated
      intrinsics, marker detection + tracking, registration quality gate,
      bundle adjustment, voxel fusion, meshing, plane removal, SuperPoint
      texture. This is the bulk of what Open3DScanner currently binds.

  L3  Creality RPCServer — a root daemon (/Library/Creality/RPCServer, launched
      via sudo from a LaunchAgent). Holds the USB device exclusively and
      exposes it on localhost as msgpack-RPC, one TCP port per interface
      (10010/10013 = IR, 10011 = IMU, 10012 = RGB) plus GigE-Vision discovery
      on UDP 3956. CONFIRMED this session: it owns the device (ioreg
      UsbExclusiveOwner = RPCServer), and OBScan never touches USB itself.

  L2  Orbbec device library (libOrbbecSDK 1.7.1, bundled v1 binary) — the
      device-I/O layer OBScan sits on. Note the public OrbbecSDK v2 is a
      DIFFERENT codebase (MIT, open) that does NOT list this device (confirmed:
      PID 0x0c03 absent from every family's PID table; a live enumeration probe
      saw 0 devices).

  L1  Firmware on the scanner — runs the sensors, the laser driver, the flash
      store (calibration), and the USB/UVC + vendor-command interface. The
      device is otherwise passive.

=============================================================================
2. THE KEY STRUCTURAL FINDING: THE DEVICE IS DUMB
=============================================================================

The two protocol captures (a full CrealityScan session, launch to quit) show
that essentially all scanning intelligence runs on the HOST, not the device:

  - Laser pattern sequencing is host-driven, ~10 Hz. Laser on/off and the
    laser-group properties are SET ~890 times each across the session, and they
    STOP exactly during the user's pauses. The device does not self-sequence.
  - Auto-exposure is a host software loop. IR exposure + laser current are
    ramped in steps several times a second; setting "auto" in the UI just
    restarts that host loop. No firmware AE.
  - Cross vs parallel laser mode is one host property; FOV produced no device
    command at all (host-side or set at scan creation).
  - Depth is computed on the host. The device exposes raw left/right IR
    (1920x1200, 8-bit, 50fps), colour, and IMU — no on-device depth sensor
    ("No required type sensor found: OB_SENSOR_DEPTH" in the vendor log).

Consequence: the device's firmware is doing comparatively little that is
scanning-specific — stream the cameras, drive the laser on command, serve flash.
That is good news for host-side replacement (most of the value is reimplementable
on the host from open math) and it is WHY firmware replacement buys so little
(section 7): the firmware is not where the scanning smarts are.

=============================================================================
3. THE PROTOCOL IS FULLY OBSERVED (this is the enabling finding)
=============================================================================

The transport between app and daemon is now understood end to end:

  - Discovery: standard GigE-Vision (GVCP) on UDP 3956. The daemon answers
    with model/serial/RPC-port in the standard reply.
  - Transport: msgpack-RPC over TCP, rpclib-style [0,msgid,method,params] /
    [1,msgid,nil,result]. ~14 methods total. Pull-based (one call per frame).
  - Methods: get_frame (IR), get_imu_frame, get_device_report_infomation,
    start/stop_stream, start/stop_imu_stream, get/set_pu, get_pu_range,
    get_stream_profile_list, set_ir_frame_light_type, send_data (the vendor
    tunnel), get_device_enumeration_info.
  - Inside send_data: Orbbec's OWN standard vendor-command framing — magic
    0x47/'MG', 8-byte header (size/opcode/requestId), opcodes GET/SET_PROPERTY,
    GET/SET_STRUCTURE_DATA, the v1.1 list ops, and memory read (opcode 25).
    This framing is defined in the PUBLIC SDK's MIT-licensed Protocol.hpp, so
    it is documentable without any closed binary.

None of the RPC layer (ob_rpc_*, msgpack) exists in the public SDK — it is
Creality/Orbbec-daemon-specific — but it is simple and now fully mapped.

=============================================================================
4. CALIBRATION IS READABLE OFF THE DEVICE (the gate that was open)
=============================================================================

Dropping OBScan under ANY path was gated on calibration (roadmap G7 / risk 2).
This session closed most of that gate:

  - The host reads calibration from DEVICE FLASH at startup via vendor opcode
    25 (memory read), tunnelled through send_data — 120 reads of 768 B,
    assembling several records. Observed directly on the wire; no libOrbbecSDK
    needed for the transport.
  - Records carry a 16-byte header (magic 0x464473xx / checksum / length /
    type==own-address). The laser-plane record (magic low-byte 0x14) sits at
    0x211000 with a second copy at 0x213000 — matching recon's static findings.
  - Intrinsics are independently readable from the host session SQLite cache
    (recon: resources.obscan, camera_intrin.bat, a 41-field big-endian record)
    — already SDK-free.

So: intrinsics are ours today; the laser planes and thermal data are now
reachable over an observed transport, with payload decoding the remaining work.

=============================================================================
5. FEASIBILITY, LAYER BY LAYER
=============================================================================

L4 OBScan — HIGH feasibility, medium effort.
  The algorithms are textbook and have permissive-licensed equivalents:
    - laser-line extraction: Steger / centre-of-gravity sub-pixel (own code,
      GPU via Metal for 1920x1200 @ ~40Hz)
    - triangulation: closed-form stereo (already understood from marker-detection
      recon earlier in the project)
    - tracking: marker detect + bundle adjust (Ceres, BSD)
    - fusion/denoise/mesh: Open3D (MIT) + the project's existing mesh/ (libigl,
      manifold)
  This can proceed INCREMENTALLY on top of the vendor's own output (replace one
  stage at a time), so the product stays usable throughout. Lowest risk of the
  four.

L3 Creality daemon — MEDIUM feasibility, medium effort.
  Two sub-steps:
    C1 (our own RPC client to the daemon): LOW effort — the protocol is mapped;
       a small msgpack-RPC client reproduces the whole session. Removes OBScan
       and libOrbbecSDK but still needs the daemon running.
    C2 (replace the daemon with our own USB layer): MEDIUM. The daemon is a thin
       USB proxy — its methods map 1:1 onto UVC streaming, UVC controls, the
       UVC extension-unit vendor channel, and HID (IMU). The public SDK's
       Protocol.cpp is an MIT reference for the vendor framing.
       The real obstacle is macOS: claiming UVC interfaces away from the system
       driver generally needs root — almost certainly why Creality ships a
       root daemon. C2 on macOS likely means shipping our OWN small privileged
       helper (or a system extension). On Linux this is just a udev rule.

L2 Orbbec device library — HIGH feasibility, folded into C2.
  We do not "port" libOrbbecSDK; C2 replaces its role. The public v2 SDK is an
  MIT reference for UVC/HID/vendor-command mechanics even though it does not
  support this PID.

L1 Firmware — see section 7.

=============================================================================
6. THE PATH (host-side independence)
=============================================================================

Phase A — Spec the protocol (recon, from the captures). No new capture needed
  for the backbone. Produce docs/recon/findings specs: the RPC method set, the
  vendor-command framing, the property map, the flash-read transport. These are
  observation-derived, clean-room-safe, and become what the builder implements
  from.

Phase B — C1: own RPC client to the daemon. Build a msgpack-RPC + GVCP client;
  reproduce a scan (open streams, pull IR, drive laser + exposure loops, stop).
  At the end of B, OBScan and libOrbbecSDK are gone; only the daemon remains.

Phase C — Own the scanning math on top of C1's raw frames (the L4 work),
  incrementally: laser-line extraction -> triangulation -> fusion -> tracking.
  Each stage A/B-tested against the vendor's own output while the vendor path
  still exists as reference.

Phase D — C2: replace the daemon. Implement UVC streaming + HID + the vendor
  channel directly, behind a privileged helper on macOS. At the end of D, no
  Creality or Orbbec code runs on the host at all. Cross-platform becomes
  possible (Linux needs only the udev rule).

Phase E (optional) — firmware (section 7), only after D and only as research.

Calibration threads through A-C: decode the flash records (planes, thermal) so
the owned pipeline has real parameters, not the vendor's.

=============================================================================
7. FIRMWARE REPLACEMENT — SEPARATE ASSESSMENT
=============================================================================

Feasibility: LOW-MEDIUM. Value: LOW for a scanner. Risk: HIGH (bricking).
Recommendation: research track only, after host independence, gated on recovery.

Why the value is low:
  Section 2 showed the device is dumb — the firmware streams cameras, drives the
  laser on command, and serves flash. Replacing it does NOT unlock better scans;
  the scanning quality lives in the host pipeline we are already replacing. The
  main things custom firmware could buy are: removing the need for the host
  laser/exposure loops (move them on-device), changing the USB protocol to
  something simpler, or unlocking hardware the vendor gates. None are required
  for an open scanner.

Why the risk is high:
  - A failed firmware write can brick the device with no guaranteed recovery.
    Unlike every host-side step, this is not reversible by "kill the process".
  - The RPCServer/flash write path we have seen includes a flash-write transport
    (updateFlashData in recon's notes); misusing it against the wrong region is
    how calibration or boot data gets destroyed.

What the extracted firmware is good for RIGHT NOW (without reflashing):
  - Confirming the SoC/architecture and the camera/laser driver details, which
    informs the host-side laser and exposure loops.
  - Cross-checking the vendor-command opcode semantics (what each property
    actually does on the device) — useful to the host work in sections 3-4.
  - Understanding the flash layout and the calibration record formats from the
    other side, complementing the wire captures.
  That is READ-only analysis of a file the user owns; it does not touch the
  device and carries no bricking risk. It belongs to the recon side of the
  clean-room wall (it is decompiled/extracted vendor code), NOT the builder side.

If firmware replacement is pursued later, the pre-gates are:
  - a confirmed recovery path (hardware flash programmer / bootloader recovery
    mode / vendor reflash) proven on THIS unit before any write;
  - full knowledge of the flash map (what must be preserved: calibration, serial,
    boot);
  - a second unit, so a brick is not the end of the project.

=============================================================================
8. OPEN QUESTIONS AND GATES
=============================================================================

Gates (must clear before the dependent phase):

  G-A  Protocol spec exists in findings (gates B).         — recon, from captures
  G-B  macOS UVC-claim mechanism decided (gates D/C2).     — needs a decision:
         own privileged helper vs system extension vs "keep a thin daemon".
  G-C  Calibration payloads decoded (gates a correct C).   — recon (see hand-back)
  G-D  Laser + exposure loop inputs fully mapped (gates B's
       control half).                                       — mostly done; needs
         single-action captures to disambiguate properties.

Open questions:

  OQ-F1  The calibration record payloads: the laser-plane struct layout and
         units, the thermal entries, the record checksum algorithm. (Recon owns
         this; see the separate hand-back. The checksum is not a standard CRC-32
         or plain XOR — recon's note points at the device's lookup table.)
  OQ-F2  Exact semantics of vendor properties 151-164, 183, 192, 235, 240, and
         what the RPC method suffixes (0-2-10/12/14/16) encode. Settle with
         short single-action captures (one pause/resume, one mode switch, one
         slider).
  OQ-F3  Whether any flash region must be WRITTEN during normal operation (vs
         read-only at startup). The captures show reads; confirm no required
         writes before C2 claims the device.
  OQ-F4  macOS privileged-access shape for C2 (G-B) — the single biggest
         unknown for true daemon-free operation.
  OQ-F5  The backup/resume parameter blob schema (recon OQ-S15d) — still
         dispatches into libOrbbecSDK; needs a runtime hexdump or the cached
         dylib, not the wire.
  OQ-F6  (Firmware track) SoC family, bootloader/recovery mode, and flash map —
         all from the extracted image, read-only, before any write is considered.

=============================================================================
9. CLEAN-ROOM POSTURE (carries the whole effort's defensibility)
=============================================================================

  - Everything host-side so far is observation-derived (wire captures + MIT
    public-SDK headers). That is the cleanest possible provenance and should
    stay the method: recon writes specs from observation; the builder implements
    from specs, never from captures or vendor binaries directly.
  - The extracted firmware and any libOrbbecSDK/OBScan analysis are
    DECOMPILED/EXTRACTED VENDOR CODE: recon-side only. They inform specs; they
    never reach builder or the committed implementation.
  - Protocol facts belong in docs/recon/findings, tagged [inferred] until a run
    confirms, same as every other recon target.

=============================================================================
10. RECOMMENDATION
=============================================================================

  1. Do the host-side replacement (Phases A-D). It is feasible, bounded, and
     each phase leaves a working product. Order: spec -> C1 client -> own the
     math -> replace the daemon.
  2. Decide G-B (macOS privileged access) early — it is the one unknown that
     can reshape C2.
  3. Use the extracted firmware now ONLY as read-only recon input (opcode
     semantics, flash map, laser/exposure details). Do not plan a reflash.
  4. Treat firmware REPLACEMENT as a later research track, off the critical
     path, gated on a proven recovery path and ideally a second unit.
