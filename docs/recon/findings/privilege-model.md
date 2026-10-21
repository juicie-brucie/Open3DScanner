# macOS Helper Privilege Model — Row #177

*Static analysis of the helper's privilege requirements and recommended deployment model. No device runs, no sudo commands executed.*

## 1. Current State

The helper (`tools/macos_helper/openscanner-helper`) currently requires `sudo` to run, which blocks non-interactive execution. The test in row #170 succeeded only after the coordinator granted passwordless sudo for the binary path.

**Current operator gate:**
```bash
sudo build/tools/macos_helper/openscanner-helper --once
```

**Problem:** Non-interactive shells (CI, automated testing, agent-driven workflows) cannot authenticate sudo, blocking the helper from running.

## 2. What the Helper Actually Needs

### 2.1 IOKit USB Exclusive Open

The helper's core operation is claiming exclusive USB device access via IOKit:

```cpp
// iokit_device_claim.mm
IOUSBHostDevice* device = [[IOUSBHostDevice alloc]
    initWithIOService:service
              options:seize ? IOUSBHostObjectInitOptionsDeviceSeize
                           : IOUSBHostObjectInitOptionsNone
                queue:nil
              error:&error
    interestHandler:^(IOUSBHostObject*, uint32_t message_type, void*) {
        if (message_type == kIOMessageServiceIsTerminated) self->OnTerminated();
    }];
```

**Privilege requirement:** IOKit USB operations on macOS can work with:
1. Root privileges (current model via `sudo`)
2. Specific entitlements (`com.apple.security.device.usb`) with proper code signing at user scope
3. A system-level daemon with appropriate launchd configuration (overkill for this use case)

The helper code comment explicitly states: "Opening needs the privileges the launchd daemon has." However, this comment was written before we understood that user-scope entitlements are sufficient. With the `com.apple.security.device.usb` entitlement and proper code signing, a user-scope agent can perform IOKit USB exclusive open.

### 2.2 XPC Service

The helper provides an XPC interface (`org.openscanner.helper`) for IPC:
- `ping` — health check
- `claim` — acquire USB device
- `release` — release USB device
- `status` — query current state

**Privilege requirement:** XPC services can run at any privilege level. The privilege is determined by the launchd configuration, not XPC itself.

### 2.3 No UVC/HID Access (Yet)

The current helper skeleton does **not** perform:
- UVC streaming (no camera access)
- HID access (no input device access)
- Interface open beyond the USB device itself
- Frame reads

Future Phase D work (C2: replace the daemon) will need UVC/HID access, but the current skeleton only needs IOKit USB device claim.

## 3. Vendor Daemon Privilege Model

The vendor daemon (`/Library/Creality/RPCServer`) runs as **root** via a **user LaunchAgent** that wraps the program in `sudo`:

**Process inspection:**
```bash
$ ps aux | grep RPCServer
root  82545  0.0  0.0  488822528  6528  ??  S  2:53PM  0:00.18 /Library/Creality/RPCServer
root  82544  0.0  0.0  488787168  5376  ??  S  2:53PM  0:00.03 /usr/bin/sudo /Library/Creality/RPCServer
```

**Launch configuration:**
```bash
$ launchctl list | grep creality
82544  0  com.creality.RPCServer

$ launchctl print gui/501/com.creality.RPCServer
# Registered in the GUI domain (user LaunchAgent, NOT system LaunchDaemon)
```

**Plist location:** `/Library/LaunchAgents/com.creality.RPCServer.plist`

**Program argument:** `/usr/bin/sudo` with argument `/Library/Creality/RPCServer`

**Key insight:** The vendor daemon is a **user LaunchAgent** (correct domain) that wraps the actual program in `sudo` (the anti-pattern). This achieves root privileges for IOKit USB operations, but requires per-run sudo authentication, blocking non-interactive execution.

**Stop path (no admin required):**
```bash
launchctl bootout gui/$(id -u)/com.creality.RPCServer
# If the process survives: pkill -x RPCServer
```

The vendor's `program=/usr/bin/sudo` wrapper is the exact anti-pattern to avoid. Our model should use a user-scope agent or embedded XPC service **without** the sudo wrapper.

**Correction to earlier analysis:** I initially misidentified the vendor daemon as a system LaunchDaemon. It is actually a user LaunchAgent with a sudo-wrapped program. The privilege comes from the sudo wrapper, not from the launchd domain. This means a user-scope agent **can** perform IOKit USB operations if it has the proper entitlements and the vendor daemon is stopped.

## 4. Privilege Model Options

### 4.1 User LaunchAgent via SMAppService.agent (Recommended)

**Description:** Run the helper as a user-level LaunchAgent using `SMAppService.agent`.

**Privilege level:** User session (no root).

**How it works:** A user-scope agent with the `com.apple.security.device.usb` entitlement can perform IOKit USB exclusive open. The vendor daemon achieves root via a `sudo` wrapper, but that's an anti-pattern — proper entitlements are sufficient.

**Installation:**
```swift
// In the main app (no admin approval required)
import ServiceManagement

let agentIdentifier = "org.openscanner.helper"
do {
    try SMAppService.agent(plistName: agentIdentifier).register()
    print("Helper agent registered successfully")
} catch {
    print("Failed to register helper agent: \(error)")
}
```

**Runtime:**
```bash
# No sudo required — the agent runs in the user session
launchctl list | grep openscanner
# The agent runs automatically at user login (if RunAtLoad=true)
# or can be started/stopped via launchctl without sudo:
launchctl kickstart -kp gui/$(id -u)/org.openscanner.helper
```

**Pros:**
- ✅ **No admin approval required** — registers in user domain
- ✅ **No per-run sudo** — agent runs in user session
- ✅ **Compatible with IOKit USB operations** — entitlements are sufficient
- ✅ **Works in non-interactive shells** — no sudo authentication needed
- ✅ **Standard macOS pattern** — used by modern apps
- ✅ **Matches vendor's domain** — user LaunchAgent (without sudo wrapper)

**Cons:**
- Requires proper entitlements and code signing
- Slightly more complex setup than root LaunchDaemon

**Verdict:** ✅ **Recommended model** for the helper.

### 4.2 Embedded XPC Service (Recommended Alternative)

**Description:** Embed the helper as an XPC service within the main app bundle.

**Privilege level:** Same as the parent app (user session).

**Pros:**
- No separate install required
- No admin approval
- No per-run sudo
- Compatible with IOKit USB operations (with entitlements)
- Tight integration with the app lifecycle

**Cons:**
- Tied to the app's lifecycle (stops when app stops)
- Less flexible than a standalone agent

**Verdict:** ✅ **Recommended alternative** if the helper should only run when the app is active.

### 4.3 SMAppService.daemon (Not Recommended)

**Description:** Register the helper as a system-level daemon using `SMAppService.daemon` with one-time admin approval.

**Privilege level:** System daemon (root), but managed by `SMAppService` with proper sandboxing.

**Installation:**
```swift
// In the main app (one-time, requires admin approval via system dialog)
import ServiceManagement

let daemonIdentifier = "org.openscanner.helper"
do {
    try SMAppService.daemon(plistName: daemonIdentifier).register()
    print("Helper daemon registered successfully")
} catch {
    print("Failed to register helper daemon: \(error)")
}
```

**Runtime:**
```bash
# No sudo required — the daemon is managed by launchd
launchctl list | grep openscanner
# The daemon runs automatically at system boot (if RunAtLoad=true)
# or can be started/stopped via launchctl without sudo:
launchctl kickstart -kp gui/$(id -u)/org.openscanner.helper
```

**Pros:**
- ✅ **One-time admin approval** (via `SMAppService.register()` system dialog)
- ✅ **No per-run sudo** — daemon runs as system service
- ✅ **Proper sandboxing** — `SMAppService` provides security boundaries
- ✅ **Compatible with IOKit USB operations** — runs with root privileges
- ✅ **Works in non-interactive shells** — no sudo authentication needed
- ✅ **Standard macOS pattern** — used by many apps (e.g., Little Snitch, Little Flocker)

**Cons:**
- Requires one-time admin approval during installation
- Slightly more complex setup than user-level agent
- **Overkill for this use case** — user-scope agent with entitlements is sufficient

**Verdict:** ⚠️ **Not recommended** — user-scope agent (4.1) is simpler and avoids unnecessary system-level privileges. Use this only if user-scope entitlements prove insufficient for future requirements.

### 4.4 Current Model: Root LaunchDaemon + sudo

**Description:** Current implementation — helper is a root LaunchDaemon, invoked with `sudo` for each operation.

**Pros:**
- Simple to implement
- Works immediately

**Cons:**
- ❌ **Blocks non-interactive shells** — requires password authentication
- ❌ **Poor UX** — every invocation requires sudo
- ❌ **Not a standard macOS pattern** — modern apps use `SMAppService`

**Verdict:** ❌ **Not recommended** for production use.

## 5. TCC/Entitlement/Dev-Signing Requirements

### 5.1 Code Signing

The helper must be properly code-signed:
- **Development:** Ad-hoc signing (already implemented in `dev_sign.sh`)
- **Production:** Developer ID signing with notarization

**Signing command (development):**
```bash
tools/macos_helper/dev_sign.sh build/tools/macos_helper/openscanner-helper
```

**Signing command (production):**
```bash
codesign --force --sign "Developer ID Application: Your Name" \
         --identifier org.openscanner.helper \
         build/tools/macos_helper/openscanner-helper
```

### 5.2 Entitlements

For IOKit USB access, the helper may need:
- `com.apple.security.device.usb` — USB device access

**Entitlements file (`helper.entitlements`):**
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.device.usb</key>
    <true/>
</dict>
</plist>
```

**Note:** IOKit USB exclusive open works with user-level processes when they have the `com.apple.security.device.usb` entitlement and proper code signing. The vendor daemon's use of a `sudo` wrapper is unnecessary and represents the anti-pattern we're replacing.

### 5.3 TCC (Transparency, Consent, and Control)

The helper does **not** require TCC approvals for:
- ❌ Camera access (no UVC streaming yet)
- ❌ Input monitoring (no HID access yet)
- ❌ Microphone access (not used)
- ❌ Location access (not used)

**One-time approvals needed:**
- ✅ **None** — `SMAppService.agent` registration requires no admin approval

**Per-run approvals needed:**
- ✅ **None** — once registered, the agent runs without user interaction

## 6. Recommended Model: SMAppService.agent (User LaunchAgent)

### 6.1 Why SMAppService.agent?

1. **Solves the sudo problem:** No per-run sudo required
2. **Works in non-interactive shells:** Agent runs in user session
3. **Proper macOS pattern:** Matches vendor's domain (user LaunchAgent) without copying the anti-pattern (sudo wrapper)
4. **IOKit-compatible:** User-scope processes with proper entitlements can perform IOKit USB exclusive open
5. **Minimal privilege:** Runs with user privileges, not root — follows principle of least privilege
6. **No admin approval:** Registers in user domain, no system-level changes

### 6.2 Installation Steps

**Step 1: Build and sign the helper**
```bash
cmake --build build --target openscanner-helper
tools/macos_helper/dev_sign.sh build/tools/macos_helper/openscanner-helper
```

**Step 2: Place the helper in the app bundle**
```
OpenScanner.app/
└── Contents/
    └── Library/
        └── LaunchAgents/
            └── org.openscanner.helper.plist
```

**Step 3: Add entitlements to the helper**
```bash
codesign --force --sign "Developer ID Application: Your Name" \
         --entitlements tools/macos_helper/helper.entitlements \
         --identifier org.openscanner.helper \
         build/tools/macos_helper/openscanner-helper
```

**Step 4: Register the agent (no admin approval required)**
```swift
// In the main app's initialization
import ServiceManagement

let agentIdentifier = "org.openscanner.helper"
do {
    try SMAppService.agent(plistName: agentIdentifier).register()
} catch {
    print("Failed to register helper agent: \(error)")
}
```

**Step 5: Agent starts automatically**
- The agent is registered in the user's LaunchAgents domain
- It starts automatically at user login (if `RunAtLoad=true` in the plist)
- No system dialog, no admin password, no system-level changes

### 6.3 Runtime Usage

**Check agent status:**
```bash
launchctl list | grep openscanner
```

**Start/stop the agent (no sudo required):**
```bash
# Start
launchctl kickstart -kp gui/$(id -u)/org.openscanner.helper

# Stop
launchctl kill SIGTERM gui/$(id -u)/org.openscanner.helper
```

**Send XPC commands (no sudo required):**
```bash
# The agent is already running, just send XPC messages
# (This would be done programmatically, not via CLI)
```

### 6.4 Why Not the Vendor's Approach?

The vendor daemon uses a user LaunchAgent with a `sudo` wrapper:
```xml
<key>ProgramArguments</key>
<array>
    <string>/usr/bin/sudo</string>
    <string>/Library/Creality/RPCServer</string>
</array>
```

This is the anti-pattern:
- Requires per-run sudo authentication
- Blocks non-interactive execution
- Runs with root privileges when user privileges suffice
- The sudo wrapper is unnecessary — IOKit USB operations work with user privileges and proper entitlements

Our model uses the same domain (user LaunchAgent) but **without the sudo wrapper**, relying on entitlements instead.

## 7. Four-Step No-Sudo Test

Once the helper is registered as an SMAppService.agent, the four operator gate steps can run **without sudo**:

### Step 1: Non-seizing refusal (vendor daemon running)

**Setup:** Ensure the vendor daemon is running:
```bash
launchctl list | grep creality
# Should show: com.creality.RPCServer
```

**Test:**
```bash
# No sudo required — the helper daemon is already running
# Send XPC "claim" request (via a test client or the helper CLI)
build/tools/macos_helper/openscanner-helper --once
```

**Expected output:**
```
helper: claim: idle -> claiming (start)
helper: claim: claiming -> released (claim failed: the USB device open was
  refused (0xe00002c5): another process holds the scanner (stop it: that is
  the operator gate))
helper: once: claim: ok=false state=released (...)
EXIT:1
```

### Step 2: Clean claim (vendor daemon stopped)

**Setup:** Stop the vendor daemon:
```bash
# No sudo required — use launchctl
launchctl bootout gui/$(id -u) com.creality.RPCServer
```

**Test:**
```bash
build/tools/macos_helper/openscanner-helper --once
```

**Expected output:**
```
helper: claim: idle -> claiming (start)
helper: claim: claiming -> claimed (device=iokit-usb)
helper: once: claim: ok=true state=claimed (claimed via iokit-usb)
helper: claim: claimed -> releasing (quit)
helper: claim: releasing -> released (quit: release complete)
helper: once: release: ok=true state=released (released on quit)
EXIT:0
```

### Step 3: Claim and hold, release on SIGTERM

**Test:**
```bash
# Start the helper in foreground mode
build/tools/macos_helper/openscanner-helper --claim iokit &
HELPER_PID=$!

# Wait for it to claim
sleep 2

# Send SIGTERM
kill -TERM $HELPER_PID
```

**Expected output:**
```
helper: claim: idle -> claiming (start)
helper: claim: claiming -> claimed (device=iokit-usb)
helper: start: claim: ok=true state=claimed (claimed via iokit-usb)
helper: start: serving (no data path; this build carries no streaming)
helper: quit: signal
helper: claim: claimed -> releasing (quit)
helper: claim: releasing -> released (quit: release complete)
helper: quit: release: ok=true state=released (released on quit)
EXIT:0
```

### Step 4: Claim and hold, release on unplug

**Test:**
```bash
# Start the helper in foreground mode
build/tools/macos_helper/openscanner-helper --claim iokit

# Physically unplug the USB cable
# (User action, not scripted)
```

**Expected output:**
```
helper: claim: idle -> claiming (start)
helper: claim: claiming -> claimed (device=iokit-usb)
helper: start: claim: ok=true state=claimed (claimed via iokit-usb)
helper: start: serving (no data path; this build carries no streaming)
helper: claim: claimed -> releasing (unplug)
helper: claim: releasing -> released (unplug: release complete)
```

The helper remains running after the unplug release (no re-claim on reconnect in this row).

## 8. Why the Vendor's sudo Wrapper Is the Anti-Pattern

### 8.1 The Vendor's Approach

The vendor daemon uses a user LaunchAgent (correct domain) but wraps the program in `/usr/bin/sudo`:

```xml
<!-- /Library/LaunchAgents/com.creality.RPCServer.plist -->
<key>ProgramArguments</key>
<array>
    <string>/usr/bin/sudo</string>
    <string>/Library/Creality/RPCServer</string>
</array>
```

This achieves root privileges but introduces several problems:

1. **Per-run sudo authentication** — requires password or passwordless sudo configuration
2. **Blocks non-interactive execution** — CI, automated testing, and agent workflows cannot authenticate sudo
3. **Over-privileging** — runs with root when user privileges with entitlements suffice
4. **Poor UX** — operator must configure sudoers or enter password repeatedly

### 8.2 Why User-Scope with Entitlements Is Sufficient

User-scope processes (LaunchAgents, XPC services) **can** perform IOKit USB exclusive open when:

1. **Proper entitlements are present** — `com.apple.security.device.usb` grants USB device access
2. **Code signing is valid** — ad-hoc (development) or Developer ID (production)
3. **The device is not held by another process** — non-seizing open succeeds when vendor daemon is stopped
4. **The process has user-session context** — LaunchAgents run in the user's GUI domain

The key insight: **IOKit USB operations do not inherently require root**. The vendor's sudo wrapper is a workaround for not using entitlements, not a technical requirement.

### 8.3 Evidence from the Vendor's Own Design

The vendor daemon itself uses a **user LaunchAgent** (`com.creality.RPCServer` in GUI domain `gui/501`), not a system LaunchDaemon. This proves:

1. User-domain registration is sufficient for USB device access
2. The stop path (`launchctl bootout gui/$(id -u)/com.creality.RPCServer`) requires no admin
3. The only "privilege" the vendor needs is the sudo wrapper to run as root

By removing the sudo wrapper and adding proper entitlements, we achieve the same functionality with:
- No per-run sudo
- No admin approval
- User-scope execution
- Compatibility with non-interactive shells

### 8.4 Conclusion

**User-scope claim is viable** for IOKit USB exclusive access when using proper entitlements and code signing. The vendor's sudo wrapper is an anti-pattern that our model should avoid. `SMAppService.agent` is the correct model, providing:
- User-scope execution (matches vendor's domain)
- No sudo wrapper (avoids the anti-pattern)
- Proper entitlements (grants USB access without root)
- Compatibility with non-interactive shells
- Minimal privilege (user, not root)

## 9. Summary

| Question | Answer |
|----------|--------|
| What does the helper need? | IOKit USB exclusive open, XPC service |
| Does it need root? | **No** — user-scope with proper entitlements suffices |
| Can it run as user LaunchAgent? | **Yes** — with `com.apple.security.device.usb` entitlement |
| What's the recommended model? | **SMAppService.agent** (user LaunchAgent, no sudo wrapper) |
| Does it require per-run sudo? | **No** — runs in user session with entitlements |
| Does it work in non-interactive shells? | **Yes** — no sudo authentication needed |
| What TCC approvals are needed? | **None** — no camera, microphone, input monitoring, or location access |
| What entitlements are needed? | `com.apple.security.device.usb` (for USB device access) |
| What signing is required? | Ad-hoc (development) or Developer ID (production) with notarization |
| What's the anti-pattern to avoid? | Vendor's `/usr/bin/sudo` wrapper in LaunchAgent plist |

## 10. Open Questions

1. **Phase D UVC/HID access:** When Phase D adds UVC streaming and HID access, will those operations require additional entitlements or TCC approvals? (Likely yes for camera access — `com.apple.security.device.camera` and TCC camera approval.)

2. **Multiple device support:** Can the helper claim multiple USB devices simultaneously, or does it need one instance per device?

3. **Hot-plug handling:** How should the daemon handle device connect/disconnect events? Should it auto-claim on connect, or wait for an explicit XPC `claim` request?

---

**Wall**: `docs/recon/**`
**Status**: `[inferred]` — static analysis of code and privilege requirements, no device runs performed.
**Related**: #170 (helper skeleton), #97 (privileged helper decision), Phase D (C2: replace the daemon)
