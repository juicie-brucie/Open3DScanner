#include "msgpack.h"

namespace shim {
namespace {

bool Need(size_t at, size_t want, size_t n) { return at <= n && n - at >= want; }

uint16_t Be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t Be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
uint64_t Be64(const uint8_t* p) { return ((uint64_t)Be32(p) << 32) | Be32(p + 4); }

size_t ParseAt(const uint8_t* p, size_t n, size_t at, Value* out) {
    if (at >= n) return 0;
    const uint8_t tag = p[at];
    if (tag <= 0x7f) {  // positive fixint
        out->kind = Value::Kind::kUInt;
        out->u = tag;
        return 1;
    }
    if (tag >= 0xe0) {  // negative fixint
        out->kind = Value::Kind::kInt;
        out->i = (int8_t)tag;
        return 1;
    }
    if (tag >= 0x90 && tag <= 0x9f) {  // fixarray
        const size_t count = tag & 0x0f;
        out->kind = Value::Kind::kArray;
        out->array.clear();
        size_t used = 1;
        for (size_t k = 0; k < count; ++k) {
            Value item;
            const size_t m = ParseAt(p, n, at + used, &item);
            if (!m) return 0;
            out->array.push_back(std::move(item));
            used += m;
        }
        return used;
    }
    if (tag >= 0xa0 && tag <= 0xbf) {  // fixstr
        const size_t len = tag & 0x1f;
        if (!Need(at + 1, len, n)) return 0;
        out->kind = Value::Kind::kStr;
        out->str.assign(reinterpret_cast<const char*>(p + at + 1), len);
        return 1 + len;
    }
    switch (tag) {
        case 0xc0:
            out->kind = Value::Kind::kNil;
            return 1;
        case 0xc2:
        case 0xc3:
            out->kind = Value::Kind::kBool;
            out->b = tag == 0xc3;
            return 1;
        case 0xc4: {  // bin8
            if (!Need(at + 1, 1, n)) return 0;
            const size_t len = p[at + 1];
            if (!Need(at + 2, len, n)) return 0;
            out->kind = Value::Kind::kBin;
            out->bin.assign(p + at + 2, p + at + 2 + len);
            return 2 + len;
        }
        case 0xc5: {  // bin16
            if (!Need(at + 1, 2, n)) return 0;
            const size_t len = Be16(p + at + 1);
            if (!Need(at + 3, len, n)) return 0;
            out->kind = Value::Kind::kBin;
            out->bin.assign(p + at + 3, p + at + 3 + len);
            return 3 + len;
        }
        case 0xc6: {  // bin32
            if (!Need(at + 1, 4, n)) return 0;
            const size_t len = Be32(p + at + 1);
            if (!Need(at + 5, len, n)) return 0;
            out->kind = Value::Kind::kBin;
            out->bin.assign(p + at + 5, p + at + 5 + len);
            return 5 + len;
        }
        case 0xcc:
            if (!Need(at + 1, 1, n)) return 0;
            out->kind = Value::Kind::kUInt;
            out->u = p[at + 1];
            return 2;
        case 0xcd:
            if (!Need(at + 1, 2, n)) return 0;
            out->kind = Value::Kind::kUInt;
            out->u = Be16(p + at + 1);
            return 3;
        case 0xce:
            if (!Need(at + 1, 4, n)) return 0;
            out->kind = Value::Kind::kUInt;
            out->u = Be32(p + at + 1);
            return 5;
        case 0xcf:
            if (!Need(at + 1, 8, n)) return 0;
            out->kind = Value::Kind::kUInt;
            out->u = Be64(p + at + 1);
            return 9;
        case 0xd0:
            if (!Need(at + 1, 1, n)) return 0;
            out->kind = Value::Kind::kInt;
            out->i = (int8_t)p[at + 1];
            return 2;
        case 0xd1:
            if (!Need(at + 1, 2, n)) return 0;
            out->kind = Value::Kind::kInt;
            out->i = (int16_t)Be16(p + at + 1);
            return 3;
        case 0xd2:
            if (!Need(at + 1, 4, n)) return 0;
            out->kind = Value::Kind::kInt;
            out->i = (int32_t)Be32(p + at + 1);
            return 5;
        case 0xd3:
            if (!Need(at + 1, 8, n)) return 0;
            out->kind = Value::Kind::kInt;
            out->i = (int64_t)Be64(p + at + 1);
            return 9;
        case 0xd9: {  // str8
            if (!Need(at + 1, 1, n)) return 0;
            const size_t len = p[at + 1];
            if (!Need(at + 2, len, n)) return 0;
            out->kind = Value::Kind::kStr;
            out->str.assign(reinterpret_cast<const char*>(p + at + 2), len);
            return 2 + len;
        }
        case 0xda: {  // str16
            if (!Need(at + 1, 2, n)) return 0;
            const size_t len = Be16(p + at + 1);
            if (!Need(at + 3, len, n)) return 0;
            out->kind = Value::Kind::kStr;
            out->str.assign(reinterpret_cast<const char*>(p + at + 3), len);
            return 3 + len;
        }
        case 0xdb: {  // str32
            if (!Need(at + 1, 4, n)) return 0;
            const size_t len = Be32(p + at + 1);
            if (!Need(at + 5, len, n)) return 0;
            out->kind = Value::Kind::kStr;
            out->str.assign(reinterpret_cast<const char*>(p + at + 5), len);
            return 5 + len;
        }
        case 0xdc: {  // array16
            if (!Need(at + 1, 2, n)) return 0;
            const size_t count = Be16(p + at + 1);
            out->kind = Value::Kind::kArray;
            out->array.clear();
            size_t used = 3;
            for (size_t k = 0; k < count; ++k) {
                Value item;
                const size_t m = ParseAt(p, n, at + used, &item);
                if (!m) return 0;
                out->array.push_back(std::move(item));
                used += m;
            }
            return used;
        }
        case 0xdd: {  // array32
            if (!Need(at + 1, 4, n)) return 0;
            const size_t count = Be32(p + at + 1);
            out->kind = Value::Kind::kArray;
            out->array.clear();
            size_t used = 5;
            for (size_t k = 0; k < count; ++k) {
                Value item;
                const size_t m = ParseAt(p, n, at + used, &item);
                if (!m) return 0;
                out->array.push_back(std::move(item));
                used += m;
            }
            return used;
        }
        default:
            return 0;  // maps, floats, ext: not in this wire subset
    }
}

}  // namespace

size_t ParseValue(const uint8_t* p, size_t n, Value* out) {
    if (!out) return 0;
    *out = Value();
    return ParseAt(p, n, 0, out);
}

std::string Describe(const Value& v) {
    switch (v.kind) {
        case Value::Kind::kNil:
            return "nil";
        case Value::Kind::kBool:
            return v.b ? "true" : "false";
        case Value::Kind::kUInt:
            return "uint(" + std::to_string(v.u) + ")";
        case Value::Kind::kInt:
            return "int(" + std::to_string(v.i) + ")";
        case Value::Kind::kStr:
            return "str(" + std::to_string(v.str.size()) + ")";
        case Value::Kind::kBin:
            return "bin(" + std::to_string(v.bin.size()) + ")";
        case Value::Kind::kArray:
            return "array(" + std::to_string(v.array.size()) + ")";
        case Value::Kind::kInvalid:
            break;
    }
    return "invalid";
}

void Packer::uint(uint64_t v) {
    if (v < 0x80) {
        out.push_back((uint8_t)v);
    } else if (v <= 0xff) {
        out.push_back(0xcc);
        out.push_back((uint8_t)v);
    } else if (v <= 0xffff) {
        out.push_back(0xcd);
        out.push_back((uint8_t)(v >> 8));
        out.push_back((uint8_t)v);
    } else if (v <= 0xffffffffULL) {
        out.push_back(0xce);
        for (int shift = 24; shift >= 0; shift -= 8) out.push_back((uint8_t)(v >> shift));
    } else {
        out.push_back(0xcf);
        for (int shift = 56; shift >= 0; shift -= 8) out.push_back((uint8_t)(v >> shift));
    }
}

void Packer::integer(int64_t v) {
    if (v >= 0) {
        uint((uint64_t)v);
        return;
    }
    if (v >= -32) {
        out.push_back((uint8_t)(0xe0 | (v + 32)));
    } else if (v >= -128) {
        out.push_back(0xd0);
        out.push_back((uint8_t)(int8_t)v);
    } else if (v >= -32768) {
        out.push_back(0xd1);
        const uint16_t u = (uint16_t)(int16_t)v;
        out.push_back((uint8_t)(u >> 8));
        out.push_back((uint8_t)u);
    } else if (v >= -2147483648LL) {
        out.push_back(0xd2);
        const uint32_t u = (uint32_t)(int32_t)v;
        for (int shift = 24; shift >= 0; shift -= 8) out.push_back((uint8_t)(u >> shift));
    } else {
        out.push_back(0xd3);
        const uint64_t u = (uint64_t)v;
        for (int shift = 56; shift >= 0; shift -= 8) out.push_back((uint8_t)(u >> shift));
    }
}

void Packer::str(const std::string& s) {
    const size_t n = s.size();
    if (n <= 31) {
        out.push_back((uint8_t)(0xa0 | n));
    } else if (n <= 0xff) {
        out.push_back(0xd9);
        out.push_back((uint8_t)n);
    } else if (n <= 0xffff) {
        out.push_back(0xda);
        out.push_back((uint8_t)(n >> 8));
        out.push_back((uint8_t)n);
    } else {
        out.push_back(0xdb);
        for (int shift = 24; shift >= 0; shift -= 8) out.push_back((uint8_t)((uint32_t)n >> shift));
    }
    out.insert(out.end(), s.begin(), s.end());
}

void Packer::bin(const uint8_t* p, size_t n) {
    if (n <= 0xff) {
        out.push_back(0xc4);
        out.push_back((uint8_t)n);
    } else if (n <= 0xffff) {
        out.push_back(0xc5);
        out.push_back((uint8_t)(n >> 8));
        out.push_back((uint8_t)n);
    } else {
        out.push_back(0xc6);
        for (int shift = 24; shift >= 0; shift -= 8) out.push_back((uint8_t)((uint32_t)n >> shift));
    }
    if (p && n) out.insert(out.end(), p, p + n);
}

void Packer::array(uint32_t n) {
    if (n <= 15) {
        out.push_back((uint8_t)(0x90 | n));
    } else if (n <= 0xffff) {
        out.push_back(0xdc);
        out.push_back((uint8_t)(n >> 8));
        out.push_back((uint8_t)n);
    } else {
        out.push_back(0xdd);
        for (int shift = 24; shift >= 0; shift -= 8) out.push_back((uint8_t)(n >> shift));
    }
}

}  // namespace shim
