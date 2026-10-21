// fast_encode_test — the device-free equivalence test for row #164 (S1).
//
// The live tick's encoder moved to fast_encode.h (one sized buffer, no
// per-float push_back). The wire format is owned by
// engine/include/openscanner/frame_protocol.h, so this test asserts the fast
// path is byte-identical to frame_protocol::encode_cloud_snapshot and that it
// reuses its buffer (no per-frame allocation).
//
// No device, no network, no vendor SDK.

#include "fast_encode.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "openscanner/frame_protocol.h"

namespace fe = openscanner::fast_encode;
namespace fp = openscanner::frame_protocol;

namespace {

int failures = 0;

void Check(const std::string& name, bool pass) {
    std::printf("[fast-encode-test] %-64s %s\n", name.c_str(), pass ? "PASS" : "FAIL");
    if (!pass) ++failures;
}

// A deterministic cloud: finite values over a scanner-sized box.
std::vector<float> Pattern(uint32_t count, uint32_t seed) {
    std::vector<float> xyz(static_cast<size_t>(count) * 3);
    for (uint32_t i = 0; i < count; ++i) {
        xyz[i * 3 + 0] = static_cast<float>((i * 37 + seed) % 1000) * 0.001f - 0.5f;
        xyz[i * 3 + 1] = static_cast<float>((i * 53 + seed) % 700) * 0.002f - 0.7f;
        xyz[i * 3 + 2] = static_cast<float>((i * 71 + seed) % 900) * 0.0005f - 0.225f;
    }
    return xyz;
}

// Bit patterns that a float path can quietly canonicalise: signed zero,
// denormals, infinities and a quiet NaN. The wire carries the input's bits.
std::vector<float> SpecialFloats() {
    const uint32_t bits[] = {
        0x00000000u,  // +0
        0x80000000u,  // -0
        0x3f800000u,  // 1
        0xbf800000u,  // -1
        0x00000001u,  // smallest denormal
        0x007fffffu,  // largest denormal
        0x00800000u,  // smallest normal
        0x7f7fffffu,  // FLT_MAX
        0xff7fffffu,  // -FLT_MAX
        0x7f800000u,  // +inf
        0xff800000u,  // -inf
        0x7fc00000u,  // quiet NaN
    };
    std::vector<float> xyz;
    for (uint32_t b : bits) {
        float f;
        std::memcpy(&f, &b, sizeof(f));
        xyz.push_back(f);
    }
    while (xyz.size() % 3 != 0) xyz.push_back(0.0f);
    return xyz;
}

// The fast frame against the shared header's encoder. Returns "" on equality,
// otherwise the first difference.
std::string Compare(uint32_t seq, double t_capture, double t_server, const std::vector<float>& xyz,
                    uint32_t count) {
    const float* src = count == 0 ? nullptr : xyz.data();
    const std::vector<uint8_t> ref = fp::encode_cloud_snapshot(seq, t_capture, t_server, src, count);
    std::vector<uint8_t> fast(fe::CloudSnapshotSize(count));
    fe::EncodeCloudSnapshot(seq, t_capture, t_server, src, count, fast.data());
    if (fast.size() != ref.size()) {
        return "size " + std::to_string(fast.size()) + " != " + std::to_string(ref.size());
    }
    for (size_t i = 0; i < fast.size(); ++i) {
        if (fast[i] != ref[i]) {
            return "byte " + std::to_string(i) + ": " + std::to_string(fast[i]) + " != " +
                   std::to_string(ref[i]);
        }
    }
    return "";
}

}  // namespace

int main() {
    // The layout's exact sizes (36-byte header + 12 bytes a point).
    Check("the frame size is 36 + 12 * count",
          fe::CloudSnapshotSize(0) == 36 && fe::CloudSnapshotSize(1) == 48 &&
              fe::CloudSnapshotSize(2304) == 27684 && fe::CloudSnapshotSize(20000) == 240036);

    // Byte-for-byte equivalence across the counts the product uses, with
    // timestamps that carry fractional bits.
    const uint32_t counts[] = {0, 1, 2, 3, 7, 48 * 48, 20000};
    const double caps[] = {0.0, 1729453612345.678, -12.5, 1e-9};
    bool all_equal = true;
    for (uint32_t count : counts) {
        const std::vector<float> xyz = Pattern(count, count);
        for (double cap : caps) {
            const std::string diff = Compare(1234567u + count, cap, cap + 0.25, xyz, count);
            if (!diff.empty()) {
                all_equal = false;
                Check("count " + std::to_string(count) + " t=" + std::to_string(cap) + ": " + diff, false);
            }
        }
    }
    Check("every count and timestamp is byte-identical to the header encoder", all_equal);

    // The bit patterns a float path can quiet.
    {
        const std::vector<float> special = SpecialFloats();
        const uint32_t count = static_cast<uint32_t>(special.size() / 3);
        const std::string diff = Compare(7, 0.5, 0.75, special, count);
        Check("signed zero, denormals, infinities and NaN keep their bits" + (diff.empty() ? "" : ": " + diff),
              diff.empty());
    }

    // The decoder reads the fast frame back, fields exact.
    {
        const uint32_t count = 2304;
        const std::vector<float> xyz = Pattern(count, 11);
        const double cap = 1729453612345.678, srv = cap + 0.125;
        openscanner::fast_encode::CloudSnapshotBuffer buffer;
        const std::vector<uint8_t>& frame = buffer.Encode(424242, cap, srv, xyz.data(), count);

        uint32_t seq = 0, out_count = 0;
        double out_cap = 0.0, out_srv = 0.0;
        const float* out_xyz = nullptr;
        const bool ok = fp::decode_cloud_snapshot(frame.data(), frame.size(), &seq, &out_cap, &out_srv,
                                                  &out_xyz, &out_count);
        Check("the decoder accepts the fast frame",
              ok && seq == 424242 && out_count == count);
        Check("t_capture and t_server are bit-exact",
              std::memcmp(&out_cap, &cap, sizeof(cap)) == 0 && std::memcmp(&out_srv, &srv, sizeof(srv)) == 0);
        Check("the decoded points are the input's bytes",
              out_xyz != nullptr &&
                  std::memcmp(out_xyz, xyz.data(), static_cast<size_t>(count) * 3 * sizeof(float)) == 0);
    }

    // The reusable buffer: after the first frame it neither reallocates nor
    // moves, including when a smaller frame follows a larger one.
    {
        const std::vector<float> big = Pattern(20000, 3);
        const std::vector<float> small = Pattern(2304, 4);
        fe::CloudSnapshotBuffer buffer;
        const std::vector<uint8_t>& first = buffer.Encode(1, 1.0, 2.0, big.data(), 20000);
        const uint8_t* data = first.data();
        const size_t capacity = buffer.capacity();
        const std::vector<uint8_t>& second = buffer.Encode(2, 1.0, 2.0, big.data(), 20000);
        Check("the same-size frame reuses the buffer", second.data() == data && buffer.capacity() == capacity);
        Check("the same-size frame keeps its length", second.size() == 240036);
        const std::vector<uint8_t>& third = buffer.Encode(3, 1.0, 2.0, small.data(), 2304);
        Check("a smaller frame reuses the same buffer", third.data() == data && buffer.capacity() == capacity);
        Check("a smaller frame has its own length", third.size() == 27684);
    }

    // The encoder writes exactly its frame and not one byte more.
    {
        const uint32_t count = 1000;
        const std::vector<float> xyz = Pattern(count, 5);
        const size_t size = fe::CloudSnapshotSize(count);
        std::vector<uint8_t> guarded(size + 32, 0xAA);
        fe::EncodeCloudSnapshot(9, 0.5, 0.5, xyz.data(), count, guarded.data() + 16);
        bool guards = true;
        for (size_t i = 0; i < 16; ++i) guards = guards && guarded[i] == 0xAA && guarded[16 + size + i] == 0xAA;
        Check("the encoder writes only within its frame", guards);
        const std::vector<uint8_t> ref = fp::encode_cloud_snapshot(9, 0.5, 0.5, xyz.data(), count);
        Check("the guarded frame is still byte-identical",
              std::memcmp(guarded.data() + 16, ref.data(), size) == 0);
    }

    std::printf("[fast-encode-test] %s (%d failure(s))\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}
