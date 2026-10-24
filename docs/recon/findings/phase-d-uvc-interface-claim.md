# Phase-D UVC Interface Claim Mechanism — OQ-UVC1

**Row**: OQ-UVC1 (Recon)
**Status**: Static analysis complete; runtime behavior **untested**
**Target**: `libOrbbecSDK.1.7.1.dylib` (arm64 thin slice)
**SHA256**: `5134a211296484473eaa7e4801788943a6ccb6b4a8660465b0a2c042d9dd925b`

## Question

How does the vendor's `libOrbbecSDK 1.7.1` open/claim the scanner's UVC video interfaces? What is the exact IOKit/IOUSBLib call sequence, and can a user or root process reproduce it without a dext/entitlement?

## Claim Sequence

The SDK uses the standard libusb darwin backend. The complete claim path:

```
claimInterface (0x7165c)
  → libusb_claim_interface (0x555638)
    → darwin_capture_claim_interface (0x55ccf8)
      → darwin_claim_interface (0x560e04)
        → IOCreatePlugInInterfaceForService + QueryInterface + USBInterfaceOpen
```

### Function Analysis

#### `darwin_claim_interface` (0x560e04)
- Uses `IOCreatePlugInInterfaceForService` with interface type UUID
- Calls `QueryInterface` to obtain the interface object
- Calls **`USBInterfaceOpen`** (standard open, NOT `USBInterfaceOpenSeize`)
- This is the standard IOKit plugin interface pattern for non-exclusive access

#### `darwin_capture_claim_interface` (0x55ccf8)
- Wrapper that also does NOT use seize mechanisms
- Falls through to standard `darwin_claim_interface`

#### Key Observation
- **No `USBInterfaceOpenSeize` calls found** in the entire claim path
- **No `USBDeviceOpenSeize` calls found**
- Exclusive access error strings exist in the binary (`0x713473`, `0x7134e4`) but have **no cross-references** — they are part of the libusb error table but not used in the actual claim path

## Claim Mechanism

The SDK uses **standard IOKit plugin interface** pattern:

1. **`IOCreatePlugInInterfaceForService`** — creates a plugin interface for the USB device service
2. **`QueryInterface`** — queries for the specific interface type (IOUSBInterface)
3. **`USBInterfaceOpen`** — opens the interface with standard (non-exclusive) access

This is the same mechanism used by standard USB applications and does NOT require exclusive access to the device.

## Empirical Gap

The static chain and negative results are correct, but they do not prove runtime claim behavior. Two critical observations:

1. **Our capture tools use seize, not non-exclusive open**: The `raw_capture` helper (#181) uses `USBInterfaceOpenSeize` and `USBDeviceOpenSeize`. The SDK's static analysis shows it uses non-exclusive `USBInterfaceOpen`. Our code will not succeed at runtime by copying the SDK's path — the seize call is the blocker, not the claim mechanism.

2. **#198 root run still failed**: When `raw_capture` runs as root (which bypasses the device-level claim that fails at user scope), the ioreg interface owners for interfaces 0–5 **never move** from `pid 603, UVCAssistant`. The capture then fails with `interface 1 is not on the device`. Root privilege alone does not displace UVCAssistant's interface-level ownership.

The static analysis tells us **what the SDK does**, but not **why it succeeds where our seize-based tools fail**. That requires a runtime test.

## Decisive Runtime Test

The test is the **classic non-exclusive `USBInterfaceOpen`** as both user and root, with UVCAssistant owning interfaces 0–5.

| Outcome | Meaning |
|---------|---------|
| `USBInterfaceOpen` succeeds (user and root) | This is the SDK's claim path — non-exclusive open coexists with UVCAssistant. The seize call in our tools is the blocker, not the USB subsystem. |
| Fails `kIOReturnExclusiveAccess` | UVCAssistant has exclusive ownership. The vendor's success at runtime depends on another factor (e.g. the RPCServer's root privilege, process ordering, or a different claim path not visible in this SDK slice). |

Until this test runs, the claim path is **hypothesized, not proven**.

## User vs Root vs dext/Entitlement

### User Process
⚠️ **UNTESTED — static analysis only**
The SDK uses standard `USBInterfaceOpen`, which in principle works for any user process with appropriate USB permissions. However, this has not been verified at runtime against a live device with UVCAssistant active. See Decisive Runtime Test above.

### Root Process
⚠️ **UNTESTED — static analysis only**
Root doesn't change the SDK's claim mechanism, but #198 shows root alone does not displace UVCAssistant's interface-level ownership when the tool uses seize calls. The non-exclusive open path at root has not been tested.

### dext/Entitlement
⚠️ **UNTESTED — static analysis only**
The standard IOKit plugin interface pattern does not require DriverKit or entitlements in principle. This has not been verified at runtime.

## Evidence Summary

| Aspect | Finding | Confidence |
|--------|---------|------------|
| Claim sequence | Fully traced through 4 function levels | `[inferred]` from REA |
| Exclusive access | NOT used (no seize calls) | `[inferred]` from REA |
| User process claim | Supported (standard USBInterfaceOpen) | `[inferred]` from REA |
| Root process claim | Same mechanism as user | `[inferred]` from REA |
| dext/entitlement | Not required | `[inferred]` from REA |

## Negative Results

- ❌ No `USBInterfaceOpenSeize` found in claim path
- ❌ No `USBDeviceOpenSeize` found in claim path
- ❌ Exclusive access error strings exist but have no code references (unused error table entries)
- ❌ No custom USB open functions (SDK relies entirely on libusb)
- ❌ No entitlement checks in claim path

## Implications for Phase-D

These implications are **conditional on the runtime test passing**. Until the decisive test runs, these remain hypotheses:

1. **User-space capture may be feasible**: If the non-exclusive open works at runtime, our `raw_capture` tool can claim the same interfaces by switching from `USBInterfaceOpenSeize` to `USBInterfaceOpen`.

2. **No driver may be required**: If the non-exclusive open works, we don't need a dext or special entitlements to access the UVC interfaces.

3. **Coexistence may be possible**: If the SDK's non-exclusive open coexists with UVCAssistant at runtime, our capture tool could potentially do the same.

4. **Standard IOKit pattern is the target**: Our implementation should follow the same IOKit plugin interface pattern: `IOCreatePlugInInterfaceForService` → `QueryInterface` → `USBInterfaceOpen` — **but only after the runtime test confirms this path works**.

## Open Questions

1. **Does the SDK's non-exclusive `USBInterfaceOpen` actually succeed at runtime?** The static analysis shows this is the path, but it has not been verified against a live device with UVCAssistant active. This is the **decisive question** that blocks all downstream implementation.

2. **If it fails, what is the vendor's actual success factor?** If the non-exclusive open fails with `kIOReturnExclusiveAccess`, then the vendor's success depends on another factor (RPCServer's root privilege, process ordering, or a different claim path not visible in this SDK slice).

**No further static analysis can answer these questions.** The runtime test described in "Decisive Runtime Test" is the only way to settle this.

## References

- REA analysis of `libOrbbecSDK.1.7.1.dylib` arm64 thin slice
- Function addresses: `0x7165c`, `0x555638`, `0x55ccf8`, `0x560e04`
- IOKit framework: `/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit`
