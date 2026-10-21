# 2026-10-21 — the daemon code complete: helper, raw capture, shim, live source, switch

The Phase D daemon code is on `main`. What remains is the operator bring-up: the
descriptor probe, the live capture, the claim/release as the user, the vendor A/B, and
the switch.

## The code, by seed

- **#170** (PR #428) — the privileged helper skeleton; the live claim/release gate
  **PASS** on hardware (the 2026-10-21 records, `plan170-helper-claim`): the non-seizing
  refusal with RPCServer up (`0xe00002c5`), the claim with it booted out
  (`device=iokit-usb`), the release on quit and on cold unplug. It ran as **root via
  passwordless sudo**; #177 is the privilege model.
- **#171** (PR #445) — the raw-capture path: the bounded sink, the #101 laser/exposure
  command path, and the known/unknown descriptor table; the transports refused until the
  table exists.
- **#172** (PR #447) — the RPC shim: the `ob_rpc_*` surface, the 11-element whole-frame
  array (the #174 correction), discovery, and the offline #132 source.
- **#178** (PR #453) — the read-only **descriptor probe** (IOKit interfaces/endpoints/
  UVC VS/HID, the exact table fields, no guesses) and the **live UVC/HID source** (the
  bulk/interrupt pipe, a bounded payload assembler; isochronous and unknown-field
  refusals).
- **#179** (PR #455) — the shim's `LiveSource` seam (the helper claim query, the OB-words
  provider) and the explicit **default-off** daemon switch
  (`engine/include/openscanner/daemon_switch.h`; `obscan_api.h` untouched).

## The privilege model — #177 (in re-read)

The helper's test ran as root because the README said so; the vendor's own "daemon" is a
**user LaunchAgent** (`com.creality.RPCServer`, GUI domain, plist
`/Library/LaunchAgents/com.creality.RPCServer.plist`, program `/usr/bin/sudo
/Library/Creality/RPCServer`). The finding is a two-tier model:

- **now/unsigned:** the user-scope claim test (the #170 four steps as the user) with a
  stable self-signed identity; no per-run sudo; the only admin step is the vendor-agent
  stop (the HW-queue commands);
- **signed:** `SMAppService.daemon` with the one-time approval and notarization if the
  user-scope claim fails.

The explicit falsifier is being added before the re-read.

## The operator sequence (T7)

1. `raw_capture --probe` with the device → paste the descriptor table (no guesses; the
   `unknown` fields need the analyzer).
2. The **#170 four steps as the user** (the privilege decision; the vendor stopped).
3. The live IR+depth capture with **no vendor process**.
4. The shim with the live source; the vendor A/B on a known scene.
5. The switch (`daemon_switch.h`); the product switch stays held until the A/B and #126.

## Board

`HW`: #178, #179 (code landed; the live cards). `RECON`: #169 (the probe run), #173 (the
enumeration test), #177 (the privilege re-read). `DONE`: #170, #171, #172, #141, #167.
#100 remains the Phase D parent.
