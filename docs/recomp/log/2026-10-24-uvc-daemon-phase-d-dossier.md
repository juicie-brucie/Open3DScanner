> **DRAFT — appendable, not a finalized log entry.** Unlike the rest of
> `docs/recomp/log/` (one file per entry, never edited after landing), this
> file is being built incrementally across one continuous session and will
> keep growing until the two Phase D targets below are settled. Dispatch
> should treat it as the dossier for splitting this branch's work into the
> PR structure in §6, not as a record to cite verbatim in `PLAN.md` — pull
> the specific commit SHAs and test records it points at instead.
>
> Scope: Phase D (replacing the vendor's `RPCServer` daemon with our own,
> starting with `tools/raw_capture` as a UVC/HID-over-IOUSBHost capture
> path). Rows: #171 (raw capture path), #178 (descriptor probe + live UVC/
> HID source). Both currently `HW`.
>
> Two things this session is going until it accomplishes, before treating
> this dossier as ready to hand off:
> 1. Resolve the interface-ownership blocker (§2) — get `raw_capture
>    --capture` to actually produce frames on at least one stream.
> 2. Depth-first validation — once (1) holds, capture a depth frame via
>    UVC and compare it byte-for-byte against an RPC depth frame from the
>    vendor daemon, to confirm the UVC path is a legitimate stand-in.

# Phase D dossier — UVC capture path (`tools/raw_capture`), 2026-10-24

## 1. What's actually landed (chronological, all on `hw/next`)

All of these are **real commits on `hw/next`, merged into `xcode/user-test`
as each one landed** — none have gone through a PR. See §6 for the intended
split.

| Commit | What | Why |
|---|---|---|
| `42027bb` | Skip redundant `selectAlternateSetting:`; attempt `clearStallWithError:`; fix a format/frame-index pairing bug in the descriptor probe | Device refuses `SET_INTERFACE` to an already-active setting; endpoint may be left halted after `launchctl bootout` of the vendor daemon |
| `4ffc688` | Land the 2026-10-21 hw-test evidence that had been sitting uncommitted in the worktree | No code change — preserving prior work |
| `8cafd4d` | Correct `plan-uvc-probe-indexfix` to the passing re-probe | Same filename, corrected content (first attempt blocked on a stale-binary preflight) |
| `3a9dcf0` | Implement the UVC `VS_PROBE_CONTROL`/`VS_COMMIT_CONTROL` negotiation (`VideoProbeCommitControl` struct, `NegotiateUvcFormat`) | UVC spec requires this handshake before a device will stream; this device's firmware enforces it ("Unable to send IO" on every read attempt without it) |
| `d52f6a0` | Re-fetch the bulk pipe after a successful commit (`RefreshPipe`) | Hypothesis: `SET_CUR(VS_COMMIT_CONTROL)` invalidates the pipe the same way `selectAlternateSetting:` is documented to — **not confirmed**, overtaken by §2's finding before it could be cleanly retested |
| `a74c9cb` | Retry the interface lookup in `Open()`, bounded 10×50ms | First "interface N is not on the device" sighting; retry theory — **refuted** by the next commit's retest |
| `64170a3` | Replace the `IORegistryEntryGetChildIterator` child-walk with `IOUSBHostInterface`'s own `createMatchingDictionaryWithVendorID:...interfaceNumber:` + `IOServiceGetMatchingService` | `--probe` never walks child services at all (reads the config descriptor directly from the device object); theorized the child-walk itself was the wrong mechanism — **also refuted** |
| `b4420bc` | Switch the top-level device claim to `IOUSBHostObjectInitOptionsDeviceCapture`, run as root | `ioreg -p IOUSB` showed zero published children; theorized device-level capture would make interface nodes appear — **refuted even as root** |
| `4fc7b6c` | **Root cause found.** Revert device-level capture; claim the *interface* with `IOUSBHostObjectInitOptionsDeviceSeize` instead | `ioreg -p IOService` (not `-p IOUSB`) showed every video/HID interface node with `"UsbExclusiveOwner" = "pid 588, UVCAssistant"` (or `AppleUserUSBHostHIDDevice` for the IMU) — see §2 |
| `c1321db` | Merge the interface *find* and the *seize* into one combined bounded retry (15×100ms) | A retest of `4fc7b6c` failed with no retry count in the error text — the find itself failed before the seize code ever ran; the earlier retry removal (in `64170a3`) was premature |
| `443fcb6` | Replace `IOUSBHostInterface`'s own `createMatchingDictionaryWithVendorID:...interfaceNumber:...` convenience method with a hand-built `IOServiceMatching` + `CFDictionarySetValue` dictionary, the identical shape `FindScannerService()` already uses successfully for the device | A retest of `c1321db` (depth **and** IR together) found the find step still failing 15/15 for *both* streams, identically — even though `ioreg -p IOService` independently proved both nodes exist, fully matched, with exactly the `idVendor`/`idProduct`/`bInterfaceNumber` values being searched for. The convenience method is doing something that doesn't match, for reasons not visible from the call site |
| `1c9ae5a` | Add `--diag-interfaces` (bare system-wide `IOUSBHostInterface` enumeration, no filter) | Settle visibility-gate vs matching-bug definitively. (Built; superseded as the next step by the classic path below before it was run) |
| `de94947` | **Add the classic IOUSBLib acquisition path** (`CreateInterfaceIterator` + `USBInterfaceOpenSeize`) — the libusb/vendor mechanism. Default for `MakeLiveUvcSource`; `OPENSCANNER_UVC_IOUSBHOST=1` forces the old path | The modern IOUSBHost lookup is exhausted (§2); the classic API enumerates interfaces through the *device object* (not the global registry) and seizes at the interface level — the route libusb, and thus the vendor's `RPCServer`, uses on this exact device. Every symbol checked against the SDK headers. **Awaiting the first hardware run** |

**`443fcb6` also failed, identically** (`plan-uvc-handdict.md`): both
depth and IR, 15/15 retries, same error. **No further matching-dictionary
code change is planned** — four structurally different lookup mechanisms
(the original plain `IORegistryEntryGetChildIterator` child-walk with no
property filter at all, the Objective-C convenience dictionary, and this
hand-built one) have all failed identically. See the rewritten §2 below:
the evidence now points at registry *visibility*, not at matching syntax,
and no variant of "ask IOKit to match on these properties" is going to fix
a node our process cannot enumerate in the first place.

## 2. Root cause: macOS's own `UVCAssistant` owns every interface

Confirmed directly via `ioreg -p IOService -n "Creality Raptor Pro" -l -r`
(critically: **not** `ioreg -p IOUSB`, which shows the device node but none
of its interface children at all — this cost several rounds before the
right plane was checked):

```
Creality Raptor Pro Depth Camera@1  <class IOUSBHostInterface...>
  "bInterfaceNumber" = 1
  "UsbExclusiveOwner" = "pid 588, UVCAssistant"
  "CameraAssistantBundleID" = "com.apple.cmio.uvcassistantextension"
```

Every video-streaming interface (depth, IR, RGB — both the VideoControl
and VideoStreaming halves) shows the same pattern. The IMU/HID interface
shows `"UsbExclusiveOwner" = "AppleUserUSBHostHIDDevice"` instead. Also
confirmed independently: `system_profiler SPCameraDataType` lists
"Creality Raptor Pro RGB Camera" as a system camera (`VendorID_11205
ProductID_3075` — this exact device).

**This is why `--probe` always worked and every capture attempt failed
identically regardless of lookup mechanism**: `--probe`
(`iokit_probe.mm`'s `ProbeAttachedDevice`) reads the whole configuration
descriptor directly from the `IOUSBHostDevice` object's
`configurationDescriptor` property — no interface-level claim needed.
`--capture` needs an actual `IOUSBHostInterface` object to call
`selectAlternateSetting:`/`copyPipeWithAddress:`/`sendDeviceRequest:`, and
that object was already exclusively held by Apple's own CoreMediaIO UVC
driver the instant the device was plugged in — independent of the vendor
daemon, independent of anything claimed at the device level.

`DeviceSeize` on the interface was the planned fix (`4fc7b6c`), but it
never got to run: `DeviceSeize` needs the interface's `io_service_t` first,
and that lookup itself has now failed under **four** different mechanisms:

1. The original plain `IORegistryEntryGetChildIterator` child-walk, **no
   property matching at all** — just "give me every child, check its
   class and `bInterfaceNumber` by hand."
2. That same child-walk, retried 10×50ms (`a74c9cb`).
3. `IOUSBHostInterface createMatchingDictionaryWithVendorID:...` +
   `IOServiceGetMatchingService` (`64170a3`).
4. A hand-built `IOServiceMatching` + `CFDictionarySetValue` dictionary,
   the identical shape that already works for the top-level device
   (`443fcb6`).

All four return **zero** — not a wrong match, not an error, nothing found
— while `ioreg -p IOService` (a separate, privileged tool) sees the exact
same nodes immediately, fully `registered, matched, active`, with the
exact properties being searched for. Mechanism (1) is the important data
point: a bare child-iterator walk with no property filter at all still
finds nothing, which rules out "wrong dictionary" as the explanation.
**Our process cannot enumerate these specific children in the registry at
all, regardless of how it asks.**

The most likely reconciling explanation: visibility of these interface
nodes to third-party `IOServiceGetMatchingService`/registry-walk calls is
gated to entitled or privileged callers, specifically because they are
**DriverKit-published** (owned by `UVCAssistant`, a dext) — unlike the
top-level device node, which is published by the classic in-kernel
`IOUSBHostFamily.kext` and opens fine for us with no special entitlement.
Both node levels carry the same `IOServiceDEXTEntitlements` property
annotation, but only the device level is actually reachable by us — which
fits "the annotation names what's needed to *manage* the node; the device
itself isn't managed by a dext, its interface children are."

**This means no further matching-dictionary variant is likely to help** —
the problem is upstream of matching, in whether the node is visible to us
at all. A `--diag-interfaces` mode (`1c9ae5a`) was added to settle the
visibility question definitively: it does a bare
`IOServiceGetMatchingServices(IOUSBHostInterface)` with no filter and
lists every interface this process can see, scanner or not. Result
pending a run.

### 2b. Live proof the seize works: RPCServer displaces UVCAssistant

`ioreg -p IOService -n "Creality Raptor Pro" -l -r` **with the vendor daemon
running** (2026-10-24) shows the interface nodes' `UsbExclusiveOwner` is
**`"pid 967, RPCServer"`** — not `UVCAssistant`. In every `raw_capture` test
(vendor daemon stopped, device freshly power-cycled) the same nodes showed
`UsbExclusiveOwner = UVCAssistant`. So ownership *flips*: `UVCAssistant` when
the device is idle, `RPCServer` when the vendor daemon runs.

This is direct, live evidence that **interface-level seize from UVCAssistant
works on this device** — the vendor does exactly it. It confirms the vendor
takes an exclusive interface claim (consistent with libusb's
`USBInterfaceOpenSeize`), and that the classic IOUSBLib path (`de94947`) is
built on the right mechanism. Our test protocol stops RPCServer first, which
releases the interfaces back to `UVCAssistant`, and then the classic path
attempts the same seize the vendor demonstrably performs. `system_profiler
SPCameraDataType` still lists "Creality Raptor Pro RGB Camera" throughout —
CoreMediaIO keeps advertising it as a camera (the `CameraAssistantBundleID`
annotation persists on the nodes) even while RPCServer holds the exclusive
claim.

### 2a. The entitlement is for a DriverKit *driver*, not our client

Read from Apple's own docs (`developer.apple.com/documentation/
bundleresources/entitlements/com.apple.developer.driverkit.transport.usb`,
retrieved 2026-10-24 via the browser — WebFetch only gets the JS shell):

> "An array of dictionaries that identify the USB devices **the driver**
> supports. [...] Each element in the array is a dictionary whose keys and
> values identify a specific type of supported device. The keys in the
> dictionary correspond to field names of the device descriptor."

Value type: **array of dictionaries** (keys: `idVendor`, `idProduct`,
`idProductArray`, `idProductMask`, `bInterfaceClass`, `bInterfaceNumber`,
…), **not** a boolean or a bare `"*"`. The `"*"` wildcard the free-tier
change refers to is how the *App ID capability / provisioning profile*
grants the entitlement broadly — not the entitlement value in the plist,
which still describes which devices the driver matches.

The decisive phrase is "**the driver**": this entitlement governs
**driver matching** — it lets a DriverKit extension (`.dext`) you write
*become the matched driver* for a USB device/interface. It is **not** a
capability a regular `IOUSBHost.framework` client process uses to claim an
interface another driver already owns. `raw_capture` is a plain client,
not a dext, so signing it with this entitlement is very unlikely to change
anything — it isn't a driver competing to match the device.

**What this means for the three options below:** option 2 (the
entitlement) is not "sign `raw_capture` and retry" — it is "write an
actual `.dext` that matches and drives the Creality USB interfaces,
displacing `UVCAssistant`." That is a real architectural fork for Phase
D, not a quick unblock. The machine now *does* have a valid Apple
Development cert (Team ID `9B9QVA8UFS`, created 2026-10-24) so the dext
path is no longer blocked on account enrolment — but it is a large build
(a DriverKit system extension, its install/approval flow, and the app
that hosts it), not a signing tweak.

New lead worth recording: the vendor's own `RPCServer` **does** capture
these interfaces, and per earlier recon it uses **libusb** and runs as
**root** (`/usr/bin/sudo /Library/Creality/RPCServer`). libusb on macOS
claims USB interfaces via its own IOKit seize path, and root may be what
lets that seize succeed where our unprivileged `IOUSBHost` client's did
not. This is a fourth option not yet explored: match what the vendor
actually does (libusb + root), rather than `IOUSBHost` + entitlement or a
dext. The `--diag-interfaces` run (and a future root-privileged variant)
will help separate "invisible without privilege" from "visible but
unclaimable without a dext."

**Four paths forward, none of them "try a fifth matching dictionary":**

1. **Kill `UVCAssistant`** (pid 588 in the observed run — pids are
   per-boot, re-check before acting) so its claim goes away, and see
   whether the interfaces then become claimable by a plain open. **Not to
   be done unilaterally**: it is a system extension; killing it has
   session-wide effects (every app using any camera loses it until it
   respawns). Needs an explicit decision. Cheapest test of "visible but
   unclaimable."
2. **Write a DriverKit extension (`.dext`)** with the
   `com.apple.developer.driverkit.transport.usb` entitlement (wildcard
   dev form, now signable with the `9B9QVA8UFS` cert) that matches the
   Creality interfaces and becomes their driver instead of `UVCAssistant`.
   The Apple-sanctioned architecture, but a large undertaking.
3. **Try routing through `openscanner-helper`** — it has only ever claimed
   the device-level node; interface-level access through it is untested.
4. **Match the vendor: libusb + root.** The vendor daemon demonstrably
   captures these interfaces this way. Least "clean" but the known-working
   reference; worth a controlled test before committing to the dext build.

## 3. Test record index (2026-10-24, chronological)

| Record | Verdict | What it settled |
|---|---|---|
| `plan-uvc-probe-bcduvc.md` | PASS | First real `format_index`/`frame_index`/`bcd_uvc` capture — later found to contain a pairing bug (fixed in `42027bb`'s predecessor) |
| `plan-uvc-probe-indexfix.md` (corrected in `8cafd4d`) | PASS | Corrected format/frame index values, UVC 1.1 confirmed (34-byte Probe/Commit struct) |
| `plan-uvc-capture-retest.md`, `plan-uvc-retest.md`, `plan-uvc-clearstall.md` | FAIL | Alt-setting fix confirmed; clearStall made no observable difference |
| `plan-uvc-probecommit.md` | FAIL | Negotiation implemented: depth's full sequence succeeded with *no* negotiation error, yet the read still failed — "necessary but not sufficient." IR failed at the first `GET_CUR` itself |
| `plan-uvc-piperefresh.md` | FAIL | Ordering ruled out (IR fails identically first or second). Depth's failure point moved from "Unable to send IO" to the same `GET_CUR` rejection IR already showed — pipe-refresh didn't fix it, but simplified the picture to one failure mode |
| `plan-uvc-getcur-baseline.md` | FAIL | New failure, *before* negotiation: "interface 1 is not on the device" on a freshly power-cycled device, moments after `--probe` saw it fine |
| `plan-uvc-iface-retry.md` | FAIL | Bounded retry (10×50ms) around the child-walk lookup — same error, now with the retry count in the text. Registry-race-via-retry theory refuted |
| `plan-uvc-direct-match.md` | FAIL | Direct `IOServiceGetMatchingService` matching instead of child-walk — identical error. Both lookup *mechanisms* now exhausted |
| `plan-uvc-devicecapture.md` | FAIL | `DeviceCapture` + root — identical error even as root. Led to the `ioreg -p IOService` investigation that found §2's root cause |
| `plan-uvc-seize.md` | FAIL | First `DeviceSeize` attempt — but the interface *lookup* itself failed before the seize code ran (no retry wrapping it after `64170a3`'s "no retry needed" assumption). Led to `c1321db` |
| `plan-uvc-findseize.md` | FAIL | Combined retry (`c1321db`), depth + IR together — both exhaust all 15 attempts identically. Proved the failure isn't stream-specific and isn't a timing issue the retry alone can fix; pointed at the matching dictionary itself. Led to `443fcb6` |
| `plan-uvc-handdict.md` | FAIL | Hand-built dictionary (`443fcb6`), depth + IR — identical failure to every prior mechanism. Four lookup mechanisms now exhausted (see §2's rewrite); reframed the problem from "matching syntax" to "registry visibility." No fifth dictionary variant planned — next move is one of §2's named options, pending a decision |
| *(available)* | — | `--diag-interfaces` (`1c9ae5a`): bare system-wide `IOUSBHostInterface` enumeration, no filter — settles visibility-gate vs matching-bug. Superseded as the critical step by the classic path; not run |
| `plan-uvc-ui-baseline.md` | **PASS** | Full vendor-SDK pipeline end-to-end: SDK loads, device found, scan runs, FRAME_READY flowing, UI served on 8787, live cloud in the browser (frame #1067, 19894 pts, 60 fps render, frame age p50 5.2/p95 13 ms). Best run yet, at scan_server's 10 Hz ceiling; confirms the raw_capture/power-cycle experiments broke nothing. Caveat: served `hw/next`'s **old** red/white overlay, not the density gradient (§4a) |
| *(in flight)* | — | First hardware run of the classic IOUSBLib path (`de94947`), not yet landed |

Full raw logs and sink files are committed alongside each `.md` record in
`tests/results/`.

## 4. Corrected hypotheses — Phase D design inputs, not merge details

These two change what Phase D's eventual daemon replacement needs to do,
independent of whether the IOKit blocker in §2 resolves:

1. **HID (interface 6, the IMU) is input-only.** Its 25-byte report
   descriptor declares no output fields (`tools/raw_capture/hid_report.cpp`'s
   parse + `--probe`'s own printout: "every field is input/feature; this
   HID interface cannot carry the laser/exposure control"). Laser/exposure
   control must be reproduced over the vendor's RPC property channel
   (`#101`'s property map) or the UVC extension units the probe already
   enumerates (`extension_units=4/8/12` on the three VideoControl
   interfaces) — **not** a HID output report, which was the row's original
   premise.

2. **The RPC IR frame is 4× the UVC IR frame.** RPC delivers 2,304,000
   bytes per IR frame (`#174`/`#124`'s findings); the live UVC IR stream is
   960×600 Y8 = 576,000 bytes — exactly 1/4. Leading hypothesis: the vendor
   daemon aggregates four light-state captures (`ob_set_ir_frame_light_type`
   0/1/2/3, per `obscan_api.h`'s recon) into one RPC delivery; a raw UVC
   read only ever gets one light state per frame. **Untested** — needs a
   real UVC IR frame (blocked on §2) and a real RPC IR frame (needs the
   vendor daemon *running*, the opposite precondition from every UVC test
   so far) side by side.

3. **Depth is the better first validation target than IR.** Its UVC
   geometry (480×300 Y8 = 252,000 bytes) already matches the RPC depth
   geometry with no known multiplier — IR carries the unresolved 4× puzzle
   on top of everything else. Byte-for-byte comparison, once §2 resolves,
   is the direct test of whether this UVC path is a legitimate daemon
   replacement for depth specifically.

4. **The no-sudo privileged-helper claim already works** (`#177`/`#181`,
   landed 2026-10-21, `plan-b-nosudo-claim.md`: PASS end to end). That's
   real, independent progress — the daemon is not just blocked on UVC; the
   device-level claim and release story for the eventual replacement daemon
   is already proven.

5. **Two separate rate ceilings — don't conflate them.** The *vendor/
   scan_server* web-UI path has a hard **10 Hz** ceiling by design
   (`kLiveTickIntervalMs = 100`). The 2026-10-24 UI baseline run
   (`plan-uvc-ui-baseline.md`, PASS) hit that ceiling cleanly — data pinned
   at ~10 fps, 60 fps render, frame age p50 5.2 ms / p95 13 ms, the best
   end-to-end run yet — so for the vendor path the client now keeps up with
   the source completely; there is no pull-rate gap left to chase there.
   The separate ~50 fps figure is the **raw USB layer** (what a
   `raptor_client`/`raw_capture` daemon-replacement could pull straight from
   the device), a higher ceiling that only matters once the raw capture path
   works. These are different layers; the UI baseline says nothing about the
   raw-USB ceiling and vice versa.

### 4a. Worktree/branch divergence on `ui/` — handoff item

The `ui/` work (density gradient, orbit/pan/zoom camera controls, the Y-flip,
the WebSocket port fix) lives on **`xcode/user-test`** (started at `3cf3f7f`).
The raw_capture/daemon work lives on **`hw/next`**. Throughout this session
`hw/next` was merged *into* `xcode/user-test` repeatedly, but never the
reverse — so **`hw/next`'s `ui/` is the pre-gradient version** (binary
red/white coverage overlay, `coverage.js` still `this.seen = new Set()`).

Consequence: the UI baseline run served `scan_server` from `hw/next`, so it
rendered the **old red/white overlay, not the density gradient** — despite
the run otherwise being a clean PASS. The gradient itself was validated
earlier this session (operator test), so this is a which-branch artifact,
not a regression.

**This split is intentional (operator, 2026-10-24): the browser-served UI
(`hw/next`) and the app UI with the density gradient (`xcode/user-test`) are
deliberately separate branches of the UI, and integrating them is not a
priority.** Recorded only so that reading a hardware record's UI observations
against the wrong branch doesn't cause confusion — e.g. "red/white overlay"
in `plan-uvc-ui-baseline.md` is expected for the `hw/next`-served UI and is
not evidence about the gradient work. No action needed; do not merge the two
unless the operator asks.

## 5. Still open

- **The interface-ownership blocker — classic IOUSBLib path in flight.**
  Four IOUSBHost lookup mechanisms exhausted (§2). Rather than kill
  `UVCAssistant` (crude, ambiguous) or build a `.dext` (large), we took
  the cleanest form of §2a option 4: the classic `IOUSBLib` API that
  libusb/the vendor use (`de94947`). It enumerates interfaces via the
  device object's `CreateInterfaceIterator` — bypassing the global-registry
  match that failed every time — and seizes via `USBInterfaceOpenSeize`,
  which politely asks `UVCAssistant` to release. **Awaiting its first
  hardware run.** Possible outcomes: full capture (the unlock); a specific
  `USBInterfaceOpenSeize` refusal (`kIOReturnExclusiveAccess` — UVCAssistant
  won't yield, may then need root); or the iterator finds the interface but
  a later step fails (progress, narrower question). If the classic path
  also can't claim it, the remaining moves are a `.dext` (now signable with
  the `9B9QVA8UFS` cert) or the same classic path run as root.
- `--diag-interfaces` (`1c9ae5a`) is still available as a characterization
  tool if the classic path's failure mode is unclear, but is no longer the
  critical next step.
- IR 4×/light-state hypothesis — needs both a UVC IR frame and an RPC IR
  reference frame captured under their respective (mutually exclusive)
  preconditions, then compared. Blocked on the above until a UVC IR frame
  is obtainable at all.
- Laser/exposure control path — map the vendor RPC properties or the UVC
  extension units; HID is ruled out (§4.1).
- Pull-rate ceiling — untested whether a tighter client loop reaches the
  source's reported 50 fps.

## 6. Recommended landing split (for dispatch to execute)

This dossier's own commits are **not PR'd** — they sit directly on
`hw/next` / `xcode/user-test`. Recommended split once ready to land:

- **PR A** — `tools/raw_capture/**` + its device-free tests
  (`raw_capture_test`, `raw_capture_probe_test`, `raw_capture_correlation_test`,
  `raw_capture_hid_report_test` — 210/210 passing throughout). Agent tier,
  needs a verifier pass. Reusable and well-structured even though the live
  capture isn't fully proven yet.
- **PR B** — `tests/results/2026-10-21-plan-{b,c,f,uvc,170}*` and
  `2026-10-24-plan-uvc-*` records (script tier, provenance-checked).
  Include the negative results — they're the evidence trail, not noise.
  Exclude any `.rec` binary over the size the verifier flags.
- **PR C** — the UI/Xcode commit `3cf3f7f` (density colours, camera
  controls, Xcode signing). Unrelated to this investigation; do not mix.
- **PR D** — hygiene: `.gitignore` for `build-xcode/`, any scratch scripts
  sitting in the worktree root (`grep_list`, `sync-to-main.sh`, `ui/watch.sh`
  — none of these are mine; confirm ownership before touching).
- **Board moves**: #171/#178 stay `HW` until §2 resolves and §4.2/§4.3's
  depth validation actually runs. File §4's two corrected hypotheses (HID
  input-only; IR's 4× multiplier) as their own finding — they change Phase
  D's design, not just this row's status.
