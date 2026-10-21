#pragma once
// The shim's frame source and the reply array it produces.
//
// The 11-element array is docs/recon/findings/frame-codec.md §10.3's tuple;
// element 4 is the whole frame (1920x1200x1, the #174 correction), not a
// band. This interface is the seam the live USB source (#169/#171) will plug
// into: the shim serves frames from whatever FrameSource it is given, and
// nothing above this line knows where they came from.

#include <cstdint>
#include <string>
#include <vector>

#include "msgpack.h"

namespace shim {

// One interface of the device, per protocol-spec.md §1.1: the method suffix,
// the TCP port, and the stream type the start/stop array carries.
struct Interface {
    std::string name;      // "IR", "depth"
    std::string suffix;    // "0-2-12", "0-2-10"
    uint16_t port = 0;     // 10013, 10010
    uint32_t stream_type = 0;  // 6 = IR, 7 = depth
};

const Interface& IrInterface();
const Interface& DepthInterface();

// The frame words the reply carries (§10.3). The three buffers are the PAL
// frame's own planes, passed through whole.
struct FramePayload {
    uint64_t e1_us = 0;  // frame timestamp, microseconds
    uint64_t e2 = 0;     // the frame's second time/counter word
    uint32_t e3 = 255;   // the frame's format field
    Bytes data;          // e4: the primary data buffer (the whole frame)
    Bytes data2;         // e6: the second data plane
    Bytes data3;         // e8: the third data plane (6 bytes when present)
    int32_t e10 = -1;    // the IR light type; -1 is the no-data form

    bool has_frame() const { return !data.empty() || !data2.empty() || !data3.empty(); }
};

class FrameSource {
public:
    virtual ~FrameSource() = default;

    // The stream for this interface starts/stops. A source that cannot serve
    // the interface says why.
    virtual bool Start(const Interface& iface, std::string* err) = 0;
    virtual void Stop(const Interface& iface) = 0;

    // Produces the next frame, or returns false for "no frame right now"
    // (the no-data form — not an error). `err` is only set on a real fault.
    virtual bool Next(const Interface& iface, FramePayload* out, std::string* err) = 0;

    virtual const char* Name() const = 0;
};

// The reply's result: the 11-element array. A payload with every buffer
// empty is the no-data form: same array, e10 = -1.
Bytes FrameArrayResult(const FramePayload& frame);

// The no-data form: the same array with all three buffers empty and e10 = -1.
Bytes NoDataFrameArray();

// Parses a captured reply's result back into the frame words. Used by the
// bundle source and by the test; false when the value is not the 11-array.
bool ParseFrameArray(const Value& result, FramePayload* out);

}  // namespace shim
