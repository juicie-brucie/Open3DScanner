// WO-B2: the mesh wire-format round-trip (the live-surface streaming bridge).
#include "recon/mesh_wire.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "openscanner/mesh_tools.h"

namespace recon = openscanner::recon;
namespace mesh = openscanner::mesh;

namespace {
int g_checks = 0, g_failures = 0;
void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}
}  // namespace

int main() {
    const mesh::MeshBuffers box = mesh::make_box(2.0, 3.0, 4.0);
    const std::vector<uint8_t> frame = recon::EncodeMeshFrame(box);

    // --- byte layout: magic 'O','M','S','H' then the counts ---------------
    Check(frame.size() >= 16 && frame[0] == 'O' && frame[1] == 'M' && frame[2] == 'S' && frame[3] == 'H',
          "frame starts with the OMSH magic");

    // --- round-trip ------------------------------------------------------
    {
        mesh::MeshBuffers got;
        Check(recon::DecodeMeshFrame(frame.data(), frame.size(), &got), "decode ok");
        Check(got.vertex_count() == box.vertex_count() && got.triangle_count() == box.triangle_count(),
              "round-trip preserves the counts");
        bool verts_ok = got.vertices.size() == box.vertices.size();
        for (size_t i = 0; verts_ok && i < box.vertices.size(); ++i)
            verts_ok = std::abs(got.vertices[i] - box.vertices[i]) < 1e-5;  // float32 precision
        Check(verts_ok, "round-trip preserves the vertices (float32)");
        Check(got.triangles == box.triangles, "round-trip preserves the triangles");
    }

    // --- rejections ------------------------------------------------------
    {
        mesh::MeshBuffers got;
        Check(!recon::DecodeMeshFrame(frame.data(), 10, &got), "a truncated frame is rejected");
        std::vector<uint8_t> bad = frame;
        bad[0] = 'X';
        Check(!recon::DecodeMeshFrame(bad.data(), bad.size(), &got), "a bad magic is rejected");
        Check(!recon::DecodeMeshFrame(nullptr, 0, &got), "null/empty is rejected");
    }

    // --- an empty mesh round-trips to empty ------------------------------
    {
        const std::vector<uint8_t> empty_frame = recon::EncodeMeshFrame(mesh::MeshBuffers{});
        mesh::MeshBuffers got;
        Check(recon::DecodeMeshFrame(empty_frame.data(), empty_frame.size(), &got) &&
                  got.vertex_count() == 0 && got.triangle_count() == 0,
              "an empty mesh round-trips to empty");
    }

    std::printf("[mesh-wire] %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
