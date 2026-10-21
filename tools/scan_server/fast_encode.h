// fast_encode — the cloud-snapshot encoder for the live tick (row #164, S1).
//
// Why it exists: the shared encoder in engine/include/openscanner/frame_protocol.h
// writes one byte per push_back, so a 20,000-point frame is 240,036
// bounds-checked pushes and 63 % of the tick's CPU with no client (#147 §7).
// This encoder writes the same frame into one sized buffer: the layout is
// frame_protocol::encode_cloud_snapshot's (that header is the wire format;
// this file must not own a second definition of it). The equivalence to that
// encoder is asserted byte for byte by fast_encode_test (ctest) and by the
// perf_budget reference measurement.
//
// Allocation: EncodeCloudSnapshot writes into a caller-owned buffer and
// allocates nothing. CloudSnapshotBuffer keeps one vector per caller and
// reuses its capacity, so the live tick allocates once on its first frame and
// never per frame.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "openscanner/frame_protocol.h"

namespace openscanner {
namespace fast_encode {

constexpr size_t kCloudSnapshotHeaderBytes = 36;  // the 32-byte envelope + the count
constexpr size_t kPointBytes = 12;                // three f32

// The exact byte count of a cloud_snapshot frame.
constexpr size_t CloudSnapshotSize(uint32_t count) {
    return kCloudSnapshotHeaderBytes + static_cast<size_t>(count) * kPointBytes;
}

// The target's byte order. The wire is little-endian; on a little-endian
// target the float payload is a straight memcpy, which is the fast path. On
// any other target the explicit stores below keep the bytes identical.
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
constexpr bool kLittleEndian = (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);
#else
constexpr bool kLittleEndian = false;
#endif

namespace detail {

inline void StoreU16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

inline void StoreU32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

inline void StoreU64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (i * 8));
}

inline void StoreF32(uint8_t* p, float v) {
    uint32_t u;
    std::memcpy(&u, &v, sizeof(u));
    StoreU32(p, u);
}

inline uint64_t BitsOf(double v) {
    uint64_t u;
    std::memcpy(&u, &v, sizeof(u));
    return u;
}

}  // namespace detail

// Write one cloud_snapshot frame into `out`, which must have room for
// CloudSnapshotSize(count) bytes. Allocation-free: `out` is the caller's.
// `xyz` may be null only when count is 0.
inline void EncodeCloudSnapshot(uint32_t seq, double t_capture, double t_server, const float* xyz,
                                uint32_t count, uint8_t* out) {
    uint8_t* p = out;
    detail::StoreU32(p, frame_protocol::MAGIC);
    p += 4;
    detail::StoreU16(p, frame_protocol::VERSION);
    p += 2;
    detail::StoreU16(p, static_cast<uint16_t>(frame_protocol::MessageType::CloudSnapshot));
    p += 2;
    detail::StoreU32(p, seq);
    p += 4;
    detail::StoreU64(p, detail::BitsOf(t_capture));
    p += 8;
    detail::StoreU64(p, detail::BitsOf(t_server));
    p += 8;
    detail::StoreU32(p, static_cast<uint32_t>(4 + static_cast<uint64_t>(count) * kPointBytes));
    p += 4;
    detail::StoreU32(p, count);
    p += 4;
    if (count == 0) return;
    if constexpr (kLittleEndian) {
        // The payload is the input's own bits, and the wire is the target's
        // order here: one copy, no per-float work.
        std::memcpy(p, xyz, static_cast<size_t>(count) * kPointBytes);
    } else {
        for (uint32_t i = 0; i < count; ++i) {
            detail::StoreF32(p, xyz[i * 3 + 0]);
            detail::StoreF32(p + 4, xyz[i * 3 + 1]);
            detail::StoreF32(p + 8, xyz[i * 3 + 2]);
            p += kPointBytes;
        }
    }
}

// Write one frame into `out`, sizing it first. After the first call at a given
// size the vector's capacity covers it, so resize() does not allocate: the
// live tick's per-frame allocation is zero.
inline void EncodeCloudSnapshotInto(uint32_t seq, double t_capture, double t_server, const float* xyz,
                                    uint32_t count, std::vector<uint8_t>* out) {
    out->resize(CloudSnapshotSize(count));
    EncodeCloudSnapshot(seq, t_capture, t_server, xyz, count, out->data());
}

// One reusable output buffer per caller (the live tick uses one per thread).
class CloudSnapshotBuffer {
public:
    // The frame's bytes; valid until the next Encode() on this object.
    const std::vector<uint8_t>& Encode(uint32_t seq, double t_capture, double t_server, const float* xyz,
                                       uint32_t count) {
        EncodeCloudSnapshotInto(seq, t_capture, t_server, xyz, count, &buf_);
        return buf_;
    }

    const std::vector<uint8_t>& bytes() const { return buf_; }
    size_t capacity() const { return buf_.capacity(); }

private:
    std::vector<uint8_t> buf_;
};

}  // namespace fast_encode
}  // namespace openscanner
