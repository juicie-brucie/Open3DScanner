// The HID output-report builder (row #178). See hid_report.h.

#include "hid_report.h"

#include <cmath>
#include <cstring>

namespace raw_capture {
namespace {

const PropertyField* FindField(const ControlSpec& spec, uint32_t property) {
    for (const PropertyField& field : spec.fields) {
        if (field.property == property) return &field;
    }
    return nullptr;
}

bool Encode(double value, FieldEncoding encoding, std::vector<uint8_t>* report, size_t offset,
            std::string* error) {
    switch (encoding) {
        case FieldEncoding::kU8: {
            if (!(value >= 0.0) || value > 255.0 || value != std::floor(value)) {
                *error = "value-out-of-range: the u8 field takes an integer 0..255";
                return false;
            }
            (*report)[offset] = static_cast<uint8_t>(value);
            return true;
        }
        case FieldEncoding::kU16le:
        case FieldEncoding::kU32le: {
            const double max = encoding == FieldEncoding::kU16le ? 65535.0 : 4294967295.0;
            if (!(value >= 0.0) || value > max || value != std::floor(value)) {
                *error = "value-out-of-range: the integer field does not fit";
                return false;
            }
            const uint32_t v = static_cast<uint32_t>(value);
            const int bytes = encoding == FieldEncoding::kU16le ? 2 : 4;
            for (int i = 0; i < bytes; ++i) (*report)[offset + i] = static_cast<uint8_t>((v >> (i * 8)) & 0xff);
            return true;
        }
        case FieldEncoding::kF32le: {
            if (!std::isfinite(value)) {
                *error = "value-out-of-range: a float field needs a finite value";
                return false;
            }
            const float f = static_cast<float>(value);
            std::memcpy(report->data() + offset, &f, sizeof(f));
            return true;
        }
        default:
            *error = "not-configured: the field's encoding is unknown";
            return false;
    }
}

}  // namespace

bool ControlFieldConfigured(const ControlSpec& spec, uint32_t property) {
    if (!ControlConfigured(spec)) return false;
    if (spec.report_id < 0 || spec.report_length <= 0) return false;
    const PropertyField* field = FindField(spec, property);
    if (field == nullptr || !field->configured) return false;
    if (field->offset < 0 || field->size <= 0 || field->encoding == FieldEncoding::kUnknown) return false;
    if (field->offset + field->size > spec.report_length) return false;
    const int expected = field->encoding == FieldEncoding::kU8    ? 1
                         : field->encoding == FieldEncoding::kU16le ? 2
                         : field->encoding == FieldEncoding::kU32le ? 4
                         : field->encoding == FieldEncoding::kF32le ? 4
                                                                    : 0;
    return expected == field->size;
}

bool BuildHidReport(const ControlSpec& spec, uint32_t property, double value, std::vector<uint8_t>* report,
                    std::string* error) {
    error->clear();
    report->clear();
    if (!ControlConfigured(spec)) {
        *error = std::string("not-configured: ") + spec.missing;
        return false;
    }
    if (spec.report_id < 0 || spec.report_length <= 0) {
        *error = "not-configured: the HID report id/length are not in the table";
        return false;
    }
    const PropertyField* field = FindField(spec, property);
    if (field == nullptr || !field->configured) {
        *error = "not-configured: property " + std::to_string(property) + " has no report field in the table";
        return false;
    }
    if (field->offset < 0 || field->size <= 0 || field->offset + field->size > spec.report_length) {
        *error = "not-configured: property " + std::to_string(property) + "'s field does not fit the report";
        return false;
    }
    if (field->encoding == FieldEncoding::kF32le) {
        if (!std::isfinite(value)) {
            *error = "value-out-of-range: a float field needs a finite value";
            return false;
        }
    } else if (!(value >= 0.0)) {
        *error = "value-out-of-range: an integer field takes a non-negative value";
        return false;
    }
    report->assign(static_cast<size_t>(spec.report_length), 0);
    (*report)[0] = static_cast<uint8_t>(spec.report_id);  // HID report id prefix
    return Encode(value, field->encoding, report, static_cast<size_t>(field->offset), error);
}

}  // namespace raw_capture
