# UVC DeviceCapture retest — depth stream — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD SHA: 0aa773d3375a295cd254d47bbe34483d41508fdb
- Target: build/tools/raw_capture/raw_capture (binary mtime 01:46 >= source iokit_source.mm 01:46)
- Preflight: links no vendor SDK; exempt from interposer rule; freshness confirmed (binary 01:46 >= iokit_source.mm 01:46)
- Interposer: n/a
- Run start: 2026-10-24 (interactive terminal — sudo required; agent cannot authenticate non-interactively)
- Device: freshly power-cycled immediately before this run
- Run mode: sudo (required by IOUSBHostObjectInitOptionsDeviceCapture — needs root or com.apple.vm.device-access entitlement)

## Background

The prior two runs (`2026-10-24-plan-uvc-iface-retry.md`, `2026-10-24-plan-uvc-direct-match.md`) both failed with `interface 1 is not on the device` despite 10 retries and two different IOKit lookup strategies. Root cause analysis: `ioreg` confirmed this device publishes zero child IOKit interface services — no `IOUSBHostInterface` nodes exist in the registry regardless of search method. Fix: switched device open to `IOUSBHostObjectInitOptionsDeviceCapture`, which Apple documents as terminating other clients/drivers and making `IOUSBHostInterface` clients available. Requires root or the `com.apple.vm.device-access` entitlement; run here as root (`sudo`).

## Commands

```
# Precondition
launchctl bootout gui/501/com.creality.RPCServer
pgrep -x RPCServer   # → empty / exit 1 (rc=3: already stopped)

cmake --build build --target raw_capture   # up to date

# Probe
sudo build/tools/raw_capture/raw_capture --probe

# Capture
mkdir -p tests/results/2026-10-24-plan-uvc-devicecapture
sudo build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-devicecapture/depth.bin
```

## Probe output (--probe, sudo, exit 0)

```
[probe] vid=0x2BC5 pid=0x0C03 configuration=1 interfaces=7
[probe] interface number=0 alternate_setting=0 class=0x0E subclass=0x01 protocol=0x00 kind=video-control
[probe] extension_units=4
[probe] video_control bcd_uvc=0x0110 (probe_commit_control_length=34)
[probe] interface number=1 alternate_setting=0 class=0x0E subclass=0x02 protocol=0x00 kind=video-streaming
[probe] endpoint interface=1 alternate_setting=0 address=0x81 transfer=bulk max_packet_size=1024 interval=0
[probe] video_streaming format_index=1 frame_index=7
[probe] video_streaming format_guid=0x719B3800AA0000800010000020203859 fourcc=Y8   bits_per_pixel=8 width=480 height=300 max_frame_bytes=252000 bytes_per_line=840 (derived) frame_interval_100ns=333333
[probe] stream[0].endpoint.interface_number=1
[probe] stream[0].endpoint.alternate_setting=0
[probe] stream[0].endpoint.endpoint_address=0x81
[probe] stream[0].endpoint.transfer=bulk
[probe] stream[0].endpoint.max_packet_size=1024
[probe] stream[0].pixel_format=0x20203859
[probe] stream[0].width=480
[probe] stream[0].height=300
[probe] stream[0].bytes_per_line=840 (derived from max_frame_bytes/height)
[probe] interface number=2 alternate_setting=0 class=0x0E subclass=0x01 protocol=0x00 kind=video-control
[probe] extension_units=8
[probe] video_control bcd_uvc=0x0110 (probe_commit_control_length=34)
[probe] interface number=3 alternate_setting=0 class=0x0E subclass=0x02 protocol=0x00 kind=video-streaming
[probe] endpoint interface=3 alternate_setting=0 address=0x82 transfer=bulk max_packet_size=1024 interval=0
[probe] video_streaming format_index=2 frame_index=1
[probe] video_streaming format_guid=0x719B3800AA0000800010000020203859 fourcc=Y8   bits_per_pixel=8 width=960 height=600 max_frame_bytes=576000 bytes_per_line=960 (derived) frame_interval_100ns=333333
[probe] stream[1].endpoint.interface_number=3
[probe] stream[1].endpoint.alternate_setting=0
[probe] stream[1].endpoint.endpoint_address=0x82
[probe] stream[1].endpoint.transfer=bulk
[probe] stream[1].endpoint.max_packet_size=1024
[probe] stream[1].pixel_format=0x20203859
[probe] stream[1].width=960
[probe] stream[1].height=600
[probe] stream[1].bytes_per_line=960 (derived from max_frame_bytes/height)
[probe] interface number=4 alternate_setting=0 class=0x0E subclass=0x01 protocol=0x00 kind=video-control
[probe] extension_units=12
[probe] video_control bcd_uvc=0x0110 (probe_commit_control_length=34)
[probe] interface number=5 alternate_setting=0 class=0x0E subclass=0x02 protocol=0x00 kind=video-streaming
[probe] endpoint interface=5 alternate_setting=0 address=0x83 transfer=bulk max_packet_size=1024 interval=0
[probe] video_streaming format_index=1 frame_index=2
[probe] video_streaming format_guid=0x719B3800AA0000800010000032595559 fourcc=YUY2 bits_per_pixel=16 width=1280 height=800 max_frame_bytes=2048000 bytes_per_line=2560 (derived) frame_interval_100ns=333333
[probe] stream[2].endpoint.interface_number=5
[probe] stream[2].endpoint.alternate_setting=0
[probe] stream[2].endpoint.endpoint_address=0x83
[probe] stream[2].endpoint.transfer=bulk
[probe] stream[2].endpoint.max_packet_size=1024
[probe] stream[2].pixel_format=0x32595559
[probe] stream[2].width=1280
[probe] stream[2].height=800
[probe] stream[2].bytes_per_line=2560 (derived from max_frame_bytes/height)
[probe] interface number=6 alternate_setting=0 class=0x03 subclass=0x00 protocol=0x00 kind=hid
[probe] endpoint interface=6 alternate_setting=0 address=0x85 transfer=interrupt max_packet_size=248 interval=1
[probe] control.endpoint.interface_number=6
[probe] control.endpoint.alternate_setting=0
[probe] control.endpoint.endpoint_address=0x85
[probe] control.endpoint.transfer=interrupt
[probe] control.report_descriptor_length=25
[probe] note: the fields printed above are the table's names; the IR/depth assignment needs the RPC->endpoint map (OQ-169e).
[hid] descriptor: 25 bytes: 05 8C 09 01 A1 01 09 03 17 00 00 00 00 27 FF FF FF FF 75 20 95 3E 81 02 C0
[hid] report ids: 0
[hid] field report_id=0 type=input usage_page=0x008C usages=0x0003 bit_offset=0 bit_size=32 count=62 logical=0..-1 variable=yes constant=no
[hid] report id=0 type=input 1984 bit(s) = 248 byte(s)
[hid] output: none — every field is input/feature; this HID interface cannot carry the laser/exposure control, which stays the vendor RPC properties (#101)
```

## Capture result (--capture --stream depth, sudo, exit 2)

```
[capture] sink: tests/results/2026-10-24-plan-uvc-devicecapture/depth.bin (16 bytes)
[capture] stop: source-error
[capture] detail: interface 1 is not on the device (retried 10 times over ~500ms)
[capture] frames: 0 (ir 0, depth 0, rgb 0), 0 record bytes
```

- Exit code: 2
- Frames: 0
- Sink file: 16 bytes (header only)

## Checks

| # | Check | Result |
|---|---|---|
| 1 | RPCServer stopped | **[PASS]** bootout rc=3 (already stopped), pgrep empty |
| 2 | Binary freshness | **[PASS]** binary 01:46 >= iokit_source.mm 01:46 |
| 3 | --probe exit 0, interface 1 enumerated | **[PASS]** all 7 interfaces enumerated, stream[0] on interface 1 confirmed |
| 4 | Run as root (sudo) | **[PASS]** authenticated interactively |
| 5 | --capture depth exit 0, frames > 0 | **[FAIL]** exit 2, 0 frames, 16-byte sink |

## Outcome case matched

**"Same error, interface still not found"** — `IOUSBHostObjectInitOptionsDeviceCapture` + root privileges made no difference. The error text and retry count are identical to the prior non-root runs. Running as root does not bypass the `IOUSBHostInterface` registry absence for this device.

## On failure — observable + one lead

- Observable: `sudo build/tools/raw_capture/raw_capture --capture --stream depth`, exit 2. `interface 1 is not on the device (retried 10 times over ~500ms)` even as root with DeviceCapture option.
- Lead: the `IOUSBHostInterface` matching approach is fundamentally wrong for this device — `IOUSBHostInterface` may not be the correct IOKit class to match against for a device whose interfaces are managed by a third-party kext or USB composite driver (e.g. `IOUSBCompositeDriver` / `AppleUSBComposite`). Try `IOUSBInterface` (legacy class) or inspect `ioreg -p IOService -n "Creality Raptor Pro" -l -r` for the actual class names of the child nodes (`tools/raw_capture/iokit_source.mm`).

## Crash sweep

No process crash; exit 2 is the expected error-exit for source-error. No crash report applicable.

## Board

- Row move: #171 and #178 remain `HW` — `IOUSBHostObjectInitOptionsDeviceCapture` + root did not resolve the interface-not-found failure; a different IOKit matching class or approach is needed.
