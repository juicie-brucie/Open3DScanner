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

**Privilege requirement:** IOKit USB operations on macOS require either:
1. Root privileges (current model via `sudo`)
2. Specific entitlements (`com.apple.security.device.usb`) with proper code signing
3. A system-level daemon with appropriate launchd configuration

The helper code comment explicitly states: "Opening needs the privileges the launchd daemon has."

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

The vendor daemon (`/Library/Creality/RPCServer`) runs as **root** via `sudo`:

```bash
$ ps aux | grep RPCServer
root  82545  0.0  0.0  488822528  6528  ??  S  2:53PM  0:00.18 /Library/Creality/RPCServer
root  82544  0.0  0.0  488787168  5376  ??  S  2:53PM  0:00.03 /usr/bin/sudo /Library/Creality/RPCServer
```

**Launch configuration:**
```bash
$ launchctl list | grep creality
82544  0  com.creality.RPCServer
```

The vendor daemon is registered as a LaunchDaemon (`com.creality.RPCServer`), which means it runs at system level with root privileges. The `sudo` prefix in the process list indicates it was launched via `sudo` from a LaunchAgent, then registered as a system daemon.

**Key insight:** The vendor daemon's root privilege is required for IOKit USB exclusive access. Any replacement daemon (Phase D C2) will need equivalent privileges.

## 4. Privilege Model Options

### 4.1 User LaunchAgent via SMAppService.agent

**Description:** Run the helper as a user-level LaunchAgent using `SMAppService.agent`.

**Privilege level:** User session (no root).

**Pros:**
- No admin approval required
- Runs in user session
- No per-run sudo

**Cons:**
- **Will not work for IOKit USB exclusive open** — user-level processes cannot claim exclusive USB device access without specific entitlements and code signing
- Even with `com.apple.security.device.usb` entitlement, exclusive open may be blocked if another process (vendor daemon) holds the device

**Verdict:** ❌ **Not viable** for the current IOKit USB claim operation.

### 4.2 Embedded XPC Service

**Description:** Embed the helper as an XPC service within the main app bundle.

**Privilege level:** Same as the parent app (user session).

**Pros:**
- No separate install required
- No admin approval
- No per-run sudo

**Cons:**
- **Same limitation as SMAppService.agent** — user-level processes cannot perform IOKit USB exclusive open
- The app itself would need root privileges, which is not practical for a GUI application

**Verdict:** ❌ **Not viable** for IOKit USB operations.

### 4.3 SMAppService.daemon (Recommended)

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

**Verdict:** ✅ **Recommended model** for the helper.

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

**Note:** Even with entitlements, IOKit USB exclusive open requires root privileges or a system daemon context. Entitlements alone are not sufficient for user-level processes.

### 5.3 TCC (Transparency, Consent, and Control)

The helper does **not** require TCC approvals for:
- ❌ Camera access (no UVC streaming yet)
- ❌ Input monitoring (no HID access yet)
- ❌ Microphone access (not used)
- ❌ Location access (not used)

**One-time approvals needed:**
- ✅ **Admin approval for SMAppService.daemon registration** — prompted by `SMAppService.register()` during installation

**Per-run approvals needed:**
- ✅ **None** — once registered, the daemon runs without user interaction

## 6. Recommended Model: SMAppService.daemon

### 6.1 Why SMAppService.daemon?

1. **Solves the sudo problem:** No per-run sudo required
2. **Works in non-interactive shells:** Daemon runs as a system service
3. **Proper macOS pattern:** Standard approach for privileged helpers
4. **IOKit-compatible:** Runs with root privileges needed for USB exclusive open
5. **One-time setup:** Admin approval during installation, then automatic

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
        └── LaunchDaemons/
            └── org.openscanner.helper.plist
```

**Step 3: Register the daemon (one-time, requires admin approval)**
```swift
// In the main app's initialization
import ServiceManagement

let daemonIdentifier = "org.openscanner.helper"
do {
    try SMAppService.daemon(plistName: daemonIdentifier).register()
} catch {
    // Handle error (user denied admin approval, or other issue)
    print("Failed to register helper: \(error)")
}
```

**Step 4: System prompts for admin approval**
- macOS displays a system dialog: "OpenScanner would like to add a helper tool"
- User enters admin password (or uses Touch ID)
- Daemon is registered and starts automatically (if `RunAtLoad=true`)

### 6.3 Runtime Usage

**Check daemon status:**
```bash
launchctl list | grep openscanner
```

**Start/stop the daemon (no sudo required):**
```bash
# Start
launchctl kickstart -kp gui/$(id -u)/org.openscanner.helper

# Stop
launchctl kill SIGTERM gui/$(id -u)/org.openscanner.helper
```

**Send XPC commands (no sudo required):**
```bash
# The daemon is already running, just send XPC messages
# (This would be done programmatically, not via CLI)
```

## 7. Four-Step No-Sudo Test

Once the helper is registered as an SMAppService.daemon, the four operator gate steps can run **without sudo**:

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

## 8. Why User-Scope Claim Is Impossible

### 8.1 IOKit USB Exclusive Open Requires Root

IOKit USB operations on macOS require root privileges for:
1. **Exclusive device access** — `IOUSBHostDevice initWithIOService:` with exclusive options
2. **Device claim** — preventing other processes from accessing the device
3. **Interface open** — claiming USB interfaces

Even with the `com.apple.security.device.usb` entitlement, user-level processes cannot perform exclusive USB device access if another process (the vendor daemon) holds the device.

### 8.2 Vendor Daemon Runs as Root

The vendor daemon (`/Library/Creality/RPCServer`) runs as root via `sudo`. This means:
- It holds exclusive USB device access
- User-level processes cannot compete for the device
- Only a root-level process can claim the device away from the vendor daemon

### 8.3 SMAppService.agent Insufficient

`SMAppService.agent` runs in the user session with user-level privileges. It cannot:
- Perform IOKit USB exclusive open
- Compete with the root-level vendor daemon
- Claim the USB device

Even if the vendor daemon is stopped, the agent still lacks the privileges needed for IOKit USB operations.

### 8.4 Conclusion

**User-scope claim is impossible** for IOKit USB exclusive access. The helper must run as a system-level daemon with root privileges. `SMAppService.daemon` is the correct model, providing:
- Root privileges for IOKit operations
- One-time admin approval (no per-run sudo)
- Proper macOS integration
- Compatibility with non-interactive shells

## 9. Summary

| Question | Answer |
|----------|--------|
| What does the helper need? | IOKit USB exclusive open, XPC service |
| Does it need root? | **Yes** — IOKit USB operations require root privileges |
| Can it run as user LaunchAgent? | **No** — user-level processes cannot perform IOKit USB exclusive open |
| What's the recommended model? | **SMAppService.daemon** with one-time admin approval |
| Does it require per-run sudo? | **No** — once registered, the daemon runs automatically |
| Does it work in non-interactive shells? | **Yes** — no sudo authentication needed |
| What TCC approvals are needed? | **None** — no camera, microphone, input monitoring, or location access |
| What entitlements are needed? | `com.apple.security.device.usb` (for USB device access) |
| What signing is required? | Ad-hoc (development) or Developer ID (production) with notarization |

## 10. Open Questions

1. **Phase D UVC/HID access:** When Phase D adds UVC streaming and HID access, will those operations require additional entitlements or TCC approvals? (Likely yes for camera access — `com.apple.security.device.camera` and TCC camera approval.)

2. **Multiple device support:** Can the helper claim multiple USB devices simultaneously, or does it need one instance per device?

3. **Hot-plug handling:** How should the daemon handle device connect/disconnect events? Should it auto-claim on connect, or wait for an explicit XPC `claim` request?

---

**Wall**: `docs/recon/**`
**Status**: `[inferred]` — static analysis of code and privilege requirements, no device runs performed.
**Related**: #170 (helper skeleton), #97 (privileged helper decision), Phase D (C2: replace the daemon)
