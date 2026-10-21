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
- `launchd/org.openscanner.helper-agent.plist.in` — the **user LaunchAgent**
  (row #181), the development registration: user scope, no sudo.
- `launchd/org.openscanner.helper.plist.in` and
  `launchd/smappservice-org.openscanner.helper.plist.in` — the LaunchDaemon and
  `SMAppService` definitions (the system/signed-tier alternatives; not the
  development path).
- `install_user_agent.sh` — renders, dev-signs and bootstraps the user agent
  (and `uninstall`/`status`/`--dry-run`).

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

## Registering it: the user LaunchAgent (row #181)

The development model is a **user LaunchAgent** (the #177 Tier A path): no
sudo, no system daemon, no per-run password. The plist is
`launchd/org.openscanner.helper-agent.plist.in`; its `MachServices` key is what
registers `org.openscanner.helper` with launchd for the session, and the
helper's `MakeMachServiceListener` attaches to it.

```sh
# Build, dev-sign, install and bootstrap in gui/$(id -u). No sudo.
cmake --build build --target openscanner-helper raptor_shim
tools/macos_helper/install_user_agent.sh install

# Verify the registration and the service's state.
tools/macos_helper/install_user_agent.sh status
launchctl print gui/$(id -u)/org.openscanner.helper

# Ask the helper over XPC with the shim's own claim channel.
build/tools/raptor_shim/raptor_shim --helper-status
```

`install` runs `tools/macos_helper/dev_sign.sh` (ad-hoc by default; pass
`--identity "OpenScanner Dev"` for a stable self-signed identity), writes
`~/Library/LaunchAgents/org.openscanner.helper.plist`, and bootstraps it. Use
`--dry-run` to render and lint the plist without registering anything, and
`uninstall` to boot it out and remove the plist.

The helper claims the scanner on start. A vendor daemon that still holds the
device gives the non-seizing refusal, and the service keeps answering
`status` (the claim state) — stop the vendor daemon for the claim itself (the
HW-queue sequence). The `status` exit code is 0 when the service answered and
1 when it did not; a helper that is not registered answers with the text
"no Mach service is registered under that name..." naming this installer.

**LaunchDaemon / `SMAppService`** remain the system/signed-tier alternatives:
substitute `@HELPER_PROGRAM@` and `@MACH_SERVICE@` in
`launchd/org.openscanner.helper.plist.in` under `/Library/LaunchDaemons/`, or
put the helper in the app bundle and register
`smappservice-org.openscanner.helper.plist.in` with `SMAppService.daemon`.
Neither is used by the development flow.

## The operator gate: live claim and release (no sudo, row #181)

Run in the **user session** — no sudo, no per-run password. This is the #177
Tier A model: a user LaunchAgent with the helper dev-signed by `dev_sign.sh`.
Only one process can hold the claim, so **boot the agent out while running the
four foreground steps**, then install it again for the XPC check:

```sh
launchctl bootout gui/$(id -u)/org.openscanner.helper 2>/dev/null || true
```

With the device connected. **Do not stop the vendor daemon for step 1** — the
refusal is the evidence that the default is non-seizing.

1. **A running vendor daemon is refused, not killed.**

   ```sh
   build/tools/macos_helper/openscanner-helper --once
   ```

   Expect `once: claim: ok=false state=released (the USB device open was
   refused (0xe00002c5): another process holds the scanner (stop it: that is
   the operator gate))` and exit 1. The vendor daemon must still be running
   afterwards (`pgrep -fl Creality` or its LaunchDaemon label).

2. **Claim with the vendor daemon stopped.** Liveness of the claim is the
   point; how the daemon is stopped is the operator's call, once:

   ```sh
   build/tools/macos_helper/openscanner-helper --once
   ```

   Expect `ok=true state=claimed`, then `releasing -> released`, exit 0.

3. **Claim and hold, then release on quit.** Foreground daemon, no install:

   ```sh
   build/tools/macos_helper/openscanner-helper --claim iokit
   ```

   Expect `claiming -> claimed`, then `start: serving`. Press Ctrl-C:
   expect `quit: signal`, `claimed -> releasing (quit)`, exit 0, and the
   device available again afterwards.

4. **Release on unplug.** Same command, leave it serving, unplug the scanner's
   USB cable. Expect `claimed -> releasing (unplug)`, `releasing -> released
   (unplug: release complete)` without any signal. Replug: the helper does
   **not** re-claim on its own in this row (claim-on-connect is later work) —
   send an explicit `claim` over XPC or restart it.

5. **XPC against the registered service** (row #181): install the user agent,
   then ask it with the shim's own channel. No sudo.

   ```sh
   tools/macos_helper/install_user_agent.sh install
   build/tools/raptor_shim/raptor_shim --helper-status
   # [shim] helper status on org.openscanner.helper: claimed-by-helper
   ```

   A refusal and an unregistered service are both readable: the detail line
   names the claim state, or names this installer when launchd has no such
   Mach service.

What the gate needs: an ad-hoc/self-signed helper (the installer calls
`dev_sign.sh`), the vendor daemon stopped for steps 2–4, the device attached,
and the user agent booted out for the foreground steps. No sudo, no system
install, no app change. If the user-scope claim is refused, the #177 falsifier
applies: the signed `SMAppService.daemon` route is the fallback.

## Tests

```sh
ctest -LE hardware -R 'helper_(state|service|xpc)_test' --output-on-failure
```

The state and service tests run anywhere; the XPC test needs macOS and uses
an anonymous listener, so it needs no launchd registration, no entitlements
and no root.
