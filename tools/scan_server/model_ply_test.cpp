// Row #149's device-free test: the vendor model.ply reader and the project
// resolver. Fixtures are written here, small and synthetic (a few vertices) —
// no vendor artifact is committed.
#include "model_ply.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openscanner::scan_server::ModelPlyError;
using openscanner::scan_server::ModelPlyErrorName;
using openscanner::scan_server::ModelPlySummary;
using openscanner::scan_server::ReadModelPly;
using openscanner::scan_server::ResolveSessionModel;
using openscanner::scan_server::SessionModel;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

bool Near(double a, double b) { return std::fabs(a - b) < 1e-5; }

fs::path MakeTempDir(const std::string& tag) {
    static int counter = 0;
    const fs::path dir = fs::temp_directory_path() /
                         ("openscanner-model-ply-" + std::to_string(++counter) + "-" + tag);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// A binary little-endian PLY from a header and a list of typed fields.
struct Field {
    std::string type;   // "float", "uchar", ...
    std::string name;
};

std::string HeaderFor(const std::vector<Field>& fields, uint64_t vertices, const std::string& extra = "") {
    std::string header = "ply\nformat binary_little_endian 1.0\n";
    header += "comment row #149 fixture\n";
    header += "element vertex " + std::to_string(vertices) + "\n";
    for (const Field& field : fields) header += "property " + field.type + " " + field.name + "\n";
    header += extra;
    header += "end_header\n";
    return header;
}

void AppendFloat(std::string* out, float value) {
    char bytes[sizeof(float)];
    std::memcpy(bytes, &value, sizeof(value));
    out->append(bytes, sizeof(bytes));
}

void AppendUchar(std::string* out, uint8_t value) { out->push_back(static_cast<char>(value)); }

fs::path WriteIntensityFixture(const std::string& tag) {
    const std::vector<Field> fields = {{"float", "x"}, {"float", "y"}, {"float", "z"},
                                       {"float", "intensity"},
                                       {"float", "nx"}, {"float", "ny"}, {"float", "nz"}};
    const float vertices[4][7] = {
        {0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f, 20.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 2.0f, 0.0f, 30.0f, 0.0f, 0.0f, 1.0f},
        {0.5f, -1.0f, 3.0f, 40.0f, 1.0f, 0.0f, 0.0f},
    };
    std::string bytes = HeaderFor(fields, 4);
    for (const auto& vertex : vertices) {
        for (float value : vertex) AppendFloat(&bytes, value);
    }
    const fs::path path = MakeTempDir(tag) / "model.ply";
    WriteBytes(path, bytes);
    return path;
}

}  // namespace

int main() {
    // --- the vendor's shape: x,y,z,intensity,nx,ny,nz -----------------------
    {
        const fs::path path = WriteIntensityFixture("intensity");
        ModelPlySummary summary;
        std::string detail;
        const ModelPlyError error = ReadModelPly(path.string(), &summary, &detail);
        Check(error == ModelPlyError::kOk, "binary intensity PLY reads");
        if (error != ModelPlyError::kOk) std::printf("  detail: %s\n", detail.c_str());
        Check(summary.format == "binary_little_endian 1.0", "the format line is reported");
        Check(summary.vertex_count == 4, "the vertex count is read");
        Check(summary.has_normals, "normals are reported present");
        Check(summary.has_intensity, "intensity is reported present");
        Check(!summary.has_colour, "colour is reported absent (the writer emits intensity)");
        Check(summary.bbox_valid, "the bbox is valid");
        Check(Near(summary.bbox_min[0], 0.0) && Near(summary.bbox_max[0], 1.0), "bbox x");
        Check(Near(summary.bbox_min[1], -1.0) && Near(summary.bbox_max[1], 2.0), "bbox y");
        Check(Near(summary.bbox_min[2], 0.0) && Near(summary.bbox_max[2], 3.0), "bbox z");
        Check(Near(summary.bbox_size(2), 3.0), "the bbox size is max - min");
        Check(summary.mean_intensity_valid && Near(summary.mean_intensity, 25.0),
              "the mean intensity is read (10+20+30+40 over 4)");
        Check(summary.face_count == 0, "no face element -> 0");
    }

    // --- colour properties, when a textured run ever writes them -----------
    {
        const std::vector<Field> fields = {{"float", "x"}, {"float", "y"},  {"float", "z"},
                                           {"uchar", "red"}, {"uchar", "green"}, {"uchar", "blue"}};
        std::string bytes = HeaderFor(fields, 2);
        const float vertices[2][3] = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};
        for (const auto& vertex : vertices) {
            for (float value : vertex) AppendFloat(&bytes, value);
            AppendUchar(&bytes, 255);
            AppendUchar(&bytes, 128);
            AppendUchar(&bytes, 0);
        }
        const fs::path path = MakeTempDir("colour") / "model.ply";
        WriteBytes(path, bytes);

        ModelPlySummary summary;
        std::string detail;
        const ModelPlyError error = ReadModelPly(path.string(), &summary, &detail);
        Check(error == ModelPlyError::kOk, "a uchar RGB PLY reads");
        Check(summary.has_colour, "colour is reported present");
        Check(!summary.has_normals && !summary.has_intensity, "no normals/intensity in this file");
        Check(summary.vertex_count == 2, "coloured vertex count");
    }

    // --- ASCII --------------------------------------------------------------
    {
        std::string text = "ply\nformat ascii 1.0\nelement vertex 2\n"
                           "property float x\nproperty float y\nproperty float z\n"
                           "property float nx\nproperty float ny\nproperty float nz\nend_header\n"
                           "0 0 0 0 0 1\n2 3 4 1 0 0\n";
        const fs::path path = MakeTempDir("ascii") / "model.ply";
        WriteBytes(path, text);
        ModelPlySummary summary;
        std::string detail;
        const ModelPlyError error = ReadModelPly(path.string(), &summary, &detail);
        Check(error == ModelPlyError::kOk, "an ASCII PLY reads");
        Check(summary.format == "ascii 1.0", "the ASCII format is reported");
        Check(summary.vertex_count == 2 && summary.has_normals, "ASCII fields");
        Check(Near(summary.bbox_max[0], 2.0) && Near(summary.bbox_max[2], 4.0), "ASCII bbox");
    }

    // --- a face element after the vertices is reported and does not break us -
    {
        const std::vector<Field> fields = {{"float", "x"}, {"float", "y"}, {"float", "z"}};
        std::string bytes = HeaderFor(fields, 3,
                                      "element face 1\nproperty list uchar int vertex_indices\n");
        const float vertices[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        for (const auto& vertex : vertices) {
            for (float value : vertex) AppendFloat(&bytes, value);
        }
        AppendUchar(&bytes, 3);  // one triangle
        const int indices[3] = {0, 1, 2};
        char index_bytes[sizeof(indices)];
        std::memcpy(index_bytes, indices, sizeof(indices));
        bytes.append(index_bytes, sizeof(index_bytes));
        const fs::path path = MakeTempDir("faces") / "model.ply";
        WriteBytes(path, bytes);

        ModelPlySummary summary;
        std::string detail;
        const ModelPlyError error = ReadModelPly(path.string(), &summary, &detail);
        Check(error == ModelPlyError::kOk, "a PLY with faces still yields the vertex summary");
        Check(summary.face_count == 1, "the face count is reported");
        Check(summary.vertex_count == 3, "the vertices before the faces are read");
    }

    // --- the named failure paths -------------------------------------------
    {
        ModelPlySummary summary;
        std::string detail;
        const fs::path missing = MakeTempDir("missing") / "model.ply";
        const ModelPlyError error = ReadModelPly(missing.string(), &summary, &detail);
        Check(error == ModelPlyError::kNotFound, "an absent file -> not-found");
        Check(std::string(ModelPlyErrorName(error)) == "not-found", "the error has a name");
    }
    {
        const fs::path path = MakeTempDir("notply") / "model.ply";
        WriteBytes(path, "hello world\n");
        ModelPlySummary summary;
        std::string detail;
        Check(ReadModelPly(path.string(), &summary, &detail) == ModelPlyError::kNotPly,
              "a non-PLY file -> not-ply");
    }
    {
        // A valid header, but the vertex block is cut short.
        const std::vector<Field> fields = {{"float", "x"}, {"float", "y"}, {"float", "z"}};
        std::string bytes = HeaderFor(fields, 4);
        for (int i = 0; i < 3; ++i) {
            AppendFloat(&bytes, 1.0f);
            AppendFloat(&bytes, 2.0f);
            AppendFloat(&bytes, 3.0f);
        }
        const fs::path path = MakeTempDir("truncated") / "model.ply";
        WriteBytes(path, bytes);
        ModelPlySummary summary;
        std::string detail;
        Check(ReadModelPly(path.string(), &summary, &detail) == ModelPlyError::kTruncated,
              "a short vertex block -> truncated");
    }
    {
        const fs::path path = MakeTempDir("novertex") / "model.ply";
        WriteBytes(path, "ply\nformat ascii 1.0\nelement face 1\nproperty list uchar int vertex_indices\n"
                         "end_header\n3 0 1 2\n");
        ModelPlySummary summary;
        std::string detail;
        Check(ReadModelPly(path.string(), &summary, &detail) == ModelPlyError::kNoVertexElement,
              "no vertex element -> no-vertex-element");
    }
    {
        const std::vector<Field> fields = {{"float", "x"}, {"float", "y"}};
        std::string bytes = HeaderFor(fields, 1);
        AppendFloat(&bytes, 1.0f);
        AppendFloat(&bytes, 2.0f);
        const fs::path path = MakeTempDir("missingz") / "model.ply";
        WriteBytes(path, bytes);
        ModelPlySummary summary;
        std::string detail;
        Check(ReadModelPly(path.string(), &summary, &detail) == ModelPlyError::kMissingPosition,
              "no z property -> missing-position");
    }
    {
        const std::vector<Field> fields = {{"float", "x"}, {"float", "y"}, {"float", "z"}};
        const fs::path path = MakeTempDir("empty") / "model.ply";
        WriteBytes(path, HeaderFor(fields, 0));
        ModelPlySummary summary;
        std::string detail;
        Check(ReadModelPly(path.string(), &summary, &detail) == ModelPlyError::kEmptyModel,
              "zero vertices -> empty-model");
    }
    {
        const std::vector<Field> fields = {{"float", "x"}, {"float", "y"}, {"float", "z"}};
        std::string bytes = "ply\nformat binary_big_endian 1.0\nelement vertex 1\n";
        for (const Field& field : fields) bytes += "property " + field.type + " " + field.name + "\n";
        bytes += "end_header\n";
        const fs::path path = MakeTempDir("bigendian") / "model.ply";
        WriteBytes(path, bytes);
        ModelPlySummary summary;
        std::string detail;
        Check(ReadModelPly(path.string(), &summary, &detail) == ModelPlyError::kUnsupportedFormat,
              "big-endian -> unsupported-format");
    }

    // --- the project resolver ----------------------------------------------
    {
        const fs::path root = MakeTempDir("project");
        const fs::path older = root / "sess-older";
        const fs::path newer = root / "sess-newer";
        fs::create_directories(older / "result");
        fs::create_directories(newer / "result");
        fs::create_directories(newer / "temp");
        WriteBytes(older / "result" / "model.ply", HeaderFor({{"float", "x"}, {"float", "y"}, {"float", "z"}}, 0));
        WriteBytes(newer / "result" / "model.ply", HeaderFor({{"float", "x"}, {"float", "y"}, {"float", "z"}}, 0));
        WriteBytes(newer / "temp" / "mainLOMCache.obmc", "cache");
        fs::create_directories(root / "sess-no-model");

        const auto now = fs::file_time_type::clock::now();
        fs::last_write_time(older / "result" / "model.ply", now - std::chrono::hours(2));
        fs::last_write_time(newer / "result" / "model.ply", now);

        const SessionModel resolved = ResolveSessionModel(root.string());
        Check(resolved.error == ModelPlyError::kOk, "the project resolver finds a model");
        Check(resolved.session_dir == newer.string(), "it picks the newest model.ply");
        Check(resolved.model_path == (newer / "result" / "model.ply").string(), "the model path is reported");
        Check(!resolved.obmc_path.empty() && resolved.obmc_path.find("mainLOMCache.obmc") != std::string::npos,
              "the out-of-core cache is reported when present");
    }
    {
        const fs::path root = MakeTempDir("project-empty");
        const SessionModel resolved = ResolveSessionModel(root.string());
        Check(resolved.error == ModelPlyError::kNotFound, "an empty project root -> not-found");
        Check(!resolved.detail.empty(), "the failure carries a detail");
    }
    {
        const fs::path missing = MakeTempDir("project-missing") / "does-not-exist";
        const SessionModel resolved = ResolveSessionModel(missing.string());
        Check(resolved.error == ModelPlyError::kNotFound, "an absent project root -> not-found");
    }

    std::printf("[model-ply] %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
