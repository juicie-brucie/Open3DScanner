#include "recon/mesh_wire.h"

#include <cstring>

namespace openscanner::recon {
namespace {

// Little-endian bytes read 'O','M','S','H' (0x4F,0x4D,0x53,0x48).
constexpr uint32_t kMagic = 0x48534D4Fu;
constexpr uint32_t kVersion = 1;

void PutU32(std::vector<uint8_t>* out, uint32_t v) {
    out->push_back(static_cast<uint8_t>(v & 0xFF));
    out->push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out->push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out->push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void PutF32(std::vector<uint8_t>* out, float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    PutU32(out, bits);
}

uint32_t GetU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

float GetF32(const uint8_t* p) {
    const uint32_t bits = GetU32(p);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

}  // namespace

std::vector<uint8_t> EncodeMeshFrame(const openscanner::mesh::MeshBuffers& mesh) {
    const uint32_t nv = static_cast<uint32_t>(mesh.vertex_count());
    const uint32_t nt = static_cast<uint32_t>(mesh.triangle_count());
    std::vector<uint8_t> out;
    out.reserve(16 + nv * 12 + nt * 12);
    PutU32(&out, kMagic);
    PutU32(&out, kVersion);
    PutU32(&out, nv);
    PutU32(&out, nt);
    for (double v : mesh.vertices) PutF32(&out, static_cast<float>(v));
    for (uint32_t idx : mesh.triangles) PutU32(&out, idx);
    return out;
}

bool DecodeMeshFrame(const uint8_t* data, std::size_t size, openscanner::mesh::MeshBuffers* out) {
    out->vertices.clear();
    out->triangles.clear();
    if (data == nullptr || size < 16) return false;
    if (GetU32(data) != kMagic || GetU32(data + 4) != kVersion) return false;
    const uint32_t nv = GetU32(data + 8);
    const uint32_t nt = GetU32(data + 12);
    const std::size_t need = 16 + static_cast<std::size_t>(nv) * 12 + static_cast<std::size_t>(nt) * 12;
    if (size < need) return false;

    const uint8_t* p = data + 16;
    out->vertices.resize(static_cast<std::size_t>(nv) * 3);
    for (std::size_t i = 0; i < static_cast<std::size_t>(nv) * 3; ++i) { out->vertices[i] = GetF32(p); p += 4; }
    out->triangles.resize(static_cast<std::size_t>(nt) * 3);
    for (std::size_t i = 0; i < static_cast<std::size_t>(nt) * 3; ++i) {
        const uint32_t idx = GetU32(p);
        p += 4;
        if (idx >= nv) { out->vertices.clear(); out->triangles.clear(); return false; }  // index out of range
        out->triangles[i] = idx;
    }
    return true;
}

}  // namespace openscanner::recon
