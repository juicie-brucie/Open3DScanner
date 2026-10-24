// Row #197: choose the UVC VideoStreaming alternate setting.
//
// A UVC VS interface's streaming endpoint is normally on a non-zero alternate
// setting (alt 0 often has no endpoint); the #178 table can only name one alt,
// and the capture must pick the one that actually carries the table's
// endpoint. The pure choice here is what the macOS handle uses before
// SET_INTERFACE, and what the test pins: prefer the table's alt when it has
// the endpoint, otherwise the alt that does, and never send SET_INTERFACE when
// the interface is already at the chosen alt (the device refused a redundant
// alt-0 selection on 2026-10-21, row #197).
#pragma once

#include <string>
#include <vector>

#include "descriptor_probe.h"

namespace raw_capture {

struct AlternateSelection {
    int alternate_setting = kUnset;  // the alt to use; kUnset on error
    bool needs_select = false;       // false when the interface is already there
    bool current_carries_endpoint = false;  // the current alt can serve the endpoint
    std::string note;                // why this alt, or what was overridden
    std::string error;               // named failure; empty = ok

    bool ok() const { return error.empty() && alternate_setting != kUnset; }
};

// `interfaces` is the parsed configuration (one entry per interface
// descriptor, i.e. per alternate setting). Picks the alt carrying
// `endpoint_address`, preferring `preferred_alternate` when it also carries it.
AlternateSelection ChooseStreamingAlternate(const std::vector<ProbeInterface>& interfaces,
                                            int interface_number, int endpoint_address,
                                            int current_alternate, int preferred_alternate);

// Row #198: choose the IOUSBHostInterface node for one interface number.
//
// FindInterfaceService built a vendor/product/bInterfaceNumber matching
// dictionary and asked IOServiceGetMatchingService for the node; on the
// scanner that returned 0, fifteen retries running, even though the bare
// IOUSBHostInterface enumeration (--diag-interfaces) saw all seven interfaces
// with those exact property values (record #498). So the caller enumerates the
// nodes itself and matches here, on the integer properties it read from each
// one. This is the pure half — no IOKit types — so it is device-free testable.
struct InterfaceCandidate {
    long vendor_id = -1;         // kUSBVendorID; -1 when the property was absent
    long product_id = -1;        // kUSBProductID
    long interface_number = -1;  // bInterfaceNumber
};

// The index of the first candidate whose vendor, product and interface number
// all equal the wanted values, or -1 when none matches. An absent property is
// -1, so it never matches a real wanted id.
int SelectInterfaceCandidate(const std::vector<InterfaceCandidate>& candidates, long want_vendor,
                             long want_product, long want_interface);

}  // namespace raw_capture
