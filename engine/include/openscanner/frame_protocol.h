#pragma once

// Clean-room frame protocol for the scan viewer (V1).
// See docs/architecture/scan-viewer.md §4. Today's wire shape becomes V1:
// a versioned, typed envelope around a payload. The C++ and JS codecs in
// this project are the only producers/consumers of this format.

#include <cstdint>
#include <cstring>
#include <vector>

namespace openscanner {
namespace frame_protocol {

constexpr uint32_t MAGIC = 0x4F534346u; // 'OSCF'
constexpr uint16_t VERSION = 1;

enum class MessageType : uint16_t {
    Status = 1,
    Pose = 2,
    CloudSnapshot = 3,
    CloudDelta = 4,
    Markers = 5,
    Mesh = 6,
    Error = 7,
};

struct Envelope {
    uint32_t magic = MAGIC;
    uint16_t version = VERSION;
    MessageType type = MessageType::CloudSnapshot;
    uint32_t seq = 0;
    double t_capture = 0.0;
    double t_server = 0.0;
    uint32_t payload_len = 0;
};

// Encode a cloud_snapshot (positions only) into the V1 envelope.
inline std::vector<uint8_t> encode_cloud_snapshot(uint32_t seq, double t_capture, double t_server,
                                                  const float* xyz, uint32_t count) {
    std::vector<uint8_t> buf;
    buf.reserve(4 + 2 + 2 + 4 + 8 + 8 + 4 + 4 + static_cast<size_t>(count) * 3 * 4);
    auto put_u32 = [&buf](uint32_t v) {
        buf.push_back(v & 0xff); buf.push_back((v >> 8) & 0xff);
        buf.push_back((v >> 16) & 0xff); buf.push_back((v >> 24) & 0xff);
    };
    auto put_u16 = [&buf](uint16_t v) {
        buf.push_back(v & 0xff); buf.push_back((v >> 8) & 0xff);
    };
    auto put_f64 = [&buf](double v) {
        uint64_t u; static_assert(sizeof(u) == sizeof(v));
        std::memcpy(&u, &v, sizeof(u));
        for (int i = 0; i < 8; ++i) buf.push_back((u >> (i * 8)) & 0xff);
    };
    auto put_f32 = [&buf, &put_u32](float v) {
        uint32_t u; std::memcpy(&u, &v, sizeof(u));
        put_u32(u);
    };
    put_u32(MAGIC);
    put_u16(VERSION);
    put_u16(static_cast<uint16_t>(MessageType::CloudSnapshot));
    put_u32(seq);
    put_f64(t_capture);
    put_f64(t_server);
    put_u32(4 + static_cast<uint32_t>(count) * 3 * 4);
    put_u32(count);
    for (uint32_t i = 0; i < count; ++i) {
        put_f32(xyz[i * 3 + 0]);
        put_f32(xyz[i * 3 + 1]);
        put_f32(xyz[i * 3 + 2]);
    }
    return buf;
}

// Decode returns false on bad magic/version/type or truncated payload.
inline bool decode_cloud_snapshot(const uint8_t* data, size_t len, uint32_t* out_seq,
                                  double* out_t_capture, double* out_t_server,
                                  const float** out_xyz, uint32_t* out_count) {
    if (len < 32 + 4) return false;
    auto get_u32 = [&](size_t off) {
        return (uint32_t)data[off] | ((uint32_t)data[off + 1] << 8) |
               ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 3] << 24);
    };
    auto get_u16 = [&](size_t off) { return (uint16_t)(data[off] | (data[off + 1] << 8)); };
    auto get_f64 = [&](size_t off) {
        uint64_t u = 0;
        for (int i = 0; i < 8; ++i) u |= (uint64_t)data[off + i] << (i * 8);
        double v; std::memcpy(&v, &u, sizeof(v)); return v;
    };
    if (get_u32(0) != MAGIC) return false;
    if (get_u16(4) != VERSION) return false;
    if (get_u16(6) != static_cast<uint16_t>(MessageType::CloudSnapshot)) return false;
    *out_seq = get_u32(8);
    *out_t_capture = get_f64(12);
    *out_t_server = get_f64(20);
    uint32_t payload_len = get_u32(28);
    if (payload_len < 4) return false;
    // Frame is 32-byte envelope + payload_len bytes. Check in 64-bit so
    // neither truncation nor uint32 wraparound can pass a hostile frame.
    if (static_cast<uint64_t>(32) + payload_len > len) return false;
    uint32_t count = get_u32(32);
    if (static_cast<uint64_t>(4) + static_cast<uint64_t>(count) * 12ULL != payload_len) return false;
    *out_count = count;
    *out_xyz = reinterpret_cast<const float*>(data + 36);
    return true;
}

} // namespace frame_protocol
} // namespace openscanner
