#pragma once
// The flat-JSON field readers the control commands use.
//
// Only flat {"key": value, ...} objects with string/number/bool leaves are
// supported - nothing here ever nests. (Base64 for the same reason.)
//
// Moved out of main.cpp so the control-message path can be exercised without
// a device (row #158): the readers are shared by the command handler and by
// the plane-removal command parser.

#include <cctype>
#include <cstdlib>
#include <string>

namespace openscanner {
namespace controljson {

// Finds "key": <value> in `json` and returns the raw, unparsed value text
// (quotes kept on if it was a string). false if the key isn't present.
inline bool FindJsonValue(const std::string& json, const std::string& key, std::string* out_value) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos += needle.size();
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    size_t end = pos;
    if (end < json.size() && json[end] == '"') {
        end = json.find('"', end + 1);
        if (end == std::string::npos) return false;
        *out_value = json.substr(pos + 1, end - pos - 1);
        return true;
    }
    while (end < json.size() && json[end] != ',' && json[end] != '}') ++end;
    *out_value = json.substr(pos, end - pos);
    // Trim trailing whitespace left over from the scan above.
    while (!out_value->empty() && std::isspace(static_cast<unsigned char>(out_value->back()))) {
        out_value->pop_back();
    }
    return true;
}

inline bool FindJsonBool(const std::string& json, const std::string& key, bool* out_value) {
    std::string raw;
    if (!FindJsonValue(json, key, &raw)) return false;
    if (raw == "true") {
        *out_value = true;
        return true;
    }
    if (raw == "false") {
        *out_value = false;
        return true;
    }
    return false;
}

inline bool FindJsonInt(const std::string& json, const std::string& key, int* out_value) {
    std::string raw;
    if (!FindJsonValue(json, key, &raw)) return false;
    if (raw.empty()) return false;
    *out_value = std::atoi(raw.c_str());
    return true;
}

}  // namespace controljson
}  // namespace openscanner
