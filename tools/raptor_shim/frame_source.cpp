#include "frame_source.h"

namespace shim {

const Interface& IrInterface() {
    static const Interface ir{"IR", "0-2-12", 10013, 6};
    return ir;
}

const Interface& DepthInterface() {
    static const Interface depth{"depth", "0-2-10", 10010, 7};
    return depth;
}

Bytes FrameArrayResult(const FramePayload& frame) {
    Packer p;
    p.array(11);
    p.uint(0);                 // e0: always 0
    p.uint(frame.e1_us);       // e1: frame timestamp
    p.uint(frame.e2);          // e2: second time/counter word
    p.uint(frame.e3);          // e3: format
    p.bin(frame.data);         // e4: the primary data buffer
    p.uint(frame.data.size()); // e5
    p.bin(frame.data2);        // e6
    p.uint(frame.data2.size());  // e7
    p.bin(frame.data3);        // e8
    p.uint(frame.data3.size());  // e9
    p.integer(frame.e10);      // e10: IR light type, -1 for no data
    return p.out;
}

Bytes NoDataFrameArray() { return FrameArrayResult(FramePayload{}); }

bool ParseFrameArray(const Value& result, FramePayload* out) {
    if (!result.is_array(11)) return false;
    const auto& e = result.array;
    for (size_t k : {0u, 1u, 2u, 3u, 5u, 7u, 9u}) {
        if (e[k].kind != Value::Kind::kUInt) return false;
    }
    for (size_t k : {4u, 6u, 8u}) {
        if (e[k].kind != Value::Kind::kBin) return false;
    }
    if (e[10].kind != Value::Kind::kInt && e[10].kind != Value::Kind::kUInt) return false;

    FramePayload f;
    f.e1_us = e[1].u;
    f.e2 = e[2].u;
    f.e3 = (uint32_t)e[3].u;
    f.data = e[4].bin;
    f.data2 = e[6].bin;
    f.data3 = e[8].bin;
    f.e10 = e[10].kind == Value::Kind::kInt ? (int32_t)e[10].i : (int32_t)e[10].u;
    if (out) *out = std::move(f);
    return true;
}

}  // namespace shim
