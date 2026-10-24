// WO-2's point-cloud tool: triangulate a recorded IR frame into our own cloud
// and A/B it against a vendor reference cloud.
//
// The math lives in the engine (recon::Triangulate, recon::CompareClouds), all
// device-free and unit-tested. This tool is the I/O and the glue:
//   --selftest                 a synthetic end-to-end, no files (the ctest).
//   --frame <bundle> --calib f  a recorded IR frame -> our cloud (PLY).
//   --compare ours.ply ref.ply  score two clouds (the A/B harness).
//
// Clean-room: the RCRP bundle is read with the shared capture::Reader; the
// calibration is read from a caller-supplied text file, never a vendor binary.
//
// GATED, by design (not failures — the WO names them):
//   * real calibration (the laser planes + intrinsics) is recon #204; until it
//     lands, pass a synthetic/placeholder --calib and the cloud's absolute
//     accuracy is unverifiable.
//   * the multi-line plane association is #206; with a >1-plane calibration and
//     no association source, this tool falls back to plane 0 for every centre
//     and says so loudly — a single-plane self-consistency cloud, not the real
//     multi-line reconstruction.
//   * the quantitative vendor A/B needs a paired recording (the same scene's IR
//     frames and the vendor's cloud); --compare runs on any two PLYs today.

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "capture.h"
#include "recon/cloud_compare.h"
#include "recon/laser_line.h"
#include "recon/triangulate.h"

namespace recon = openscanner::recon;

namespace {

constexpr int kWidth = 1920;
constexpr int kHeight = 1200;
constexpr int kStride = 1920;
constexpr uint32_t kFrameBytes = 2304000;  // 1920 * 1200 * 1

// The get_frame reply's 11-element array carries a Bin(2304000) at element 4.
// Same extraction as tools/laser_line (row #205); kept local to the tool.
const uint8_t* FindFrameBin(const std::vector<uint8_t>& reply, uint32_t* len) {
    for (size_t i = 0; i + 5 <= reply.size(); ++i) {
        if (reply[i] != 0xC5) continue;  // msgpack bin16 marker
        const uint32_t n = (static_cast<uint32_t>(reply[i + 1]) << 8) | reply[i + 2];
        if (n == kFrameBytes && i + 3 + n <= reply.size()) {
            *len = n;
            return reply.data() + i + 3;
        }
    }
    return nullptr;
}

// A minimal ASCII-PLY writer (x y z, float). Enough for our clouds and for an
// interchange the operator's tools can read.
bool WritePly(const std::string& path, const std::vector<recon::Point3>& pts) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "ply\nformat ascii 1.0\nelement vertex " << pts.size()
        << "\nproperty float x\nproperty float y\nproperty float z\nend_header\n";
    for (const recon::Point3& p : pts) out << p.x << ' ' << p.y << ' ' << p.z << '\n';
    return static_cast<bool>(out);
}

// A minimal ASCII/binary-little-endian PLY reader for x/y/z vertices. Handles
// the common subset vendor exporters emit; unknown extra properties per vertex
// are skipped by count when the header declares them (ascii only).
bool ReadPly(const std::string& path, std::vector<recon::Point3>* pts, std::string* err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { *err = path + ": cannot open"; return false; }
    std::string line;
    std::getline(in, line);
    if (line.rfind("ply", 0) != 0) { *err = path + ": not a PLY"; return false; }
    bool ascii = true;
    size_t vertex_count = 0;
    int prop_count = 0, x_idx = -1, y_idx = -1, z_idx = -1;
    bool in_vertex = false;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string tok;
        ls >> tok;
        if (tok == "format") {
            std::string fmt;
            ls >> fmt;
            ascii = (fmt == "ascii");
        } else if (tok == "element") {
            std::string kind;
            ls >> kind >> vertex_count;
            in_vertex = (kind == "vertex");
            if (!in_vertex) vertex_count = 0;  // only vertex elements are read
        } else if (tok == "property" && in_vertex) {
            std::string type, name;
            ls >> type >> name;
            if (name == "x") x_idx = prop_count;
            else if (name == "y") y_idx = prop_count;
            else if (name == "z") z_idx = prop_count;
            ++prop_count;
        } else if (tok == "end_header") {
            break;
        }
    }
    if (x_idx < 0 || y_idx < 0 || z_idx < 0) { *err = path + ": no x/y/z vertex properties"; return false; }
    if (!ascii) { *err = path + ": only ascii PLY is supported here"; return false; }
    pts->clear();
    pts->reserve(vertex_count);
    for (size_t v = 0; v < vertex_count; ++v) {
        if (!std::getline(in, line)) break;
        std::istringstream ls(line);
        std::vector<double> f;
        double val;
        while (ls >> val) f.push_back(val);
        if (static_cast<int>(f.size()) <= z_idx) continue;
        pts->push_back({f[x_idx], f[y_idx], f[z_idx]});
    }
    return true;
}

// The calibration text file:
//   camera <fx> <fy> <cx> <cy>
//   plane  <a> <b> <c> <d>      (repeated, one per laser plane)
// '#' starts a comment; blank lines are ignored.
bool ReadCalibration(const std::string& path, recon::Calibration* cal, std::string* err) {
    std::ifstream in(path);
    if (!in) { *err = path + ": cannot open"; return false; }
    std::string line;
    bool have_camera = false;
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream ls(line);
        std::string tok;
        if (!(ls >> tok)) continue;
        if (tok == "camera") {
            if (!(ls >> cal->camera.fx >> cal->camera.fy >> cal->camera.cx >> cal->camera.cy)) {
                *err = path + ": malformed camera line";
                return false;
            }
            have_camera = true;
        } else if (tok == "plane") {
            recon::LaserPlane p;
            if (!(ls >> p.a >> p.b >> p.c >> p.d)) { *err = path + ": malformed plane line"; return false; }
            cal->planes.push_back(p);
        } else {
            *err = path + ": unknown directive '" + tok + "'";
            return false;
        }
    }
    if (!have_camera) { *err = path + ": no camera line"; return false; }
    if (cal->planes.empty()) { *err = path + ": no plane lines"; return false; }
    return true;
}

int RunSelftest() {
    // A synthetic frame on a known plane, triangulated and then compared with
    // itself: end-to-end glue, no files, no device.
    const recon::CameraIntrinsics k{1000.0, 1000.0, 960.0, 600.0};
    const recon::LaserPlane plane{0, 0, 1, -500};  // Z = 500
    recon::Result frame;
    frame.valid = true;
    frame.rows.resize(1000);
    std::vector<std::vector<int>> assoc(1000);
    int n = 0;
    for (int row = 400; row < 800; row += 20) {
        recon::LineCenter c;
        c.y = row;
        c.x = static_cast<float>(900 + (row % 7) * 3);  // some sub-pixel columns
        frame.rows[row].centres.push_back(c);
        assoc[row] = {0};
        ++n;
    }
    frame.centre_count = n;
    recon::Calibration cal{k, {plane}};
    const auto cloud = recon::Triangulate(frame, cal, assoc);
    const auto pts = recon::Positions(cloud);
    const auto m = recon::CompareClouds(pts, pts, 0.1);
    const bool ok = (static_cast<int>(cloud.size()) == n) && m.valid && m.rms_nn < 1e-9 &&
                    std::fabs(m.coverage - 1.0) < 1e-9;
    std::printf("[point-cloud selftest] centres=%d -> points=%zu; self-compare rms=%.3e coverage=%.3f\n",
                n, cloud.size(), m.rms_nn, m.coverage);
    std::printf("[point-cloud selftest] %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

void PrintMetrics(const recon::CloudMetrics& m) {
    if (!m.valid) {
        std::printf("A/B: invalid (empty cloud or tau <= 0)\n");
        return;
    }
    std::printf("A/B: count_ours=%zu count_ref=%zu  rms_nn=%.6g mean_nn=%.6g max_nn=%.6g  coverage=%.4f\n",
                m.count_a, m.count_b, m.rms_nn, m.mean_nn, m.max_nn, m.coverage);
}

int RunCompare(const std::string& ours_path, const std::string& ref_path, double tau) {
    std::vector<recon::Point3> ours, ref;
    std::string err;
    if (!ReadPly(ours_path, &ours, &err) || !ReadPly(ref_path, &ref, &err)) {
        std::fprintf(stderr, "[point-cloud] %s\n", err.c_str());
        return 2;
    }
    PrintMetrics(recon::CompareClouds(ours, ref, tau));
    return 0;
}

int RunFrame(const std::string& bundle, const std::string& calib_path, const std::string& out_path,
             const std::string& ref_path, double tau) {
    recon::Calibration cal;
    std::string err;
    if (!ReadCalibration(calib_path, &cal, &err)) {
        std::fprintf(stderr, "[point-cloud] %s\n", err.c_str());
        return 2;
    }
    if (cal.planes.size() > 1) {
        std::fprintf(stderr,
                     "[point-cloud] WARNING: %zu-plane calibration but no #206 association; "
                     "assigning every centre to plane 0 (single-plane placeholder, NOT the real "
                     "multi-line reconstruction).\n",
                     cal.planes.size());
    }

    capture::Reader reader;
    if (!reader.Open(bundle, &err)) {
        std::fprintf(stderr, "[point-cloud] %s\n", err.c_str());
        return 2;
    }
    capture::Record rec;
    std::vector<recon::Point3> cloud_pts;
    uint64_t frames = 0;
    while (reader.Next(&rec)) {
        if (rec.method.rfind("ob_rpc_get_frame", 0) != 0) continue;
        uint32_t len = 0;
        const uint8_t* data = FindFrameBin(rec.bytes, &len);
        if (data == nullptr) continue;
        const recon::FrameView view{data, kWidth, kHeight, kStride};
        const recon::Result centres = recon::ExtractLaserLines(view);
        const auto assoc = recon::AssociateSinglePlane(centres);  // plane 0 (see warning above)
        const auto cloud = recon::Triangulate(centres, cal, assoc);
        for (const auto& cp : cloud) cloud_pts.push_back(cp.position);
        ++frames;
    }
    std::printf("[point-cloud] frames=%llu  points=%zu\n", static_cast<unsigned long long>(frames),
                cloud_pts.size());
    if (!out_path.empty()) {
        if (!WritePly(out_path, cloud_pts)) {
            std::fprintf(stderr, "[point-cloud] could not write %s\n", out_path.c_str());
            return 2;
        }
        std::printf("[point-cloud] wrote %s\n", out_path.c_str());
    }
    if (!ref_path.empty()) {
        std::vector<recon::Point3> ref;
        if (!ReadPly(ref_path, &ref, &err)) {
            std::fprintf(stderr, "[point-cloud] %s\n", err.c_str());
            return 2;
        }
        PrintMetrics(recon::CompareClouds(cloud_pts, ref, tau));
    }
    return 0;
}

void Usage() {
    std::printf(
        "point_cloud — triangulate a recorded IR frame and A/B it vs a reference\n"
        "  --selftest                         synthetic end-to-end, no files\n"
        "  --frame <bundle> --calib <file>    IR frames -> our cloud\n"
        "      [--out cloud.ply] [--ref ref.ply] [--tau <t>]\n"
        "  --compare <ours.ply> <ref.ply> [--tau <t>]\n"
        "\n"
        "calib file:  'camera fx fy cx cy' then one 'plane a b c d' per laser plane.\n"
        "NOTE: real calibration is recon #204 and the multi-line association is #206;\n"
        "      a >1-plane calib without #206 produces a plane-0 placeholder cloud.\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::string bundle, calib, out, ref, ours;
    double tau = 1.0;
    enum { kNone, kSelftest, kFrame, kCompare } mode = kNone;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "[point-cloud] %s needs a value\n", name); std::exit(2); }
            return argv[++i];
        };
        if (a == "--selftest") mode = kSelftest;
        else if (a == "--frame") { mode = kFrame; bundle = next("--frame"); }
        else if (a == "--calib") calib = next("--calib");
        else if (a == "--out") out = next("--out");
        else if (a == "--ref") ref = next("--ref");
        else if (a == "--tau") tau = std::atof(next("--tau").c_str());
        else if (a == "--compare") { mode = kCompare; ours = next("--compare"); ref = next("--compare(ref)"); }
        else if (a == "-h" || a == "--help") { Usage(); return 0; }
        else { std::fprintf(stderr, "[point-cloud] unknown argument: %s\n", a.c_str()); Usage(); return 2; }
    }
    switch (mode) {
        case kSelftest: return RunSelftest();
        case kFrame:
            if (calib.empty()) { std::fprintf(stderr, "[point-cloud] --frame needs --calib\n"); return 2; }
            return RunFrame(bundle, calib, out, ref, tau);
        case kCompare: return RunCompare(ours, ref, tau);
        default: Usage(); return 2;
    }
}
