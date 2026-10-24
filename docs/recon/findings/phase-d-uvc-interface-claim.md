# Phase-D UVC Interface Claim Mechanism — OQ-UVC1

**Row**: OQ-UVC1 (Recon)
**Status**: Settled via REA static analysis
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

## User vs Root vs dext/Entitlement

### User Process
✅ **CAN claim the interfaces**
The SDK uses standard `USBInterfaceOpen`, which works for any user process with appropriate USB permissions. No special privileges required beyond standard USB device access.

### Root Process
✅ **Same mechanism**
Root doesn't change the claim mechanism. The SDK uses the same standard IOKit plugin interface path regardless of privilege level.

### dext/Entitlement
❌ **NOT required**
The standard IOKit plugin interface pattern does not require:
- DriverKit extension (dext)
- Special entitlements
- Exclusive device access

Any process with USB device access can use this mechanism.

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

1. **User-space capture is feasible**: The standard claim mechanism works without exclusive access, so our `raw_capture` tool can claim the same interfaces without conflicting with the SDK.

2. **No driver required**: We don't need to implement a dext or obtain special entitlements to access the UVC interfaces.

3. **Coexistence possible**: Since the SDK doesn't use exclusive access, our capture tool can potentially coexist with the vendor SDK (though simultaneous access may have other implications).

4. **Standard IOKit pattern**: Our implementation should follow the same IOKit plugin interface pattern: `IOCreatePlugInInterfaceForService` → `QueryInterface` → `USBInterfaceOpen`.

## Open Questions

None for this specific question. The claim mechanism is fully settled via static analysis.

**Next actionable**: Implement the claim mechanism in `raw_capture` following the same IOKit plugin interface pattern identified here.

## References

- REA analysis of `libOrbbecSDK.1.7.1.dylib` arm64 thin slice
- Function addresses: `0x7165c`, `0x555638`, `0x55ccf8`, `0x560e04`
- IOKit framework: `/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit`
