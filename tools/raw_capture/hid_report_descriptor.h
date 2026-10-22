// Row #189: the HID report descriptor, parsed.
//
// The probe reads the HID interface's report descriptor with a GET_DESCRIPTOR
// report request (HID 1.11 §7.1.1) — read-only, no writes, no seize — and this
// module turns the bytes into the item structure the question needs: the report
// ids, each field's usage/bit offset/size, and whether the interface has any
// **output** report at all. A descriptor with no output report is the answer
// that the laser/exposure control cannot ride this HID interface.
//
// Pure: bytes in, structure out; the macOS read is iokit_probe.mm and a saved
// descriptor can be parsed with `--hid-report <file>`.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace raw_capture {

constexpr int kUnsetValue = -1;

enum class HidFieldType { kInput, kOutput, kFeature };

const char* HidFieldTypeName(HidFieldType type);

struct HidField {
    int report_id = 0;          // 0 = unnumbered
    HidFieldType type = HidFieldType::kInput;
    int bit_offset = 0;         // within this (report id, type)
    int bit_size = 0;           // per element
    int count = 0;              // elements
    uint32_t usage_page = 0;    // the page in effect
    std::vector<uint32_t> usages;  // resolved usage ids (a bounded expansion)
    bool usage_range = false;   // the field declared Usage Minimum/Maximum
    int64_t logical_min = 0;
    int64_t logical_max = 0;
    bool constant = false;      // Input/Output/Feature flag bit 0
    bool variable = false;      // flag bit 1
};

struct HidReport {
    int report_id = 0;
    HidFieldType type = HidFieldType::kInput;
    int bits = 0;
    int bytes = 0;              // ceil(bits / 8)
};

struct HidReportDescriptor {
    std::vector<uint8_t> bytes;
    std::vector<HidField> fields;
    std::vector<HidReport> reports;
    std::vector<int> report_ids;   // in order of first appearance
    bool has_input = false;
    bool has_output = false;
    bool has_feature = false;
    int output_report_id = kUnsetValue;  // set when exactly one output report exists
    int output_report_bytes = 0;
    bool output_ambiguous = false;       // more than one output report id
    std::string error;                   // parse error; empty = ok

    bool ok() const { return error.empty(); }
};

HidReportDescriptor ParseHidReportDescriptor(const uint8_t* data, size_t size);

// Reads a saved report descriptor (e.g. from a bus capture). A missing or
// empty file is a named error.
bool ReadHidReportFile(const std::string& path, std::vector<uint8_t>* bytes, std::string* error);

// The human section: the raw hex, the fields, and the output-report verdict.
// `fetch_note` names why there are no bytes when the read was refused.
std::string FormatHidReportDescriptor(const HidReportDescriptor& descriptor, const std::string& fetch_note);
void PrintHidReportDescriptor(const HidReportDescriptor& descriptor, const std::string& fetch_note,
                              std::FILE* out);

}  // namespace raw_capture
