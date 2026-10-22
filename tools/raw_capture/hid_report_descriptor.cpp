// The HID report-descriptor parser (row #189). See hid_report_descriptor.h.
//
// Reference: HID 1.11 §6.2.2 (item format), §6.2.2.4-§6.2.2.8 (global/local
// items), §6.2.2.5 (main items). Only the items the question needs are
// interpreted; an item this parser does not know is skipped, not guessed at.

#include "hid_report_descriptor.h"

#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <utility>

namespace raw_capture {
namespace {

constexpr uint8_t kLongItem = 0xFE;

int64_t SignedValue(const uint8_t* data, size_t size) {
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) value |= static_cast<uint64_t>(data[i]) << (i * 8);
    if (size == 0) return 0;
    const int bits = static_cast<int>(size) * 8;
    if (bits < 64 && (value & (1ull << (bits - 1)))) {
        value |= ~((1ull << bits) - 1);
    }
    return static_cast<int64_t>(value);
}

uint32_t UnsignedValue(const uint8_t* data, size_t size) {
    uint32_t value = 0;
    for (size_t i = 0; i < size && i < 4; ++i) value |= static_cast<uint32_t>(data[i]) << (i * 8);
    return value;
}

struct GlobalState {
    uint32_t usage_page = 0;
    int64_t logical_min = 0;
    int64_t logical_max = 0;
    int report_size = 0;
    int report_count = 0;
    int report_id = 0;
};

struct LocalState {
    std::vector<uint32_t> usages;  // usage ids, page applied for 1/2-byte items
    bool have_usage_range = false;
    uint32_t usage_min = 0;
    uint32_t usage_max = 0;
};

void ClearLocal(LocalState* local) {
    local->usages.clear();
    local->have_usage_range = false;
    local->usage_min = 0;
    local->usage_max = 0;
}

void AddReportId(std::vector<int>* ids, int id) {
    if (std::find(ids->begin(), ids->end(), id) == ids->end()) ids->push_back(id);
}

}  // namespace

const char* HidFieldTypeName(HidFieldType type) {
    switch (type) {
        case HidFieldType::kInput: return "input";
        case HidFieldType::kOutput: return "output";
        case HidFieldType::kFeature: return "feature";
    }
    return "unknown";
}

HidReportDescriptor ParseHidReportDescriptor(const uint8_t* data, size_t size) {
    HidReportDescriptor descriptor;
    if (data != nullptr) descriptor.bytes.assign(data, data + size);
    if (data == nullptr || size == 0) {
        descriptor.error = "the report descriptor is empty";
        return descriptor;
    }

    GlobalState global;
    LocalState local;
    std::vector<GlobalState> stack;
    std::map<std::pair<int, int>, int> bits;  // (report id, type) -> bits used
    std::map<std::pair<int, int>, int> report_bits;

    size_t at = 0;
    while (at < size) {
        const uint8_t prefix = data[at];
        if (prefix == kLongItem) {
            if (at + 3 > size) {
                descriptor.error = "truncated long item at byte " + std::to_string(at);
                return descriptor;
            }
            const size_t data_size = data[at + 1];
            if (at + 3 + data_size > size) {
                descriptor.error = "truncated long item data at byte " + std::to_string(at);
                return descriptor;
            }
            at += 3 + data_size;  // a long item is vendor-defined; skip it
            continue;
        }
        const size_t item_size = prefix & 0x03;
        const size_t value_size = item_size == 3 ? 4 : item_size;
        const int type = (prefix >> 2) & 0x03;  // 0 main, 1 global, 2 local
        const int tag = (prefix >> 4) & 0x0F;
        if (at + 1 + value_size > size) {
            descriptor.error = "truncated report item at byte " + std::to_string(at);
            return descriptor;
        }
        const uint8_t* value = data + at + 1;

        if (type == 1) {  // global
            switch (tag) {
                case 0x0: global.usage_page = UnsignedValue(value, value_size); break;
                case 0x1: global.logical_min = SignedValue(value, value_size); break;
                case 0x2: global.logical_max = SignedValue(value, value_size); break;
                case 0x7: global.report_size = static_cast<int>(UnsignedValue(value, value_size)); break;
                case 0x8: global.report_id = static_cast<int>(UnsignedValue(value, value_size)); break;
                case 0x9: global.report_count = static_cast<int>(UnsignedValue(value, value_size)); break;
                case 0xA: stack.push_back(global); break;
                case 0xB:
                    if (!stack.empty()) {
                        global = stack.back();
                        stack.pop_back();
                    }
                    break;
                default: break;  // logical/physical minimum, unit, string index...
            }
        } else if (type == 2) {  // local
            switch (tag) {
                case 0x0: {  // Usage: the id; the page is the global/page in effect
                    const uint32_t usage = UnsignedValue(value, value_size);
                    local.usages.push_back(usage & 0xFFFF);
                    break;
                }
                case 0x1:
                    local.usage_min = UnsignedValue(value, value_size) & 0xFFFF;
                    local.have_usage_range = true;
                    break;
                case 0x2:
                    local.usage_max = UnsignedValue(value, value_size) & 0xFFFF;
                    local.have_usage_range = true;
                    break;
                default: break;  // designator/string/delimiter: not needed
            }
        } else if (type == 0) {  // main
            if (tag == 0x8 || tag == 0x9 || tag == 0xB) {  // Input, Output, Feature
                HidField field;
                field.report_id = global.report_id;
                field.type = tag == 0x8 ? HidFieldType::kInput
                             : tag == 0x9 ? HidFieldType::kOutput
                                          : HidFieldType::kFeature;
                const uint32_t flags = UnsignedValue(value, value_size);
                field.constant = (flags & 0x01) != 0;
                field.variable = (flags & 0x02) != 0;
                field.bit_size = global.report_size;
                field.count = global.report_count;
                field.logical_min = global.logical_min;
                field.logical_max = global.logical_max;
                const int64_t bits_needed = static_cast<int64_t>(global.report_size) * global.report_count;
                if (bits_needed < 0 || bits_needed > (1 << 20)) {
                    descriptor.error = "implausible report size/count at byte " + std::to_string(at);
                    return descriptor;
                }
                const std::pair<int, int> key(global.report_id, static_cast<int>(field.type));
                field.bit_offset = bits[key];
                bits[key] += static_cast<int>(bits_needed);
                report_bits[key] += static_cast<int>(bits_needed);

                field.usage_page = global.usage_page;
                if (!local.usages.empty()) {
                    field.usages = local.usages;
                    if (field.usages.size() > 32) field.usages.resize(32);
                } else if (local.have_usage_range) {
                    uint32_t lo = local.usage_min;
                    uint32_t hi = local.usage_max;
                    if (lo > hi) std::swap(lo, hi);
                    field.usage_range = true;
                    for (uint32_t usage = lo; usage <= hi && field.usages.size() < 32; ++usage) {
                        field.usages.push_back(usage);
                    }
                }
                if (field.type == HidFieldType::kInput) descriptor.has_input = true;
                if (field.type == HidFieldType::kOutput) descriptor.has_output = true;
                if (field.type == HidFieldType::kFeature) descriptor.has_feature = true;
                AddReportId(&descriptor.report_ids, field.report_id);
                descriptor.fields.push_back(field);
            }
            // Every main item consumes the local items (collections too).
            ClearLocal(&local);
            if (tag == 0xA || tag == 0xC) {  // Collection / End Collection
                // No field; nothing else to record.
            }
        }
        at += 1 + value_size;
    }

    for (const auto& entry : report_bits) {
        HidReport report;
        report.report_id = entry.first.first;
        report.type = static_cast<HidFieldType>(entry.first.second);
        report.bits = entry.second;
        report.bytes = (report.bits + 7) / 8;
        descriptor.reports.push_back(report);
    }

    // The output verdict: exactly one output report id can be named; more is
    // ambiguous and is reported as such.
    std::set<int> output_ids;
    for (const HidReport& report : descriptor.reports) {
        if (report.type == HidFieldType::kOutput) output_ids.insert(report.report_id);
    }
    if (output_ids.size() == 1) {
        const int output_id = *output_ids.begin();
        descriptor.output_report_id = output_id;
        for (const HidReport& report : descriptor.reports) {
            if (report.type == HidFieldType::kOutput && report.report_id == output_id) {
                descriptor.output_report_bytes += report.bytes;
            }
        }
    } else if (output_ids.size() > 1) {
        descriptor.output_ambiguous = true;
    }
    if (descriptor.fields.empty() && descriptor.error.empty()) {
        descriptor.error = "the report descriptor declares no input/output/feature fields";
    }
    return descriptor;
}

bool ReadHidReportFile(const std::string& path, std::vector<uint8_t>* bytes, std::string* error) {
    bytes->clear();
    error->clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        *error = "not-configured: cannot open the report descriptor file: " + path;
        return false;
    }
    const std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (raw.empty()) {
        *error = "not-configured: the report descriptor file is empty: " + path;
        return false;
    }
    // The probe prints the descriptor as hex, so a file of that text is
    // accepted too: all characters hex or whitespace means hex text.
    bool hex_text = true;
    bool any_hex = false;
    for (uint8_t byte : raw) {
        if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') continue;
        const bool digit = (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') ||
                           (byte >= 'A' && byte <= 'F');
        if (!digit) {
            hex_text = false;
            break;
        }
        any_hex = true;
    }
    if (hex_text && any_hex) {
        int high = -1;
        for (uint8_t byte : raw) {
            if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') continue;
            int value = -1;
            if (byte >= '0' && byte <= '9') value = byte - '0';
            else if (byte >= 'a' && byte <= 'f') value = byte - 'a' + 10;
            else if (byte >= 'A' && byte <= 'F') value = byte - 'A' + 10;
            if (high < 0) {
                high = value;
            } else {
                bytes->push_back(static_cast<uint8_t>((high << 4) | value));
                high = -1;
            }
        }
        if (high >= 0) {
            bytes->clear();
            *error = "not-configured: the hex report descriptor has an odd digit count: " + path;
            return false;
        }
        if (bytes->empty()) {
            *error = "not-configured: the report descriptor file is empty: " + path;
            return false;
        }
        return true;
    }
    *bytes = raw;
    return true;
}

std::string FormatHidReportDescriptor(const HidReportDescriptor& descriptor, const std::string& fetch_note) {
    std::string text;
    auto append = [&text](const char* format, ...) {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        if (written > 0) {
            text.append(buffer, std::min<size_t>(static_cast<size_t>(written), sizeof(buffer) - 1));
        }
    };
    if (descriptor.bytes.empty()) {
        append("[hid] descriptor: not read (%s)\n",
               fetch_note.empty() ? "no report descriptor bytes" : fetch_note.c_str());
        return text;
    }
    append("[hid] descriptor: %zu bytes:", descriptor.bytes.size());
    for (uint8_t byte : descriptor.bytes) append(" %02X", byte);
    append("\n");
    if (!descriptor.ok()) {
        append("[hid] parse error: %s\n", descriptor.error.c_str());
        return text;
    }
    append("[hid] report ids:");
    if (descriptor.report_ids.empty()) {
        append(" none (unnumbered reports only)\n");
    } else {
        for (size_t i = 0; i < descriptor.report_ids.size(); ++i) {
            append("%s%d", i ? ", " : " ", descriptor.report_ids[i]);
        }
        append("\n");
    }
    for (const HidField& field : descriptor.fields) {
        append("[hid] field report_id=%d type=%s usage_page=0x%04X usages=", field.report_id,
               HidFieldTypeName(field.type), field.usage_page);
        if (field.usages.empty()) {
            append("none");
        } else {
            for (size_t i = 0; i < field.usages.size(); ++i) {
                append("%s0x%04X", i ? "," : "", field.usages[i] & 0xFFFF);
            }
            if (field.usage_range) append("..(range)");
        }
        append(" bit_offset=%d bit_size=%d count=%d logical=%lld..%lld variable=%s constant=%s\n",
               field.bit_offset, field.bit_size, field.count, static_cast<long long>(field.logical_min),
               static_cast<long long>(field.logical_max), field.variable ? "yes" : "no",
               field.constant ? "yes" : "no");
    }
    for (const HidReport& report : descriptor.reports) {
        append("[hid] report id=%d type=%s %d bit(s) = %d byte(s)\n", report.report_id,
               HidFieldTypeName(report.type), report.bits, report.bytes);
    }
    if (descriptor.output_ambiguous) {
        append("[hid] output: more than one output report id — the control report is ambiguous; not filled\n");
    } else if (descriptor.has_output && descriptor.output_report_id != kUnsetValue) {
        const int total = descriptor.output_report_bytes +
                          (descriptor.output_report_id != 0 ? 1 : 0);  // the id byte is part of the report
        append("[hid] output: report id=%d, %d data byte(s), %d byte(s) including the id — paste "
               "`control.report_id=%d control.report_length=%d`\n",
               descriptor.output_report_id, descriptor.output_report_bytes, total,
               descriptor.output_report_id, total);
        append("[hid] the property->field map still needs a usage->#101-property mapping; the descriptor "
               "alone does not name the properties\n");
    } else {
        append("[hid] output: none — every field is input/feature; this HID interface cannot carry the "
               "laser/exposure control, which stays the vendor RPC properties (#101)\n");
    }
    return text;
}

void PrintHidReportDescriptor(const HidReportDescriptor& descriptor, const std::string& fetch_note,
                              std::FILE* out) {
    const std::string text = FormatHidReportDescriptor(descriptor, fetch_note);
    std::fwrite(text.data(), 1, text.size(), out);
    std::fflush(out);
}

}  // namespace raw_capture
