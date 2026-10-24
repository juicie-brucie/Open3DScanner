// Device-free test for the local-first project archive and the network copy
// (project_save.h). Small synthetic clouds, a temp cache root and a temp
// "share" — no device, no vendor artifact.
#include "project_save.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace openscanner::scan_server;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

fs::path MakeTempDir(const std::string& tag) {
    static int counter = 0;
    const fs::path dir = fs::temp_directory_path() /
                         ("openscanner-project-" + std::to_string(++counter) + "-" + tag);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

std::string ReadFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
    const std::vector<float> cube = {
        0, 0, 0,  1, 0, 0,  0, 1, 0,  0, 0, 1,  1, 1, 1,
    };
    const uint32_t count = static_cast<uint32_t>(cube.size() / 3);

    // --- Sanitisation keeps names inside the root --------------------------
    Check(SanitizeName("../etc/passwd", "fb") == "etcpasswd", "sanitize strips path escape");
    Check(SanitizeName("", "fallback") == "fallback", "empty name -> fallback");
    Check(SanitizeName("  ...  ", "fb") == "fb", "dots/space only -> fallback");
    Check(SanitizeName("Bracket Mount", "fb") == "Bracket Mount", "ordinary name kept");

    // --- A first save creates the project and writes the PLY ---------------
    const fs::path root = MakeTempDir("root");
    SaveResult r1 = SaveObjectToProject(root.string(), "MyScan", "partA", cube.data(), count);
    Check(r1.ok, "first save ok");
    Check(r1.project == "MyScan" && r1.object == "partA", "names round-trip");
    Check(r1.points == count, "point count reported");
    const fs::path ply = root / "MyScan" / "objects" / "partA" / "model.ply";
    Check(fs::exists(ply), "model.ply written");
    const std::string ply_text = ReadFile(ply);
    Check(ply_text.rfind("ply\n", 0) == 0, "PLY magic present");
    Check(ply_text.find("element vertex 5") != std::string::npos, "vertex count in header");
    Check(fs::exists(root / "MyScan" / "project.json"), "project.json written");
    Check(fs::exists(root / "MyScan" / "objects" / "partA" / "object.json"), "object.json written");

    // --- A project holds many objects --------------------------------------
    SaveResult r2 = SaveObjectToProject(root.string(), "MyScan", "partB", cube.data(), count);
    Check(r2.ok, "second object ok");
    Check(r2.objects.size() == 2, "project lists both objects");
    Check(r2.objects[0] == "partA" && r2.objects[1] == "partB", "objects enumerated sorted");

    // --- Empty names fall back to a (timestamp) slug, still saved ----------
    SaveResult r3 = SaveObjectToProject(root.string(), "", "", cube.data(), count);
    Check(r3.ok && !r3.project.empty() && !r3.object.empty(), "empty names -> timestamp save");

    // --- Guard rails -------------------------------------------------------
    SaveResult r4 = SaveObjectToProject(root.string(), "P", "O", nullptr, 0);
    Check(!r4.ok, "no data -> save fails cleanly");
    SaveResult r5 = SaveObjectToProject("", "P", "O", cube.data(), count);
    Check(!r5.ok, "no root -> save fails cleanly");

    // --- Upload mirrors the project folder into a mounted share path -------
    const fs::path share = MakeTempDir("share");
    UploadResult u1 = UploadProjectToNetwork(root.string(), share.string(), "MyScan");
    Check(u1.ok, "upload ok");
    Check(fs::exists(fs::path(share) / "MyScan" / "objects" / "partA" / "model.ply"), "uploaded tree present");
    UploadResult u2 = UploadProjectToNetwork(root.string(), "", "MyScan");
    Check(!u2.ok, "no network dest -> upload fails cleanly");
    UploadResult u3 = UploadProjectToNetwork(root.string(), share.string(), "DoesNotExist");
    Check(!u3.ok, "missing project -> upload fails cleanly");

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
