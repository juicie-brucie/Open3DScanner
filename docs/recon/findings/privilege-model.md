# macOS Helper Privilege Model — Row #177

*Static analysis of the helper's privilege requirements and recommended deployment model. No device runs, no sudo commands executed.*

## 1. Current State and Operator Design Input (2026-10-21)

The helper (`tools/macos_helper/openscanner-helper`) currently requires `sudo` to run, which blocks non-interactive execution. The test in row #170 succeeded only after the coordinator granted passwordless sudo for the binary path.

**Current operator gate:**
```bash
sudo build/tools/macos_helper/openscanner-helper --once
```

**Problem:** Non-interactive shells (CI, automated testing, agent-driven workflows) cannot authenticate sudo, blocking the helper from running.

**Operator design input:** The app/helper is **unsigned today** (self-signed at best); an Apple Developer account is a future item. This affects SMAppService registration and TCC identity stability, but **does NOT affect the IOKit USB claim itself**. The vendor's own daemon uses a user LaunchAgent with a sudo wrapper — that's their choice, not a macOS requirement.

## 2. What Unsigned/Self-Signed State Actually Forces

### 2.1 What It Forces

1. **SMAppService registration tied to code signature/team:** An unsigned or ad-hoc app cannot rely on `SMAppService.daemon`/`agent` registration. SMAppService requires a valid Developer ID or Apple Development signature with a stable team identifier.

2. **TCC identity instability:** An ad-hoc signature's identity changes with every build, so camera/input-monitoring grants re-prompt or are denied. A stable self-signed identity (using `dev_sign.sh` with a named Keychain identity) is the development-mode mitigation.

3. **Notarization/Gatekeeper for distribution:** This is a packaging problem, not a claim problem. It affects distribution, not the ability to claim USB devices.

### 2.2 What It Does NOT Force

1. **IOKit USB claim does not require root:** The #170 test's `sudo` was the README/test choice. The vendor's own daemon is a user LaunchAgent whose program is `/usr/bin/sudo /Library/Creality/RPCServer` — the vendor's own sudo wrapper, not a macOS requirement.

2. **The claim may work from the user session:** The #170 four-step test should be run as the user (not with sudo) to prove whether user-scope claim actually works.

3. **Root is not required for IOKit USB exclusive open:** With the `com.apple.security.device.usb` entitlement and proper code signing (even ad-hoc), a user-scope agent can perform IOKit USB exclusive open.

## 3. What the Helper Actually Needs

### 3.1 IOKit USB Exclusive Open

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

### 3.2 XPC Service

The helper provides an XPC interface (`org.openscanner.helper`) for IPC:
- `ping` — health check
- `claim` — acquire USB device
- `release` — release USB device
- `status` — query current state

**Privilege requirement:** XPC services can run at any privilege level. The privilege is determined by the launchd configuration, not XPC itself.

### 3.3 No UVC/HID Access (Yet)

The current helper skeleton does **not** perform:
- UVC streaming (no camera access)
- HID access (no input device access)
- Interface open beyond the USB device itself
- Frame reads

Future Phase D work (C2: replace the daemon) will need UVC/HID access, but the current skeleton only needs IOKit USB device claim.

## 4. Vendor Daemon Privilege Model

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

## 5. Privilege Model Options

### 4.1 Tier A: Unsigned/Self-Signed (Current State)

For unsigned or self-signed apps (no Apple Developer account), SMAppService is not available. Two viable options:

#### 4.1.1 Plain User LaunchAgent (Recommended for Tier A)

**Description:** Manually install a LaunchAgent plist in `~/Library/LaunchAgents/` (user domain, not system).

**Privilege level:** User session (no root).

**How it works:** The helper runs as a user-scope LaunchAgent with the `com.apple.security.device.usb` entitlement. No SMAppService registration, no admin approval, no per-run sudo.

**Installation:**
```bash
# Copy plist to user's LaunchAgents directory
cp tools/macos_helper/launchd/org.openscanner.helper-agent.plist.in \
   ~/Library/LaunchAgents/org.openscanner.helper.plist

# Substitute placeholders
sed -i '' "s|@HELPER_PROGRAM@|$(pwd)/build/tools/macos_helper/openscanner-helper|g" \
        ~/Library/LaunchAgents/org.openscanner.helper.plist
sed -i '' "s|@MACH_SERVICE@|org.openscanner.helper|g" \
        ~/Library/LaunchAgents/org.openscanner.helper.plist

# Load the agent (no sudo required)
launchctl load ~/Library/LaunchAgents/org.openscanner.helper.plist
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
- ✅ **Works with unsigned/self-signed apps** — no SMAppService required
- ✅ **No admin approval required** — installs in user domain
- ✅ **No per-run sudo** — agent runs in user session
- ✅ **Compatible with IOKit USB operations** — entitlements are sufficient
- ✅ **Works in non-interactive shells** — no sudo authentication needed
- ✅ **Matches vendor's domain** — user LaunchAgent (without sudo wrapper)

**Cons:**
- Manual installation (no SMAppService automation)
- Requires proper entitlements and code signing (ad-hoc or self-signed)
- Plist must be copied to `~/Library/LaunchAgents/` by the app or installer

**Verdict:** ✅ **Recommended for Tier A (unsigned/self-signed state)**

#### 4.1.2 Embedded XPC Service (Alternative for Tier A)

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

**Verdict:** ✅ **Alternative for Tier A** if the helper should only run when the app is active.

### 4.2 Tier B: Developer ID Signed (Future State)

Once the app is signed with Developer ID and notarized, SMAppService becomes available:

#### 4.2.1 SMAppService.agent (Recommended for Tier B)

**Description:** Register the helper as a user-level LaunchAgent using `SMAppService.agent`.

**Privilege level:** User session (no root).

**Installation:**
```swift
// In the main app (one-time user approval via system dialog)
import ServiceManagement

let agentIdentifier = "org.openscanner.helper"
do {
    try SMAppService.agent(plistName: agentIdentifier).register()
    print("Helper agent registered successfully")
} catch {
    print("Failed to register helper agent: \(error)")
}
```

**Pros:**
- ✅ **Automated registration** — SMAppService handles plist installation
- ✅ **One-time user approval** — system dialog, no admin password
- ✅ **Stable TCC grants** — tied to Developer ID team identifier
- ✅ **No per-run sudo** — agent runs in user session
- ✅ **Compatible with IOKit USB operations** — entitlements are sufficient
- ✅ **Works in non-interactive shells** — no sudo authentication needed

**Cons:**
- Requires Developer ID signing and notarization
- Requires Apple Developer account ($99/year)

**Verdict:** ✅ **Recommended for Tier B (signed state)**

#### 4.2.2 SMAppService.daemon (Not Recommended)

**Description:** Register the helper as a system-level daemon using `SMAppService.daemon`.

**Privilege level:** System daemon (root), but managed by `SMAppService`.

**Verdict:** ⚠️ **Not recommended** — user-scope agent is sufficient for IOKit USB operations. System-level daemon is overkill and requires admin approval.

### 4.3 Current Model: Root LaunchDaemon + sudo (Anti-Pattern)

**Description:** Current implementation — helper is a root LaunchDaemon, invoked with `sudo` for each operation.

**Verdict:** ❌ **Not recommended** — this is the anti-pattern we're replacing. Requires per-run sudo, blocks non-interactive execution, and over-privileges the helper.

## 6. TCC/Entitlement/Dev-Signing Requirements

### 5.1 Code Signing

The helper must be properly code-signed for both tiers:

**Tier A (unsigned/self-signed):**
- Ad-hoc signing: `codesign --force --sign - build/tools/macos_helper/openscanner-helper`
- Stable self-signed identity (recommended): Use `dev_sign.sh` with a named Keychain identity
  ```bash
  tools/macos_helper/dev_sign.sh build/tools/macos_helper/openscanner-helper "OpenScanner Dev"
  ```
  This provides a stable designated requirement, preventing TCC identity churn.

**Tier B (Developer ID signed):**
- Developer ID signing with notarization:
  ```bash
  codesign --force --sign "Developer ID Application: Your Name" \
           --identifier org.openscanner.helper \
           build/tools/macos_helper/openscanner-helper
  xcrun notarytool submit build/tools/macos_helper/openscanner-helper \
           --appleid "your@email.com" --team-id "TEAMID" --password "@keychain:notarization"
  ```

### 5.2 Entitlements

For IOKit USB access, the helper needs the `com.apple.security.device.usb` entitlement in both tiers:

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

**Current state (no UVC/HID access yet):**
The helper does **not** require TCC approvals for:
- ❌ Camera access (no UVC streaming yet)
- ❌ Input monitoring (no HID access yet)
- ❌ Microphone access (not used)
- ❌ Location access (not used)

**Future state (when UVC/HID access is added in Phase D):**
- ✅ **Camera access** — requires TCC approval (`com.apple.security.device.camera` entitlement + user prompt)
- ✅ **Input monitoring** — requires TCC approval (if HID access is needed)

**Tier A TCC identity stability:**
- Ad-hoc signature: Identity changes with every build → TCC grants re-prompt or are denied
- Stable self-signed identity (via `dev_sign.sh`): Identity remains stable → TCC grants persist across rebuilds
- **Mitigation:** Use `dev_sign.sh` with a named Keychain identity for development to avoid TCC churn

**Tier B TCC identity stability:**
- Developer ID signature: Identity is tied to team identifier → TCC grants are stable
- No TCC churn across builds or notarization

**Registration approval:**
- **Tier A:** None — LaunchAgent installs in user domain, no system dialog
- **Tier B:** One-time user approval via `SMAppService.register()` system dialog (no admin password required for agent)

**Per-run approvals needed:**
- ✅ **None** — once registered/installed, the agent runs without user interaction in both tiers

## 7. Recommended Model: Two-Tier Approach

Based on the operator's design input (2026-10-21), the app is currently **unsigned/self-signed** with no Apple Developer account. This requires a two-tier recommendation that separates the current unsigned state from the future signed state.

### 6.1 Tier A: Unsigned/Self-Signed (Current State)

**Recommended model:** Plain user LaunchAgent (manual installation) or embedded XPC service

**Why not SMAppService?** SMAppService requires a valid Developer ID or Apple Development signature with a stable team identifier. Unsigned or ad-hoc apps cannot use SMAppService registration.

**Installation:**
```bash
# Option 1: Plain user LaunchAgent (manual installation)
cp tools/macos_helper/launchd/org.openscanner.helper-agent.plist.in \
   ~/Library/LaunchAgents/org.openscanner.helper.plist
sed -i '' "s|@HELPER_PROGRAM@|$(pwd)/build/tools/macos_helper/openscanner-helper|g" \
        ~/Library/LaunchAgents/org.openscanner.helper.plist
sed -i '' "s|@MACH_SERVICE@|org.openscanner.helper|g" \
        ~/Library/LaunchAgents/org.openscanner.helper.plist
launchctl load ~/Library/LaunchAgents/org.openscanner.helper.plist

# Option 2: Embedded XPC service (no installation required)
# Place helper in app bundle at Contents/XPCServices/org.openscanner.helper.xpc
# The app launches it automatically when needed
```

**Code signing:**
```bash
# Use stable self-signed identity (recommended for Tier A)
tools/macos_helper/dev_sign.sh build/tools/macos_helper/openscanner-helper "OpenScanner Dev"
# This provides a stable designated requirement, preventing TCC identity churn
```

**Pros:**
- ✅ **Works with unsigned/self-signed apps** — no Developer ID required
- ✅ **No admin approval required** — installs in user domain
- ✅ **No per-run sudo** — agent runs in user session
- ✅ **Compatible with IOKit USB operations** — entitlements are sufficient
- ✅ **Works in non-interactive shells** — no sudo authentication needed
- ✅ **No Apple Developer account needed** ($99/year saved)

**Cons:**
- Manual installation (no SMAppService automation)
- TCC identity may churn with ad-hoc signing (mitigated by stable self-signed identity)
- Must be re-installed after app updates (if using LaunchAgent approach)

**Verdict:** ✅ **Recommended for Tier A (current unsigned state)**

### 6.2 Tier B: Developer ID Signed (Future State)

**Recommended model:** SMAppService.agent (user LaunchAgent with automated registration)

**When to migrate:** Once the app is signed with Developer ID and notarized.

**Installation:**
```swift
// In the main app (one-time user approval via system dialog)
import ServiceManagement

let agentIdentifier = "org.openscanner.helper"
do {
    try SMAppService.agent(plistName: agentIdentifier).register()
    print("Helper agent registered successfully")
} catch {
    print("Failed to register helper agent: \(error)")
}
```

**Code signing:**
```bash
codesign --force --sign "Developer ID Application: Your Name" \
         --entitlements tools/macos_helper/helper.entitlements \
         --identifier org.openscanner.helper \
         build/tools/macos_helper/openscanner-helper
xcrun notarytool submit build/tools/macos_helper/openscanner-helper \
         --appleid "your@email.com" --team-id "TEAMID" --password "@keychain:notarization"
```

**Pros:**
- ✅ **Automated registration** — SMAppService handles plist installation
- ✅ **One-time user approval** — system dialog, no admin password
- ✅ **Stable TCC grants** — tied to Developer ID team identifier
- ✅ **No per-run sudo** — agent runs in user session
- ✅ **Seamless updates** — SMAppService handles upgrades automatically

**Cons:**
- Requires Developer ID signing and notarization
- Requires Apple Developer account ($99/year)

**Migration path:** The same code/interface works for both tiers. The only change is the registration method (manual plist copy vs SMAppService) and signing (ad-hoc/stable self-signed vs Developer ID).

**Verdict:** ✅ **Recommended for Tier B (future signed state)**

### 6.3 Why Not the Vendor's Approach?

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

### 6.4 Common Steps for Both Tiers

**Step 1: Build the helper**
```bash
cmake --build build --target openscanner-helper
```

**Step 2: Add entitlements to the helper**
```bash
codesign --force --sign - \
         --entitlements tools/macos_helper/helper.entitlements \
         build/tools/macos_helper/openscanner-helper
```

**Step 3: Stop the vendor daemon (one-time admin step)**
```bash
launchctl bootout gui/$(id -u)/com.creality.RPCServer
# If the process survives: pkill -x RPCServer
```

**Step 4: Run the four-step no-sudo test** (see section 7)

**Runtime usage (both tiers):**
```bash
# Check agent status
launchctl list | grep openscanner

# Start/stop the agent (no sudo required)
launchctl kickstart -kp gui/$(id -u)/org.openscanner.helper
launchctl kill SIGTERM gui/$(id -u)/org.openscanner.helper

# Send XPC commands (no sudo required)
# The agent is already running, just send XPC messages
# (This would be done programmatically, not via CLI)
```

## 8. Four-Step No-Sudo Test

Once the helper is installed (Tier A: manually via `launchctl load`, or Tier B: via `SMAppService.agent`), the four operator gate steps can run **without sudo**:

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

## 9. Why the Vendor's sudo Wrapper Is the Anti-Pattern

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

## 10. Summary

| Question | Answer |
|----------|--------|
| What does the helper need? | IOKit USB exclusive open, XPC service |
| Does it need root? | **No** — user-scope with proper entitlements suffices |
| Can it run as user LaunchAgent? | **Yes** — with `com.apple.security.device.usb` entitlement |
| What's the recommended model? | **Tier A (unsigned):** Plain user LaunchAgent or embedded XPC service; **Tier B (signed):** SMAppService.agent |
| Does it require per-run sudo? | **No** — runs in user session with entitlements |
| Does it work in non-interactive shells? | **Yes** — no sudo authentication needed |
| What TCC approvals are needed? | **None** — no camera, microphone, input monitoring, or location access (yet) |
| What entitlements are needed? | `com.apple.security.device.usb` (for USB device access) |
| What signing is required? | **Tier A:** Ad-hoc or stable self-signed identity; **Tier B:** Developer ID with notarization |
| What's the anti-pattern to avoid? | Vendor's `/usr/bin/sudo` wrapper in LaunchAgent plist |
| Does SMAppService require signing? | **Yes** — requires Developer ID or Apple Development signature; cannot be used with unsigned/ad-hoc apps |
| What's the migration path? | Same code/interface works for both tiers; only registration method and signing change |

## 11. Open Questions

1. **Phase D UVC/HID access:** When Phase D adds UVC streaming and HID access, will those operations require additional entitlements or TCC approvals? (Likely yes for camera access — `com.apple.security.device.camera` and TCC camera approval.)

2. **Multiple device support:** Can the helper claim multiple USB devices simultaneously, or does it need one instance per device?

3. **Hot-plug handling:** How should the daemon handle device connect/disconnect events? Should it auto-claim on connect, or wait for an explicit XPC `claim` request?

---

**Wall**: `docs/recon/**`
**Status**: `[inferred]` — static analysis of code and privilege requirements, no device runs performed.
**Related**: #170 (helper skeleton), #97 (privileged helper decision), Phase D (C2: replace the daemon)
