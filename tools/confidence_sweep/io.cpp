// The harness's readers and writers (row #159).
//
// The PLY reader is here because the vendor's own model output is a PLY and
// because the #126 capture will hand the harness one; the recorded-session
// reader is here because the project's V1 recorder is the other way a cloud
// arrives without a device.
//
// The V1 envelope is decoded with the project's own codec
// (engine/include/openscanner/frame_protocol.h) - the wire format is not
// restated here.

#include "harness.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "openscanner/frame_protocol.h"

namespace openscanner {
namespace confidence {
namespace {

// The PLY property types and their sizes in bytes.
struct PlyType {
    const char* name;
    int size;
    bool is_float;
};

const PlyType kPlyTypes[] = {
    {"char", 1, false},   {"int8", 1, false},   {"uchar", 1, false},  {"uint8", 1, false},
    {"short", 2, false},  {"int16", 2, false},  {"ushort", 2, false}, {"uint16", 2, false},
    {"int", 4, false},    {"int32", 4, false},  {"uint", 4, false},  {"uint32", 4, false},
    {"float", 4, true},   {"float32", 4, true}, {"double", 8, true},  {"float64", 8, true},
};

const PlyType* find_ply_type(const std::string& name) {
    for (const PlyType& t : kPlyTypes) {
        if (name == t.name) return &t;
    }
    return nullptr;
}

struct PlyProperty {
    std::string name;
    const PlyType* type = nullptr;
    bool is_list = false;
    const PlyType* count_type = nullptr;  // for a list property
};

struct PlyElement {
    std::string name;
    std::size_t count = 0;
    std::vector<PlyProperty> properties;
};

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// The host's byte order, so a PLY written big-endian can still be read.
bool host_is_big_endian() {
    const uint16_t probe = 1;
    return reinterpret_cast<const uint8_t*>(&probe)[0] == 0;
}

// Reads one scalar of the given type from `data`, honouring the file's byte
// order.
double read_binary_scalar(const uint8_t* data, const PlyType& type, bool file_big_endian) {
    uint8_t bytes[8];
    const std::size_t size = static_cast<std::size_t>(type.size);
    std::memcpy(bytes, data, size);
    if (file_big_endian != host_is_big_endian()) std::reverse(bytes, bytes + size);
    switch (size) {
        case 1:
            return static_cast<double>(bytes[0]);
        case 2: {
            uint16_t v;
            std::memcpy(&v, bytes, 2);
            return static_cast<double>(v);
        }
        case 4: {
            if (type.is_float) {
                float v;
                std::memcpy(&v, bytes, 4);
                return static_cast<double>(v);
            }
            uint32_t v;
            std::memcpy(&v, bytes, 4);
            return static_cast<double>(v);
        }
        default: {
            double v;
            std::memcpy(&v, bytes, 8);
            return v;
        }
    }
}

// Reads a PLY cloud: ascii or binary, any element/property layout, as long
// as the vertex element carries x, y and z.
bool read_ply_impl(const std::string& path, Cloud* out, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::string line;
    if (!std::getline(in, line) || trim(line) != "ply") {
        if (error) *error = path + ": not a PLY file";
        return false;
    }
    std::string format;
    std::vector<PlyElement> elements;
    bool header_done = false;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t.rfind("comment", 0) == 0 || t.rfind("obj_info", 0) == 0) continue;
        std::istringstream ss(t);
        std::string key;
        ss >> key;
        if (key == "format") {
            ss >> format;
        } else if (key == "element") {
            PlyElement e;
            ss >> e.name >> e.count;
            elements.push_back(e);
        } else if (key == "property") {
            if (elements.empty()) {
                if (error) *error = path + ": a property before any element";
                return false;
            }
            std::string type_name;
            ss >> type_name;
            PlyProperty prop;
            if (type_name == "list") {
                std::string count_name, value_name;
                ss >> count_name >> value_name >> prop.name;
                prop.is_list = true;
                prop.count_type = find_ply_type(count_name);
                prop.type = find_ply_type(value_name);
                if (!prop.count_type || !prop.type) {
                    if (error) *error = path + ": unknown list property type";
                    return false;
                }
            } else {
                ss >> prop.name;
                prop.type = find_ply_type(type_name);
                if (!prop.type) {
                    if (error) *error = path + ": unknown property type " + type_name;
                    return false;
                }
            }
            elements.back().properties.push_back(prop);
        } else if (key == "end_header") {
            header_done = true;
            break;
        }
    }
    if (!header_done) {
        if (error) *error = path + ": no end_header";
        return false;
    }
    const bool ascii = (format == "ascii");
    if (!ascii && format != "binary_little_endian" && format != "binary_big_endian") {
        if (error) *error = path + ": unsupported PLY format " + format;
        return false;
    }
    const bool file_big_endian = (format == "binary_big_endian");
    const PlyElement* vertex = nullptr;
    for (const PlyElement& e : elements) {
        if (e.name == "vertex") {
            vertex = &e;
            break;
        }
    }
    if (!vertex) {
        if (error) *error = path + ": no vertex element";
        return false;
    }
    int ix = -1, iy = -1, iz = -1;
    for (std::size_t i = 0; i < vertex->properties.size(); ++i) {
        if (vertex->properties[i].name == "x") ix = static_cast<int>(i);
        if (vertex->properties[i].name == "y") iy = static_cast<int>(i);
        if (vertex->properties[i].name == "z") iz = static_cast<int>(i);
    }
    if (ix < 0 || iy < 0 || iz < 0) {
        if (error) *error = path + ": the vertex element has no x/y/z";
        return false;
    }
    out->xyz.clear();
    out->xyz.reserve(vertex->count * 3);
    if (ascii) {
        std::size_t read_vertices = 0;
        while (read_vertices < vertex->count && std::getline(in, line)) {
            const std::string t = trim(line);
            if (t.empty()) continue;
            std::istringstream ss(t);
            double coords[3] = {0, 0, 0};
            std::size_t prop = 0;
            bool ok = true;
            for (const PlyProperty& p : vertex->properties) {
                if (p.is_list) {
                    if (error) *error = path + ": a list property on the vertex element is not supported";
                    return false;
                }
                double v = 0.0;
                if (!(ss >> v)) {
                    ok = false;
                    break;
                }
                if (prop == static_cast<std::size_t>(ix)) coords[0] = v;
                if (prop == static_cast<std::size_t>(iy)) coords[1] = v;
                if (prop == static_cast<std::size_t>(iz)) coords[2] = v;
                ++prop;
            }
            if (!ok) break;
            out->xyz.push_back(static_cast<float>(coords[0]));
            out->xyz.push_back(static_cast<float>(coords[1]));
            out->xyz.push_back(static_cast<float>(coords[2]));
            ++read_vertices;
        }
    } else {
        // Binary: the elements follow the header in order. The vertex element
        // is read here; the elements after it are skipped.
        std::vector<uint8_t> buffer;
        auto read_exact = [&](uint8_t* dst, std::size_t n) {
            in.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
            return static_cast<std::size_t>(in.gcount()) == n;
        };
        for (const PlyElement& e : elements) {
            const bool is_vertex = (&e == vertex);
            for (std::size_t v = 0; v < e.count; ++v) {
                if (is_vertex) {
                    double coords[3] = {0, 0, 0};
                    for (std::size_t prop = 0; prop < e.properties.size(); ++prop) {
                        const PlyProperty& p = e.properties[prop];
                        if (p.is_list) {
                            if (error) *error = path + ": a list property on the vertex element is not supported";
                            return false;
                        }
                        uint8_t raw[8];
                        if (!read_exact(raw, static_cast<std::size_t>(p.type->size))) {
                            if (error) *error = path + ": truncated vertex data";
                            return false;
                        }
                        const double value = read_binary_scalar(raw, *p.type, file_big_endian);
                        if (prop == static_cast<std::size_t>(ix)) coords[0] = value;
                        if (prop == static_cast<std::size_t>(iy)) coords[1] = value;
                        if (prop == static_cast<std::size_t>(iz)) coords[2] = value;
                    }
                    out->xyz.push_back(static_cast<float>(coords[0]));
                    out->xyz.push_back(static_cast<float>(coords[1]));
                    out->xyz.push_back(static_cast<float>(coords[2]));
                } else {
                    // Skip this element's records: the byte count when the
                    // record layout is fixed, the records themselves when it
                    // is not.
                    std::size_t record_size = 0;
                    bool variable = false;
                    for (const PlyProperty& p : e.properties) {
                        if (p.is_list) {
                            variable = true;
                            break;
                        }
                        record_size += static_cast<std::size_t>(p.type->size);
                    }
                    if (!variable) {
                        std::vector<uint8_t> chunk(64u * 1024u);
                        std::size_t remaining = record_size * e.count;
                        while (remaining > 0) {
                            const std::size_t want = std::min(remaining, chunk.size());
                            if (!read_exact(chunk.data(), want)) {
                                if (error) *error = path + ": truncated element data";
                                return false;
                            }
                            remaining -= want;
                        }
                    } else {
                        for (std::size_t v2 = 0; v2 < e.count; ++v2) {
                            for (const PlyProperty& p : e.properties) {
                                if (!p.is_list) {
                                    uint8_t raw[8];
                                    if (!read_exact(raw, static_cast<std::size_t>(p.type->size))) {
                                        if (error) *error = path + ": truncated element data";
                                        return false;
                                    }
                                    continue;
                                }
                                uint8_t raw[8];
                                if (!read_exact(raw, static_cast<std::size_t>(p.count_type->size))) {
                                    if (error) *error = path + ": truncated element data";
                                    return false;
                                }
                                const std::size_t n =
                                    static_cast<std::size_t>(read_binary_scalar(raw, *p.count_type, file_big_endian));
                                buffer.assign(static_cast<std::size_t>(p.type->size), 0);
                                for (std::size_t k = 0; k < n; ++k) {
                                    if (!read_exact(buffer.data(), static_cast<std::size_t>(p.type->size))) {
                                        if (error) *error = path + ": truncated element data";
                                        return false;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if (out->xyz.empty()) {
        if (error) *error = path + ": no vertices";
        return false;
    }
    return true;
}

}  // namespace

bool read_ply(const std::string& path, Cloud* out, std::string* error) {
    return read_ply_impl(path, out, error);
}

bool read_record(const std::string& path, RecMode mode, int frame_index, Cloud* out, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    char header[16];
    in.read(header, 16);
    if (!in || std::memcmp(header, "OSCR", 4) != 0) {
        if (error) *error = path + ": not a recorded session (bad container magic)";
        return false;
    }
    uint32_t version = 0;
    std::memcpy(&version, header + 4, 4);
    if (version != 1) {
        if (error) *error = path + ": unsupported recorded-session version " + std::to_string(version);
        return false;
    }
    out->xyz.clear();
    std::size_t cloud_frames = 0;
    while (true) {
        char peek[4];
        in.read(peek, 4);
        const std::streamsize got = in.gcount();
        if (got == 0) break;
        if (got != 4) {
            if (error) *error = path + ": truncated frame header";
            return false;
        }
        if (std::memcmp(peek, "OSCE", 4) == 0) break;  // the recorder's terminator
        in.seekg(-4, std::ios::cur);
        std::vector<uint8_t> frame;
        frame.resize(32);
        in.read(reinterpret_cast<char*>(frame.data()), 32);
        if (!in) {
            if (error) *error = path + ": truncated frame header";
            return false;
        }
        uint32_t payload_len = 0;
        std::memcpy(&payload_len, frame.data() + 28, 4);
        if (payload_len > (64u * 1024 * 1024)) {
            if (error) *error = path + ": frame payload too large";
            return false;
        }
        frame.resize(32 + payload_len);
        if (payload_len > 0) {
            in.read(reinterpret_cast<char*>(frame.data() + 32), payload_len);
            if (!in) {
                if (error) *error = path + ": truncated frame payload";
                return false;
            }
        }
        uint16_t type = 0;
        std::memcpy(&type, frame.data() + 6, 2);
        if (type != static_cast<uint16_t>(openscanner::frame_protocol::MessageType::CloudSnapshot)) continue;
        uint32_t seq = 0, count = 0;
        double t_capture = 0.0, t_server = 0.0;
        const float* xyz = nullptr;
        if (!openscanner::frame_protocol::decode_cloud_snapshot(frame.data(), frame.size(), &seq, &t_capture,
                                                               &t_server, &xyz, &count)) {
            if (error) *error = path + ": invalid cloud frame";
            return false;
        }
        const bool keep = (mode == RecMode::kUnion) ||
                          (mode == RecMode::kLastFrame && cloud_frames == 0) ||
                          (mode == RecMode::kFrame && static_cast<int>(cloud_frames) == frame_index);
        if (keep) {
            out->xyz.insert(out->xyz.end(), xyz, xyz + static_cast<std::size_t>(count) * 3);
        }
        if (mode == RecMode::kLastFrame) {
            out->xyz.clear();
            out->xyz.insert(out->xyz.end(), xyz, xyz + static_cast<std::size_t>(count) * 3);
        }
        ++cloud_frames;
    }
    if (out->xyz.empty()) {
        if (error) *error = path + ": the recording carries no cloud frame";
        return false;
    }
    return true;
}

bool read_confidence_field(const std::string& path, std::size_t expected_points, std::vector<float>* out,
                           std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    out->assign(expected_points, 0.0f);
    std::vector<bool> seen(expected_points, false);
    std::string line;
    std::size_t line_no = 0;
    std::size_t next_index = 0;
    while (std::getline(in, line)) {
        ++line_no;
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        std::size_t index = next_index;
        std::string value_text = t;
        const std::size_t comma = t.find(',');
        if (comma != std::string::npos) {
            try {
                index = static_cast<std::size_t>(std::stoull(t.substr(0, comma)));
            } catch (const std::exception&) {
                if (error) *error = path + ":" + std::to_string(line_no) + ": not a point index";
                return false;
            }
            value_text = t.substr(comma + 1);
        }
        double value = 0.0;
        try {
            value = std::stod(value_text);
        } catch (const std::exception&) {
            if (error) *error = path + ":" + std::to_string(line_no) + ": not a number";
            return false;
        }
        if (index >= expected_points) {
            if (error) {
                *error = path + ":" + std::to_string(line_no) + ": point index " + std::to_string(index) +
                         " is outside the cloud (" + std::to_string(expected_points) + " points)";
            }
            return false;
        }
        (*out)[index] = static_cast<float>(value);
        seen[index] = true;
        ++next_index;
    }
    for (std::size_t i = 0; i < expected_points; ++i) {
        if (!seen[i]) {
            if (error) {
                *error = path + ": no value for point " + std::to_string(i);
            }
            return false;
        }
    }
    return true;
}

bool write_ply_ascii(const std::string& path, const Cloud& cloud, std::string* error) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    out << "ply\nformat ascii 1.0\nelement vertex " << cloud.size() << "\n";
    out << "property float x\nproperty float y\nproperty float z\nend_header\n";
    char buf[128];
    for (std::size_t i = 0; i < cloud.size(); ++i) {
        const float* p = cloud.point(i);
        std::snprintf(buf, sizeof(buf), "%.9g %.9g %.9g\n", static_cast<double>(p[0]), static_cast<double>(p[1]),
                      static_cast<double>(p[2]));
        out << buf;
    }
    if (!out) {
        if (error) *error = "the write of " + path + " failed";
        return false;
    }
    return true;
}

}  // namespace confidence
}  // namespace openscanner
