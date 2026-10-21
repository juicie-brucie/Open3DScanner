#pragma once
// The property bank and the vendor-command framing (protocol-spec.md §2, §3;
// #101's map). The shim answers GET/SET_PROPERTY so a client's laser/exposure
// loop runs exactly as it does against the daemon, and stores a SET so a GET
// afterwards reads it back.
//
// An opcode the spec does not describe is refused with a non-zero errorCode;
// it is never guessed.

#include <cstdint>
#include <map>
#include <string>

#include "msgpack.h"

namespace shim {

// PropertyData {i32 cur, max, min, def, step} (protocol-spec §2.2, opcode 1).
struct PropertyData {
    int32_t cur = 0;
    int32_t max = 0;
    int32_t min = 0;
    int32_t def = 0;
    int32_t step = 0;
};

class PropertyBank {
public:
    PropertyBank();
    PropertyData Get(uint32_t id) const;
    void Set(uint32_t id, int32_t value);

private:
    std::map<uint32_t, PropertyData> data_;
};

// The vendor frame inside ob_rpc_send_data (§2.1). Little-endian.
struct VendorRequest {
    uint16_t declared_size = 0;  // as the client wrote it
    uint16_t opcode = 0;
    uint16_t request_id = 0;
    Bytes payload;
};

bool ParseVendorRequest(const Bytes& bytes, VendorRequest* out, std::string* err);

struct VendorResponse {
    uint16_t opcode = 0;
    uint16_t request_id = 0;
    uint16_t error = 0;
    Bytes payload;
};

// GET_PROPERTY (1) and SET_PROPERTY (2) are served; every other opcode gets
// errorCode 1 and no payload.
VendorResponse HandleVendorRequest(const VendorRequest& request, PropertyBank* bank);

Bytes BuildVendorResponse(const VendorResponse& response);

}  // namespace shim
