// Row #178: build the HID output report for #101's laser/exposure properties.
//
// The report layout is captured, not inferred: descriptor_table.h's
// ControlSpec carries a report id, a length and a per-property byte field,
// all unconfigured until the operator's HID report descriptor capture fills
// them. This builder refuses a property with no configured field, and
// refuses a value that does not fit the field — the transport never guesses
// a byte offset.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "descriptor_table.h"

namespace raw_capture {

// True when the control endpoint and this property's report field are
// configured (the field's report id, offset, size and encoding are set, and
// the field fits the report).
bool ControlFieldConfigured(const ControlSpec& spec, uint32_t property);

// Builds the report for one property. On failure returns false with a name:
//   not-configured: ... (endpoint/report/field unset)
//   value-out-of-range: ...
bool BuildHidReport(const ControlSpec& spec, uint32_t property, double value, std::vector<uint8_t>* report,
                    std::string* error);

}  // namespace raw_capture
