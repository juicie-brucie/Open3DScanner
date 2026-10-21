// Row #149, the reader half: read the vendor's model.ply back.
//
// The vendor's stop tail writes <session output_dir>/result/model.ply — the
// full fused model cloud, not our decimated broadcast. The file's shape and
// where the write sits in the teardown are settled in
// docs/recon/findings/scan-live-data.md §16 and the #136 stage map
// (docs/recomp/log/2026-10-19-row-136-pipeline-map.md, stage 8); this header
// is the reader, and the format is parsed from the file's own header rather
// than assumed.
//
// It reads what the header declares: positions (x,y,z) are required; normals
// (nx,ny,nz), intensity and colour (red/green/blue) are reported when present.
// The writer the vendor uses in the live path emits intensity, not RGB, so
// "colour: absent" is a real answer, not a parse failure.
//
// Pure I/O, no vendor SDK, no device: the test writes small fixtures and reads
// them back (model_ply_test.cpp).
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace openscanner::scan_server {

// The session output directory the SDK writes into (tools/scan_server/main.cpp,
// session_config.output_dir). The archive's own location and format are the
// operator's decision (row #149); this is the vendor's folder as it exists.
inline constexpr const char* kSessionOutputDir = "/tmp/openscanner_live_server_session";

enum class ModelPlyError {
    kOk = 0,
    kNotFound,          // the file (or a session's model.ply) does not exist
    kUnreadable,        // exists, but open()/read() failed
    kNotPly,            // no "ply" magic
    kUnsupportedFormat, // not binary_little_endian/ascii 1.0, or an unknown type
    kMalformedHeader,   // a header line this reader cannot use
    kNoVertexElement,   // no "element vertex"
    kMissingPosition,   // the vertex element has no x/y/z
    kEmptyModel,        // a vertex element with zero vertices
    kTruncated,         // the file ends inside the vertex data
};

inline const char* ModelPlyErrorName(ModelPlyError error) {
    switch (error) {
        case ModelPlyError::kOk: return "ok";
        case ModelPlyError::kNotFound: return "not-found";
        case ModelPlyError::kUnreadable: return "unreadable";
        case ModelPlyError::kNotPly: return "not-ply";
        case ModelPlyError::kUnsupportedFormat: return "unsupported-format";
        case ModelPlyError::kMalformedHeader: return "malformed-header";
        case ModelPlyError::kNoVertexElement: return "no-vertex-element";
        case ModelPlyError::kMissingPosition: return "missing-position";
        case ModelPlyError::kEmptyModel: return "empty-model";
        case ModelPlyError::kTruncated: return "truncated";
    }
    return "unknown";
}

struct ModelPlySummary {
    std::string format;            // e.g. "binary_little_endian 1.0"
    uint64_t vertex_count = 0;
    uint64_t face_count = 0;       // 0 when the file has no "face" element
    bool has_normals = false;
    bool has_intensity = false;
    bool has_colour = false;       // red/green/blue properties
    bool bbox_valid = false;
    double bbox_min[3] = {0.0, 0.0, 0.0};
    double bbox_max[3] = {0.0, 0.0, 0.0};
    bool mean_intensity_valid = false;
    double mean_intensity = 0.0;

    double bbox_size(int axis) const { return bbox_max[axis] - bbox_min[axis]; }
};

namespace detail {

enum class PlyType {
    kInt8, kUint8, kInt16, kUint16, kInt32, kUint32, kFloat32, kFloat64, kUnknown
};

inline size_t PlyTypeSize(PlyType type) {
    switch (type) {
        case PlyType::kInt8:
        case PlyType::kUint8: return 1;
        case PlyType::kInt16:
        case PlyType::kUint16: return 2;
        case PlyType::kInt32:
        case PlyType::kUint32:
        case PlyType::kFloat32: return 4;
        case PlyType::kFloat64: return 8;
        default: return 0;
    }
}

inline PlyType PlyTypeFromName(const std::string& name) {
    if (name == "char" || name == "int8") return PlyType::kInt8;
    if (name == "uchar" || name == "uint8") return PlyType::kUint8;
    if (name == "short" || name == "int16") return PlyType::kInt16;
    if (name == "ushort" || name == "uint16") return PlyType::kUint16;
    if (name == "int" || name == "int32") return PlyType::kInt32;
    if (name == "uint" || name == "uint32") return PlyType::kUint32;
    if (name == "float" || name == "float32") return PlyType::kFloat32;
    if (name == "double" || name == "float64") return PlyType::kFloat64;
    return PlyType::kUnknown;
}

// One number from a record, widened to double. The host is little-endian on
// every target this tool builds for, which is the format the vendor writes.
inline double ReadScalar(const uint8_t* base, size_t offset, PlyType type) {
    const uint8_t* at = base + offset;
    switch (type) {
        case PlyType::kInt8: { int8_t v; std::memcpy(&v, at, 1); return static_cast<double>(v); }
        case PlyType::kUint8: { uint8_t v; std::memcpy(&v, at, 1); return static_cast<double>(v); }
        case PlyType::kInt16: { int16_t v; std::memcpy(&v, at, 2); return static_cast<double>(v); }
        case PlyType::kUint16: { uint16_t v; std::memcpy(&v, at, 2); return static_cast<double>(v); }
        case PlyType::kInt32: { int32_t v; std::memcpy(&v, at, 4); return static_cast<double>(v); }
        case PlyType::kUint32: { uint32_t v; std::memcpy(&v, at, 4); return static_cast<double>(v); }
        case PlyType::kFloat32: { float v; std::memcpy(&v, at, 4); return static_cast<double>(v); }
        case PlyType::kFloat64: { double v; std::memcpy(&v, at, 8); return v; }
        default: return 0.0;
    }
}

struct PlyProperty {
    std::string name;
    PlyType type = PlyType::kUnknown;
    size_t offset = 0;       // within a record (fixed-size elements)
    bool is_list = false;
    PlyType list_count_type = PlyType::kUnknown;
    PlyType list_item_type = PlyType::kUnknown;
};

struct PlyElement {
    std::string name;
    uint64_t count = 0;
    size_t record_size = 0;
    bool has_list = false;
    std::vector<PlyProperty> properties;
};

struct PlyHeader {
    std::string format;
    bool binary = true;
    std::vector<PlyElement> elements;
};

inline std::vector<std::string> SplitWords(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> words;
    std::string word;
    while (in >> word) words.push_back(word);
    return words;
}

inline ModelPlyError ParseHeader(std::istream& in, PlyHeader* header, std::string* detail) {
    std::string line;
    if (!std::getline(in, line)) {
        *detail = "empty file";
        return ModelPlyError::kNotPly;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "ply") {
        *detail = "first line is not \"ply\"";
        return ModelPlyError::kNotPly;
    }
    bool have_format = false;
    int lines_read = 0;
    while (std::getline(in, line)) {
        if (++lines_read > 10000) {
            *detail = "header has no end_header";
            return ModelPlyError::kMalformedHeader;
        }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::vector<std::string> words = SplitWords(line);
        if (words.empty()) continue;
        if (words[0] == "comment" || words[0] == "obj_info") continue;
        if (words[0] == "end_header") {
            if (!have_format) {
                *detail = "no format line";
                return ModelPlyError::kMalformedHeader;
            }
            if (header->elements.empty()) {
                *detail = "no elements";
                return ModelPlyError::kMalformedHeader;
            }
            return ModelPlyError::kOk;
        }
        if (words[0] == "format") {
            if (words.size() != 3) {
                *detail = "malformed format line";
                return ModelPlyError::kMalformedHeader;
            }
            header->format = words[1] + " " + words[2];
            if (words[1] == "binary_little_endian") {
                header->binary = true;
            } else if (words[1] == "ascii") {
                header->binary = false;
            } else {
                *detail = "format " + header->format + " (only binary_little_endian and ascii 1.0 are read)";
                return ModelPlyError::kUnsupportedFormat;
            }
            have_format = true;
            continue;
        }
        if (words[0] == "element") {
            if (words.size() != 3) {
                *detail = "malformed element line: " + line;
                return ModelPlyError::kMalformedHeader;
            }
            PlyElement element;
            element.name = words[1];
            element.count = static_cast<uint64_t>(std::strtoull(words[2].c_str(), nullptr, 10));
            header->elements.push_back(element);
            continue;
        }
        if (words[0] == "property") {
            if (header->elements.empty()) {
                *detail = "property before any element";
                return ModelPlyError::kMalformedHeader;
            }
            PlyElement& element = header->elements.back();
            PlyProperty property;
            if (words.size() >= 5 && words[1] == "list") {
                property.is_list = true;
                property.list_count_type = PlyTypeFromName(words[2]);
                property.list_item_type = PlyTypeFromName(words[3]);
                property.name = words[4];
                if (property.list_count_type == PlyType::kUnknown ||
                    property.list_item_type == PlyType::kUnknown) {
                    *detail = "unknown list type on " + property.name;
                    return ModelPlyError::kUnsupportedFormat;
                }
                element.has_list = true;
            } else if (words.size() == 3) {
                property.type = PlyTypeFromName(words[1]);
                property.name = words[2];
                if (property.type == PlyType::kUnknown) {
                    *detail = "unknown property type \"" + words[1] + "\"";
                    return ModelPlyError::kUnsupportedFormat;
                }
                property.offset = element.record_size;
                element.record_size += PlyTypeSize(property.type);
            } else {
                *detail = "malformed property line: " + line;
                return ModelPlyError::kMalformedHeader;
            }
            element.properties.push_back(property);
            continue;
        }
        // Unknown header keyword: ignore it, as PLY readers conventionally do.
    }
    *detail = "unexpected end of header";
    return ModelPlyError::kMalformedHeader;
}

inline const PlyProperty* FindProperty(const PlyElement& element, const char* name) {
    for (const PlyProperty& property : element.properties) {
        if (property.name == name) return &property;
    }
    return nullptr;
}

inline bool ReadExact(std::istream& in, char* out, size_t bytes) {
    in.read(out, static_cast<std::streamsize>(bytes));
    return static_cast<size_t>(in.gcount()) == bytes;
}

// Skips one binary element that is not the vertex block (e.g. faces).
inline bool SkipBinaryElement(std::istream& in, const PlyElement& element, std::string* detail) {
    if (!element.has_list) {
        const uint64_t bytes = element.count * static_cast<uint64_t>(element.record_size);
        in.seekg(static_cast<std::streamoff>(bytes), std::ios::cur);
        if (!in) {
            *detail = "cannot skip element \"" + element.name + "\"";
            return false;
        }
        return true;
    }
    for (uint64_t i = 0; i < element.count; ++i) {
        for (const PlyProperty& property : element.properties) {
            if (!property.is_list) {
                const size_t size = PlyTypeSize(property.type);
                std::vector<char> scratch(size);
                if (!ReadExact(in, scratch.data(), size)) {
                    *detail = "cannot skip element \"" + element.name + "\"";
                    return false;
                }
                continue;
            }
            uint8_t count_bytes[8] = {0};
            const size_t count_size = PlyTypeSize(property.list_count_type);
            if (!ReadExact(in, reinterpret_cast<char*>(count_bytes), count_size)) {
                *detail = "cannot read a list count in \"" + element.name + "\"";
                return false;
            }
            const uint64_t items = static_cast<uint64_t>(ReadScalar(count_bytes, 0, property.list_count_type));
            const uint64_t skip = items * static_cast<uint64_t>(PlyTypeSize(property.list_item_type));
            in.seekg(static_cast<std::streamoff>(skip), std::ios::cur);
            if (!in) {
                *detail = "cannot skip list items in \"" + element.name + "\"";
                return false;
            }
        }
    }
    return true;
}

inline bool SkipAsciiLines(std::istream& in, uint64_t count) {
    std::string line;
    for (uint64_t i = 0; i < count; ++i) {
        if (!std::getline(in, line)) return false;
    }
    return true;
}

}  // namespace detail

// Reads `path` and fills `summary`. On failure returns the named error and
// leaves a human-readable explanation in `detail`.
inline ModelPlyError ReadModelPly(const std::string& path, ModelPlySummary* summary, std::string* detail) {
    summary->format.clear();
    summary->vertex_count = 0;
    summary->face_count = 0;
    summary->has_normals = summary->has_intensity = summary->has_colour = false;
    summary->bbox_valid = false;
    summary->mean_intensity_valid = false;
    summary->mean_intensity = 0.0;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        *detail = "no such file";
        return ModelPlyError::kNotFound;
    }
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        *detail = "not a regular file";
        return ModelPlyError::kUnreadable;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        *detail = "cannot open for reading";
        return ModelPlyError::kUnreadable;
    }

    detail::PlyHeader header;
    const ModelPlyError header_error = detail::ParseHeader(in, &header, detail);
    if (header_error != ModelPlyError::kOk) return header_error;
    summary->format = header.format;

    const detail::PlyElement* vertex = nullptr;
    for (const detail::PlyElement& element : header.elements) {
        if (element.name == "vertex") {
            vertex = &element;
            break;
        }
    }
    if (vertex == nullptr) {
        *detail = "no element vertex";
        return ModelPlyError::kNoVertexElement;
    }
    for (const detail::PlyElement& element : header.elements) {
        if (element.name == "face") summary->face_count = element.count;
    }
    summary->vertex_count = vertex->count;
    if (vertex->count == 0) {
        *detail = "the vertex element is empty";
        return ModelPlyError::kEmptyModel;
    }
    if (vertex->has_list) {
        *detail = "list properties in the vertex element are not read";
        return ModelPlyError::kUnsupportedFormat;
    }

    const detail::PlyProperty* px = detail::FindProperty(*vertex, "x");
    const detail::PlyProperty* py = detail::FindProperty(*vertex, "y");
    const detail::PlyProperty* pz = detail::FindProperty(*vertex, "z");
    if (px == nullptr || py == nullptr || pz == nullptr) {
        *detail = "the vertex element has no x/y/z";
        return ModelPlyError::kMissingPosition;
    }
    const detail::PlyProperty* nx = detail::FindProperty(*vertex, "nx");
    const detail::PlyProperty* ny = detail::FindProperty(*vertex, "ny");
    const detail::PlyProperty* nz = detail::FindProperty(*vertex, "nz");
    const detail::PlyProperty* intensity = detail::FindProperty(*vertex, "intensity");
    const detail::PlyProperty* red = detail::FindProperty(*vertex, "red");
    const detail::PlyProperty* green = detail::FindProperty(*vertex, "green");
    const detail::PlyProperty* blue = detail::FindProperty(*vertex, "blue");
    summary->has_normals = nx != nullptr && ny != nullptr && nz != nullptr;
    summary->has_intensity = intensity != nullptr;
    summary->has_colour = red != nullptr && green != nullptr && blue != nullptr;

    double min[3] = {0.0, 0.0, 0.0};
    double max[3] = {0.0, 0.0, 0.0};
    double intensity_sum = 0.0;

    // Every element before "vertex" is skipped first; the vendor's file has
    // none, but a PLY with a leading element is still read correctly.
    for (const detail::PlyElement& element : header.elements) {
        if (&element == vertex) break;
        const bool ok = header.binary ? detail::SkipBinaryElement(in, element, detail)
                                      : detail::SkipAsciiLines(in, element.count);
        if (!ok) {
            if (detail->empty()) *detail = "cannot skip element \"" + element.name + "\"";
            return ModelPlyError::kTruncated;
        }
    }

    if (header.binary && vertex->record_size > 0) {
        const uint64_t kChunkRecords = 8192;
        std::vector<uint8_t> buffer(static_cast<size_t>(kChunkRecords) * vertex->record_size);
        uint64_t remaining = vertex->count;
        bool first = true;
        const bool want_intensity = intensity != nullptr;
        while (remaining > 0) {
            const uint64_t records = remaining < kChunkRecords ? remaining : kChunkRecords;
            const size_t bytes = static_cast<size_t>(records) * vertex->record_size;
            in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(bytes));
            if (static_cast<size_t>(in.gcount()) != bytes) {
                *detail = "the vertex data ends early";
                return ModelPlyError::kTruncated;
            }
            for (uint64_t r = 0; r < records; ++r) {
                const uint8_t* record = buffer.data() + r * vertex->record_size;
                const double x = detail::ReadScalar(record, px->offset, px->type);
                const double y = detail::ReadScalar(record, py->offset, py->type);
                const double z = detail::ReadScalar(record, pz->offset, pz->type);
                if (first) {
                    min[0] = max[0] = x;
                    min[1] = max[1] = y;
                    min[2] = max[2] = z;
                    first = false;
                } else {
                    min[0] = x < min[0] ? x : min[0];
                    min[1] = y < min[1] ? y : min[1];
                    min[2] = z < min[2] ? z : min[2];
                    max[0] = x > max[0] ? x : max[0];
                    max[1] = y > max[1] ? y : max[1];
                    max[2] = z > max[2] ? z : max[2];
                }
                if (want_intensity) {
                    intensity_sum += detail::ReadScalar(record, intensity->offset, intensity->type);
                }
            }
            remaining -= records;
        }
        summary->bbox_valid = !first;
    } else if (!header.binary) {
        std::string line;
        for (uint64_t i = 0; i < vertex->count; ++i) {
            if (!std::getline(in, line)) {
                *detail = "the vertex data ends early";
                return ModelPlyError::kTruncated;
            }
            std::istringstream row(line);
            std::vector<double> values;
            double value = 0.0;
            while (row >> value) values.push_back(value);
            if (values.size() < vertex->properties.size()) {
                *detail = "a vertex row has fewer values than the header declares";
                return ModelPlyError::kTruncated;
            }
            size_t position = 0;
            double xyz[3] = {0.0, 0.0, 0.0};
            for (const detail::PlyProperty& property : vertex->properties) {
                const double v = values[position++];
                if (property.name == "x") xyz[0] = v;
                else if (property.name == "y") xyz[1] = v;
                else if (property.name == "z") xyz[2] = v;
                else if (property.name == "intensity" && intensity != nullptr) intensity_sum += v;
            }
            if (i == 0) {
                for (int a = 0; a < 3; ++a) min[a] = max[a] = xyz[a];
            } else {
                for (int a = 0; a < 3; ++a) {
                    min[a] = xyz[a] < min[a] ? xyz[a] : min[a];
                    max[a] = xyz[a] > max[a] ? xyz[a] : max[a];
                }
            }
        }
        summary->bbox_valid = true;
    }

    for (int a = 0; a < 3; ++a) {
        summary->bbox_min[a] = min[a];
        summary->bbox_max[a] = max[a];
    }
    if (summary->has_intensity) {
        summary->mean_intensity_valid = true;
        summary->mean_intensity = intensity_sum / static_cast<double>(vertex->count);
    }
    return ModelPlyError::kOk;
}

// The project folder, resolved from the SDK's session output root: the newest
// <root>/<session>/result/model.ply, with the session's out-of-core cache
// reported when it is there (the cache itself is vendor-internal and unread).
struct SessionModel {
    ModelPlyError error = ModelPlyError::kNotFound;
    std::string detail;
    std::string root;
    std::string session_dir;
    std::string model_path;
    std::string obmc_path;  // empty when absent
};

inline SessionModel ResolveSessionModel(const std::string& root) {
    SessionModel result;
    result.root = root;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec) {
        result.error = ModelPlyError::kNotFound;
        result.detail = "the session output directory does not exist";
        return result;
    }
    bool found = false;
    std::filesystem::file_time_type newest{};
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec) || ec) continue;
        const std::filesystem::path model = entry.path() / "result" / "model.ply";
        std::error_code file_ec;
        if (!std::filesystem::is_regular_file(model, file_ec) || file_ec) continue;
        const std::filesystem::file_time_type written = std::filesystem::last_write_time(model, file_ec);
        if (file_ec) continue;
        if (!found || written > newest) {
            found = true;
            newest = written;
            result.session_dir = entry.path().string();
            result.model_path = model.string();
        }
    }
    if (!found) {
        result.error = ModelPlyError::kNotFound;
        result.detail = "no <session>/result/model.ply under the session output directory";
        return result;
    }
    const std::filesystem::path obmc = std::filesystem::path(result.session_dir) / "temp" / "mainLOMCache.obmc";
    if (std::filesystem::is_regular_file(obmc, ec) && !ec) result.obmc_path = obmc.string();
    result.error = ModelPlyError::kOk;
    return result;
}

}  // namespace openscanner::scan_server
