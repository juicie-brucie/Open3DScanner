# openscanner-helper — privileged helper skeleton (row #170)

Phase D seed 2 from `docs/recomp/FULL-INDEPENDENCE.md` §5 L3 / §6 Phase D and
the #97 decision (own privileged helper, launchd/`SMAppService`, self-signed
for development). This is a skeleton, not the product.

## What is here, and what is deliberately not

Built:

- `openscanner-helper` — a daemon that claims the scanner on start, serves a
  minimal XPC protocol (`ping`, `claim`, `release`, `status`), logs every
  claim-state transition, and releases on quit and on unplug.
- `iokit_device_claim.mm` — the live claim: matches the scanner's USB device
  and takes exclusive ownership through `IOUSBHost`. **Default is a
  non-seizing open**, so a running vendor daemon keeps its claim and gets a
  clean refusal rather than being killed. `--seize` is opt-in.
  The legacy `IOUSBDevice` user client is not used: on the current stack the
  device is an `IOUSBHostDevice` and the legacy path fails at
  `IOCreatePlugInInterfaceForService` with `kIOReturnNoResources`, before any
  open. The IOUSBHost initializer establishes exclusive ownership and gives
  the termination interest handler that becomes the unplug event.
- `helper_state_test`, `helper_service_test`, `helper_xpc_test` — the
  device-free tests (`ctest -LE hardware`).
- `launchd/*.plist.in` — the LaunchDaemon and `SMAppService` service
  definitions. Nothing installs them.

Not here, by design:

- No streaming, no UVC/HID data path, no interface open, no frame read.
- No app change: the app's default launch path is untouched, and no client
  code in this row connects to the helper.
- No installer: no `SMAppService` registration call, no `launchctl bootstrap`,
  no plist copied to a system location. Do not load these persistently from
  this row — the operator gate below runs the binary in the foreground.

## The claim state machine

`helper_state.h` owns it; the invariant its test pins is **exactly one
backend release per successful claim**, and none for a claim that never
succeeded. Release happens on:

- **quit** — `SIGTERM`/`SIGINT` (also `~ClaimMachine`, best effort), and
- **unplug** — the IOKit service-termination interest, wired to the machine's
  unplug event.

Every transition logs one line:

```
claim: idle -> claiming (start)
claim: claiming -> claimed (device=iokit-usb)
claim: claimed -> releasing (quit)
claim: releasing -> released (quit: release complete)
```

## Run it without installing anything

```sh
cmake --build build --target openscanner-helper
build/tools/macos_helper/openscanner-helper --help
```

`--claim null` claims nothing (a run that touched nothing cannot look green).

## Development signing

Ad-hoc signing, no notarization:

```sh
tools/macos_helper/dev_sign.sh build/tools/macos_helper/openscanner-helper
```

or pass a Keychain self-signed identity as the second argument. launchd
refuses to run an unsigned daemon on current macOS.

## Registering it (a later row's decision — shown for completeness)

- **LaunchDaemon:** substitute `@HELPER_PROGRAM@` and `@MACH_SERVICE@` in
  `launchd/org.openscanner.helper.plist.in`, place it under
  `/Library/LaunchDaemons/`, `launchctl bootstrap system <plist>`.
- **`SMAppService`:** put the helper in the app bundle and the
  `smappservice-…` plist at `Contents/Library/LaunchDaemons/`, then register
  it with `SMAppService.daemon(plistName:).register()` from the app.

Neither is wired in this row.

## The operator gate: live claim and release

Run as root (the helper is a daemon, so this is the privilege it will have),
with the device connected. **Do not stop the vendor daemon for step 1** — the
refusal is the evidence that the default is non-seizing.

1. **A running vendor daemon is refused, not killed.**

   ```sh
   sudo build/tools/macos_helper/openscanner-helper --once
   ```

   Expect `once: claim: ok=false state=released (the USB device open was
   refused (0xe00002c5): another process holds the scanner (stop it: that is
   the operator gate))` and exit 1. The vendor daemon must still be running
   afterwards (`pgrep -fl Creality` or its LaunchDaemon label).

2. **Claim with the vendor daemon stopped.** Liveness of the claim is the
   point; how the daemon is stopped is the operator's call, once:

   ```sh
   sudo build/tools/macos_helper/openscanner-helper --once
   ```

   Expect `ok=true state=claimed`, then `releasing -> released`, exit 0.

3. **Claim and hold, then release on quit.** Foreground daemon, no install:

   ```sh
   sudo build/tools/macos_helper/openscanner-helper --claim iokit
   ```

   Expect `claiming -> claimed`, then `start: serving`. Press Ctrl-C:
   expect `quit: signal`, `claimed -> releasing (quit)`, exit 0, and the
   device available again afterwards.

4. **Release on unplug.** Same command, leave it serving, unplug the scanner's
   USB cable. Expect `claimed -> releasing (unplug)`, `releasing -> released
   (unplug: release complete)` without any signal. Replug: the helper does
   **not** re-claim on its own in this row (claim-on-connect is later work) —
   send an explicit `claim` over XPC or restart it.

5. **XPC against the real daemon** (optional, needs the plist loaded): a
   `status` request must report `backend=iokit-usb`. The in-process test
   already exercises the same path over a real XPC connection.

What the gate needs from the dispatcher: a self-signed (ad-hoc is fine)
helper binary on the operator's machine, the vendor daemon stopped for steps
2–4, and the device attached. No install, no plist, no app change.

## Tests

```sh
ctest -LE hardware -R 'helper_(state|service|xpc)_test' --output-on-failure
```

The state and service tests run anywhere; the XPC test needs macOS and uses
an anonymous listener, so it needs no launchd registration, no entitlements
and no root.
