#pragma once
// The msgpack subset the daemon's wire uses (protocol-spec.md §1.1).
//
// This is the server side's own codec: the client (tools/raptor_client) has
// its own, and the two are deliberately independent so a shim reply and a
// client parse are two implementations of the spec, not one shared bug.
//
// Encoded forms: nil, bool, uint, int, str, bin, array. Anything else (maps,
// floats, ext, timestamps) is rejected rather than guessed: the observed
// method set never carries them.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace shim {

using Bytes = std::vector<uint8_t>;

struct Value {
    enum class Kind { kInvalid, kNil, kBool, kUInt, kInt, kStr, kBin, kArray };
    Kind kind = Kind::kInvalid;
    bool b = false;
    uint64_t u = 0;
    int64_t i = 0;
    std::string str;
    Bytes bin;
    std::vector<Value> array;

    bool is_array(size_t n) const { return kind == Kind::kArray && array.size() == n; }
};

// Parses one complete value from [p, p+n). Returns the bytes consumed, or 0
// when the value is incomplete or not in the subset.
size_t ParseValue(const uint8_t* p, size_t n, Value* out);

// A short shape for a log line: "array(11)", "bin(2304000)", "str(24)", ...
std::string Describe(const Value& v);

class Packer {
public:
    Bytes out;

    void nil() { out.push_back(0xc0); }
    void boolean(bool b) { out.push_back(b ? 0xc3 : 0xc2); }
    void uint(uint64_t v);
    void integer(int64_t v);
    void str(const std::string& s);
    void bin(const uint8_t* p, size_t n);
    void bin(const Bytes& b) { bin(b.data(), b.size()); }
    void array(uint32_t n);
    void raw(const Bytes& b) { out.insert(out.end(), b.begin(), b.end()); }
};

}  // namespace shim
