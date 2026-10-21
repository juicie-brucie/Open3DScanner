#include "properties.h"

#include <cstring>

namespace shim {
namespace {

constexpr uint16_t kRequestMagic = 0x4d47;   // wire 47 4d
constexpr uint16_t kResponseMagic = 0x4252;  // wire 52 42
constexpr uint16_t kOpGetProperty = 1;
constexpr uint16_t kOpSetProperty = 2;
// The recorded ack header's size field (protocol-spec §2.1's example). The
// client does not read it; it is emitted here as the daemon emitted it.
constexpr uint16_t kRecordedSizeHalfWords = 0x0053;

uint16_t Le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t Le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void PutLe16(Bytes* out, uint16_t v) {
    out->push_back((uint8_t)v);
    out->push_back((uint8_t)(v >> 8));
}
void PutLe32(Bytes* out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out->push_back((uint8_t)(v >> (i * 8)));
}
void PutI32(Bytes* out, int32_t v) { PutLe32(out, (uint32_t)v); }

}  // namespace

PropertyBank::PropertyBank() {
    // The ids and ranges the capture observed (protocol-spec.md §3, #101 §4).
    auto put = [this](uint32_t id, int32_t cur, int32_t max, int32_t min, int32_t def, int32_t step) {
        data_[id] = PropertyData{cur, max, min, def, step};
    };
    put(3, 0, 1, 0, 0, 1);       // laser on/off
    put(152, 0, 1, 0, 0, 1);     // laser group
    put(153, 0, 1, 0, 0, 1);     // laser mode
    put(154, 1400000, 1500000, 100000, 810000, 5900);  // exposure
    put(155, 1400000, 1500000, 100000, 810000, 5900);  // current
    put(5, 110000, 1500000, 100000, 810000, 5900);
    put(240, 800000, 1500000, 500000, 0, 0);
    put(85, 0, 0, 0, 0, 0);
    put(89, 0, 0, 0, 0, 0);
    put(151, 0, 1, 0, 0, 1);
    put(157, 0, 1, 0, 0, 1);
    put(158, 0, 1, 0, 0, 1);
    put(160, 100, 100, 0, 100, 1);
    put(161, 50, 100, 0, 50, 1);
    put(164, 0, 5, 0, 0, 1);
    put(181, 1792014971, -1, 0, 0, 0);
    put(183, 0, 1, 0, 0, 1);
    put(186, 0, 1, 0, 0, 1);
    put(187, 0, 2, 0, 0, 1);
    put(192, 0, 8, 0, 0, 1);
    put(2020, 0, 1, 0, 0, 1);
    put(2022, 8, 15, 1, 5, 1);
    put(2024, 2, 4, 1, 2, 1);
    put(2026, 2403, 3000, 100, 3000, 10);
}

PropertyData PropertyBank::Get(uint32_t id) const {
    const auto it = data_.find(id);
    return it == data_.end() ? PropertyData{} : it->second;
}

void PropertyBank::Set(uint32_t id, int32_t value) {
    PropertyData& data = data_[id];
    data.cur = value;
}

bool ParseVendorRequest(const Bytes& bytes, VendorRequest* out, std::string* err) {
    if (bytes.size() < 8) {
        if (err) *err = "vendor frame shorter than its 8-byte header";
        return false;
    }
    if (Le16(bytes.data()) != kRequestMagic) {
        if (err) *err = "vendor frame magic is not 0x4d47 (\"MG\")";
        return false;
    }
    out->declared_size = Le16(bytes.data() + 2);
    out->opcode = Le16(bytes.data() + 4);
    out->request_id = Le16(bytes.data() + 6);
    out->payload.assign(bytes.begin() + 8, bytes.end());
    return true;
}

VendorResponse HandleVendorRequest(const VendorRequest& request, PropertyBank* bank) {
    VendorResponse response;
    response.opcode = request.opcode;
    response.request_id = request.request_id;
    if (request.opcode == kOpGetProperty) {
        if (request.payload.size() < 4) {
            response.error = 1;
            return response;
        }
        const uint32_t id = Le32(request.payload.data());
        const PropertyData data = bank->Get(id);
        PutI32(&response.payload, data.cur);
        PutI32(&response.payload, data.max);
        PutI32(&response.payload, data.min);
        PutI32(&response.payload, data.def);
        PutI32(&response.payload, data.step);
        return response;
    }
    if (request.opcode == kOpSetProperty) {
        if (request.payload.size() < 8) {
            response.error = 1;
            return response;
        }
        const uint32_t id = Le32(request.payload.data());
        const int32_t value = (int32_t)Le32(request.payload.data() + 4);
        bank->Set(id, value);
        return response;
    }
    // Anything else (the flash read, the structure reads): refused, not guessed.
    response.error = 1;
    return response;
}

Bytes BuildVendorResponse(const VendorResponse& response) {
    Bytes out;
    PutLe16(&out, kResponseMagic);
    PutLe16(&out, kRecordedSizeHalfWords);
    PutLe16(&out, response.opcode);
    PutLe16(&out, response.request_id);
    PutLe16(&out, response.error);
    out.insert(out.end(), response.payload.begin(), response.payload.end());
    return out;
}

}  // namespace shim
