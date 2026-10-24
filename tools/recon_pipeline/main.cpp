// WO-B2's end-to-end tool: a recorded IR session + calibration -> our fused
// mesh, A/B'd against the vendor's exported mesh. The math is all in the engine
// (laser_line -> triangulate -> fuse -> reconstruct -> mesh_compare); this tool
// is the RCRP front end, the calibration/mesh I/O, and the glue.
//
//   --selftest                         synthetic sphere end-to-end, no files
//   --session <bundle> --calib <file>  recorded IR frames -> our mesh (PLY)
//       [--out mesh.ply] [--ref vendor.ply] [--live]
//
// GATED, by design (named, not failures):
//   * calibration (intrinsics + the 29 planes) is recon #204/#508; pass a
//     synthetic/placeholder --calib until it lands.
//   * the per-frame pose and the multi-line plane association are #206; absent
//     them this tool uses identity poses and plane 0, so a real multi-frame
//     scan will not fuse correctly yet — the pipeline wiring is what is proven
//     here, on synthetic data via --selftest.
//   * the quantitative vendor A/B needs a paired recording (the #207 HW card);
//     --ref runs CompareMeshes on any two meshes today.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "capture.h"
#include "recon/laser_line.h"
#include "recon/mesh_compare.h"
#include "recon/recon_pipeline.h"
#include "recon/triangulate.h"

namespace recon = openscanner::recon;
namespace mesh = openscanner::mesh;

namespace {

constexpr int kWidth = 1920, kHeight = 1200, kStride = 1920;
constexpr uint32_t kFrameBytes = 2304000;

// The get_frame reply's Bin(2304000) at element 4 (same as tools/laser_line).
const uint8_t* FindFrameBin(const std::vector<uint8_t>& reply, uint32_t* len) {
    for (size_t i = 0; i + 5 <= reply.size(); ++i) {
        if (reply[i] != 0xC5) continue;
        const uint32_t n = (static_cast<uint32_t>(reply[i + 1]) << 8) | reply[i + 2];
        if (n == kFrameBytes && i + 3 + n <= reply.size()) { *len = n; return reply.data() + i + 3; }
    }
    return nullptr;
}

bool ReadCalibration(const std::string& path, recon::Calibration* cal, std::string* err) {
    std::ifstream in(path);
    if (!in) { *err = path + ": cannot open"; return false; }
    std::string line;
    bool have_camera = false;
    while (std::getline(in, line)) {
        const auto h = line.find('#');
        if (h != std::string::npos) line.erase(h);
        std::istringstream ls(line);
        std::string tok;
        if (!(ls >> tok)) continue;
        if (tok == "camera") {
            if (!(ls >> cal->camera.fx >> cal->camera.fy >> cal->camera.cx >> cal->camera.cy)) {
                *err = path + ": bad camera line"; return false;
            }
            have_camera = true;
        } else if (tok == "plane") {
            recon::LaserPlane p;
            if (!(ls >> p.a >> p.b >> p.c >> p.d)) { *err = path + ": bad plane line"; return false; }
            cal->planes.push_back(p);
        } else {
            *err = path + ": unknown directive '" + tok + "'"; return false;
        }
    }
    if (!have_camera || cal->planes.empty()) { *err = path + ": need a camera line and >= 1 plane"; return false; }
    return true;
}

bool WritePlyMesh(const std::string& path, const mesh::MeshBuffers& m) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "ply\nformat ascii 1.0\nelement vertex " << m.vertex_count()
        << "\nproperty float x\nproperty float y\nproperty float z\nelement face " << m.triangle_count()
        << "\nproperty list uchar int vertex_indices\nend_header\n";
    for (size_t i = 0; i < m.vertex_count(); ++i)
        out << m.vertices[3 * i] << ' ' << m.vertices[3 * i + 1] << ' ' << m.vertices[3 * i + 2] << '\n';
    for (size_t i = 0; i < m.triangle_count(); ++i)
        out << "3 " << m.triangles[3 * i] << ' ' << m.triangles[3 * i + 1] << ' ' << m.triangles[3 * i + 2]
            << '\n';
    return static_cast<bool>(out);
}

// A minimal ASCII-PLY mesh reader (x/y/z vertices + triangle faces).
bool ReadPlyMesh(const std::string& path, mesh::MeshBuffers* m, std::string* err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { *err = path + ": cannot open"; return false; }
    std::string line;
    std::getline(in, line);
    if (line.rfind("ply", 0) != 0) { *err = path + ": not a PLY"; return false; }
    size_t nv = 0, nf = 0;
    bool ascii = true, in_vertex = false;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string tok;
        ls >> tok;
        if (tok == "format") { std::string f; ls >> f; ascii = (f == "ascii"); }
        else if (tok == "element") { std::string k; size_t c; ls >> k >> c; if (k == "vertex") { nv = c; in_vertex = true; } else if (k == "face") { nf = c; in_vertex = false; } }
        else if (tok == "end_header") break;
    }
    if (!ascii) { *err = path + ": only ascii PLY supported"; return false; }
    m->vertices.resize(nv * 3);
    for (size_t i = 0; i < nv; ++i) { std::getline(in, line); std::istringstream ls(line); ls >> m->vertices[3 * i] >> m->vertices[3 * i + 1] >> m->vertices[3 * i + 2]; }
    m->triangles.clear();
    for (size_t i = 0; i < nf; ++i) {
        std::getline(in, line); std::istringstream ls(line);
        int cnt = 0; ls >> cnt;
        if (cnt != 3) { *err = path + ": only triangle faces supported"; return false; }
        uint32_t a, b, c; ls >> a >> b >> c;
        m->triangles.push_back(a); m->triangles.push_back(b); m->triangles.push_back(c);
    }
    return true;
}

std::vector<recon::Point3> SphereCloud(int n, double R) {
    std::vector<recon::Point3> v;
    const double golden = 3.14159265358979323846 * (3.0 - std::sqrt(5.0));
    for (int i = 0; i < n; ++i) {
        const double y = 1.0 - 2.0 * (i + 0.5) / n;
        const double r = std::sqrt(std::max(0.0, 1.0 - y * y));
        v.push_back({R * r * std::cos(golden * i), R * y, R * r * std::sin(golden * i)});
    }
    return v;
}

int RunSelftest() {
    const auto sphere = SphereCloud(2000, 1.0);
    std::vector<recon::FrameCloud> frames(8);
    for (int i = 0; i < static_cast<int>(sphere.size()); ++i) frames[i % 8].points.push_back(sphere[i]);
    const recon::PipelineOutput out = recon::RunReconPipeline(frames);
    const bool ok = out.ok && recon::IsWatertight(out.mesh) && out.triangle_count > 0;
    std::printf("[recon-pipeline selftest] frames=8 cloud=%zu tris=%zu watertight=%d\n", out.cloud.size(),
                out.triangle_count, static_cast<int>(recon::IsWatertight(out.mesh)));
    std::printf("[recon-pipeline selftest] %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int RunSession(const std::string& bundle, const std::string& calib_path, const std::string& out_path,
               const std::string& ref_path, bool live) {
    recon::Calibration cal;
    std::string err;
    if (!ReadCalibration(calib_path, &cal, &err)) { std::fprintf(stderr, "[recon-pipeline] %s\n", err.c_str()); return 2; }
    if (cal.planes.size() > 1)
        std::fprintf(stderr, "[recon-pipeline] WARNING: %zu-plane calib but no #206 association; using plane 0 for every centre.\n", cal.planes.size());

    capture::Reader reader;
    if (!reader.Open(bundle, &err)) { std::fprintf(stderr, "[recon-pipeline] %s\n", err.c_str()); return 2; }

    std::vector<recon::FrameCloud> frames;
    capture::Record rec;
    uint64_t ir = 0;
    while (reader.Next(&rec)) {
        if (rec.method.rfind("ob_rpc_get_frame", 0) != 0) continue;
        uint32_t len = 0;
        const uint8_t* data = FindFrameBin(rec.bytes, &len);
        if (!data) continue;
        const recon::FrameView view{data, kWidth, kHeight, kStride};
        const recon::Result centres = recon::ExtractLaserLines(view);
        const auto assoc = recon::AssociateSinglePlane(centres);  // plane 0 (see warning)
        const auto cloud = recon::Triangulate(centres, cal, assoc);
        recon::FrameCloud fc;
        fc.pose = recon::Pose{};  // identity: real per-frame pose is #206, gated
        for (const auto& cp : cloud) fc.points.push_back(cp.position);
        frames.push_back(std::move(fc));
        ++ir;
    }
    std::printf("[recon-pipeline] frames=%llu\n", static_cast<unsigned long long>(ir));

    recon::PipelineOutput out;
    if (live) {
        recon::LiveReconPipeline lp;
        for (const auto& f : frames) lp.AddFrame(f);
        const recon::Surface s = lp.LiveSurface();
        std::printf("[recon-pipeline] live surface: tris=%zu\n", s.triangle_count);
        out = lp.Finalize();
    } else {
        out = recon::RunReconPipeline(frames);
    }
    std::printf("[recon-pipeline] fused cloud=%zu  mesh tris=%zu  watertight=%d\n", out.cloud.size(),
                out.triangle_count, static_cast<int>(recon::IsWatertight(out.mesh)));

    if (!out_path.empty() && out.ok) {
        if (!WritePlyMesh(out_path, out.mesh)) { std::fprintf(stderr, "[recon-pipeline] write %s failed\n", out_path.c_str()); return 2; }
        std::printf("[recon-pipeline] wrote %s\n", out_path.c_str());
    }
    if (!ref_path.empty()) {
        mesh::MeshBuffers ref;
        if (!ReadPlyMesh(ref_path, &ref, &err)) { std::fprintf(stderr, "[recon-pipeline] %s\n", err.c_str()); return 2; }
        const recon::MeshMetrics m = recon::CompareMeshes(out.mesh, ref);
        if (!m.valid) std::printf("A/B: invalid (empty mesh)\n");
        else std::printf("A/B vs vendor: tris_ours=%zu tris_ref=%zu hausdorff=%.6g rms=%.6g max=%.6g watertight_ours=%d watertight_ref=%d\n",
                         m.tris_a, m.tris_b, m.hausdorff, m.rms_surface, m.max_surface,
                         static_cast<int>(m.watertight_a), static_cast<int>(m.watertight_b));
    }
    return 0;
}

void Usage() {
    std::printf("recon_pipeline — recorded IR session + calibration -> our mesh, A/B vs the vendor mesh\n"
                "  --selftest\n"
                "  --session <bundle> --calib <file> [--out mesh.ply] [--ref vendor.ply] [--live]\n"
                "calib: 'camera fx fy cx cy' then one 'plane a b c d' per laser plane.\n"
                "NOTE: calibration is #204/#508; per-frame pose + association are #206; both gated.\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::string bundle, calib, out, ref;
    bool live = false;
    enum { kNone, kSelftest, kSession } mode = kNone;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* n) -> std::string { if (i + 1 >= argc) { std::fprintf(stderr, "[recon-pipeline] %s needs a value\n", n); std::exit(2); } return argv[++i]; };
        if (a == "--selftest") mode = kSelftest;
        else if (a == "--session") { mode = kSession; bundle = next("--session"); }
        else if (a == "--calib") calib = next("--calib");
        else if (a == "--out") out = next("--out");
        else if (a == "--ref") ref = next("--ref");
        else if (a == "--live") live = true;
        else if (a == "-h" || a == "--help") { Usage(); return 0; }
        else { std::fprintf(stderr, "[recon-pipeline] unknown arg: %s\n", a.c_str()); Usage(); return 2; }
    }
    if (mode == kSelftest) return RunSelftest();
    if (mode == kSession) {
        if (calib.empty()) { std::fprintf(stderr, "[recon-pipeline] --session needs --calib\n"); return 2; }
        return RunSession(bundle, calib, out, ref, live);
    }
    Usage();
    return 2;
}
