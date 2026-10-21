# PLAN #179 — shim live source + product switch — BLOCKED

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raptor_shim/raptor_shim  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 16:02:16 >= newest source helper_claim.h 16:02:09
- Interposer: n/a
- Run start: 2026-10-21 16:20:58

## Command

```
build/tools/raptor_shim/raptor_shim --source live --no-depth --seconds 1
```

## Result

- Exit code: `2`
- Verdict: **BLOCKED**
- Raw log: `2026-10-21-plan179-live-blocked.raw.log` (same directory)

## On failure or block — observable + three leads

Verbatim refusal (single line, EXIT:2):

```
raptor_shim: live source refused:
  not-configured: the device is not claimed by openscanner-helper
    (openscanner-helper is not answering on org.openscanner.helper
    (reply was not a dictionary: Connection invalid));
  not-configured: IR endpoint (interface/address/transfer),
    bInterfaceNumber, bEndpointAddress, transfer type, UVC pixel format,
    stride (bytes per line), width/height; depth endpoint
    (interface/address/transfer), bInterfaceNumber, bEndpointAddress,
    transfer type, UVC pixel format, stride (bytes per line), width/height
    (docs/recon/findings/usb-descriptor-map.md §2.1);
  not-configured: the live transport does not supply the frame's OB words
    (e2/e3/e10)
```

Three distinct blockers, each independently required:

1. **Helper not claiming** — `openscanner-helper --claim iokit` must be running and the device must be claimed before the shim's live source will open. The XPC service (`org.openscanner.helper`) is not registered with launchd in this session (`Connection invalid`). Resolved by: operator starts the helper via `openscanner-helper --claim iokit` after registering the service with launchd or using `SMAppService`.
2. **USB endpoint map unknown (OQ-169e)** — `descriptor_table.h` has IR and depth USB endpoints as "unknown" (confirmed by `raw_capture --table`, see `2026-10-21-plan178-probe.md`). Resolved by: OQ-169e (RPC→endpoint map) must be filled into `tools/raw_capture/descriptor_table.h`.
3. **OB words provider not implemented** — The live transport carries pixels, timestamps, and UVC format; `e2`/`e3`/`e10` (vendor time word, OBFormat, stream index) are not yet mapped from the raw UVC frame. Resolved by: builder implements the OB-words provider in the `FrameSource` seam (`tools/raptor_shim/live_source.h`).

Lead: all three leads are named in the refusal itself; each is a separate row/open-question for the fixer.

## Crash sweep

no crash report for 'raptor_shim' at/after 2026-10-21 16:20:58 — safe to record 'no crash report (checked DiagnosticReports for raptor_shim at/after the run start)'.

## Open questions

- When OQ-169e is resolved and the OB-words provider is implemented, does the shim's live source start cleanly with the helper claiming?
- Does the helper's launchd registration need to be re-done for the shim's XPC query to succeed in a non-app context?

## Board

- Row move: #179 stays `HW` — live gate blocked by three independent not-configured conditions; the product switch (daemon_switch.h) is confirmed present and parseable (tested in #172 via `--daemon shim`); the live source gate requires OQ-169e + OB-words provider before it can run
