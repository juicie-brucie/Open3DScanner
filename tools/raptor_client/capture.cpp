// capture — the reply recorder for raptor_client (row #140).
// The format is documented in capture.h.

#include "capture.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

namespace capture {
namespace {

// The file header's magic and the format version.
const char kMagic[4] = {'R', 'C', 'R', 'P'};
const uint32_t kFormatVersion = 1;

uint32_t Rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

void PutU16(std::string* s, uint16_t v) {
    s->push_back(static_cast<char>(v & 0xff));
    s->push_back(static_cast<char>((v >> 8) & 0xff));
}

void PutU32(std::string* s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s->push_back(static_cast<char>((v >> (i * 8)) & 0xff));
}

void PutU64(std::string* s, uint64_t v) {
    for (int i = 0; i < 8; ++i) s->push_back(static_cast<char>((v >> (i * 8)) & 0xff));
}

uint16_t GetU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

uint32_t GetU32(const uint8_t* p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (i * 8);
    return v;
}

uint64_t GetU64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (i * 8);
    return v;
}

// A JSON string, quoted and escaped.
std::string JsonString(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += "\"";
    return out;
}

// The whole file, or "" when it cannot be read.
std::string ReadWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The directory's entries, sorted.
std::vector<std::string> ListDir(const std::string& dir) {
    std::vector<std::string> names;
    DIR* d = opendir(dir.c_str());
    if (!d) return names;
    while (struct dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        names.push_back(name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    return names;
}

// Create a directory and its parents.
bool MakeDirs(const std::string& path) {
    if (path.empty()) return false;
    std::string acc;
    size_t i = 0;
    if (path[0] == '/') {
        acc = "/";
        i = 1;
    }
    while (i <= path.size()) {
        size_t slash = path.find('/', i);
        std::string part = path.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
        if (!part.empty()) {
            if (!acc.empty() && acc.back() != '/') acc += '/';
            acc += part;
            struct stat st;
            if (stat(acc.c_str(), &st) != 0) {
                if (mkdir(acc.c_str(), 0755) != 0 && errno != EEXIST) return false;
            } else if (!S_ISDIR(st.st_mode)) {
                return false;
            }
        }
        if (slash == std::string::npos) break;
        i = slash + 1;
    }
    return true;
}

// The absolute path of a (possibly relative) one, with symlinks resolved.
//
// A bundle must not land inside the repository, and a symlink is a way to
// aim one there, so the path is resolved before it is judged. A path that
// does not exist yet (the directory the recorder is about to create) is
// resolved through its deepest existing ancestor.
std::string AbsolutePath(const std::string& path) {
    if (path.empty()) return path;
    char resolved[4096];
    if (realpath(path.c_str(), resolved)) return resolved;

    std::string abs = path;
    if (abs[0] != '/') {
        char cwd[4096];
        if (!getcwd(cwd, sizeof(cwd))) return abs;
        abs = std::string(cwd) + "/" + abs;
    }
    // The components, from the root down.
    std::vector<std::string> parts;
    size_t i = 1;
    while (i <= abs.size()) {
        size_t slash = abs.find('/', i);
        std::string part = abs.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
        if (!part.empty()) parts.push_back(part);
        if (slash == std::string::npos) break;
        i = slash + 1;
    }
    // The longest existing prefix.
    std::string acc = "/";
    size_t existing = 0;
    for (size_t k = 0; k < parts.size(); ++k) {
        const std::string candidate = acc == "/" ? "/" + parts[k] : acc + "/" + parts[k];
        struct stat st;
        if (stat(candidate.c_str(), &st) != 0) break;
        acc = candidate;
        existing = k + 1;
    }
    // Resolve it — it may be a symlink — and re-attach the rest.
    std::string head = realpath(acc.c_str(), resolved) ? std::string(resolved) : acc;
    for (size_t k = existing; k < parts.size(); ++k) head += "/" + parts[k];
    return head;
}

// The directory holding the .git entry above start_dir, or "".
std::string FindGitRoot(const std::string& start_dir) {
    std::string dir = AbsolutePath(start_dir);
    while (true) {
        struct stat st;
        if (stat((dir + "/.git").c_str(), &st) == 0) return dir;
        size_t slash = dir.find_last_of('/');
        if (slash == std::string::npos) return "";
        if (slash == 0) {
            if (stat("/.git", &st) == 0) return "/";
            return "";
        }
        dir = dir.substr(0, slash);
    }
}

// The sha of a 40-hex-character commit id.
bool IsHex40(const std::string& s) {
    if (s.size() != 40) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4)
// ---------------------------------------------------------------------------
namespace {
const uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
}  // namespace

void Sha256::Reset() {
    h_[0] = 0x6a09e667;
    h_[1] = 0xbb67ae85;
    h_[2] = 0x3c6ef372;
    h_[3] = 0xa54ff53a;
    h_[4] = 0x510e527f;
    h_[5] = 0x9b05688c;
    h_[6] = 0x1f83d9ab;
    h_[7] = 0x5be0cd19;
    buf_len_ = 0;
    total_ = 0;
}

void Sha256::Block(const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) |
               (uint32_t)p[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = hh + S1 + ch + kK[i] + w[i];
        uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += hh;
}

void Sha256::Update(const uint8_t* data, size_t n) {
    total_ += n;
    while (n > 0) {
        size_t take = 64 - buf_len_;
        if (take > n) take = n;
        std::memcpy(buf_ + buf_len_, data, take);
        buf_len_ += take;
        data += take;
        n -= take;
        if (buf_len_ == 64) {
            Block(buf_);
            buf_len_ = 0;
        }
    }
}

void Sha256::Update(const std::string& s) {
    Update(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

std::string Sha256::HexDigest() {
    uint64_t bits = total_ * 8;
    uint8_t pad = 0x80;
    Update(&pad, 1);
    uint8_t zero = 0;
    while (buf_len_ != 56) Update(&zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; ++i) len[i] = (uint8_t)((bits >> ((7 - i) * 8)) & 0xff);
    // The length must not be counted in the message length, so write it
    // straight into the buffer.
    std::memcpy(buf_ + buf_len_, len, 8);
    Block(buf_);
    buf_len_ = 0;
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int i = 0; i < 8; ++i) {
        for (int b = 3; b >= 0; --b) {
            uint8_t byte = (uint8_t)((h_[i] >> (b * 8)) & 0xff);
            out += hex[byte >> 4];
            out += hex[byte & 0xf];
        }
    }
    return out;
}

std::string BytesSha256(const uint8_t* data, size_t n) {
    Sha256 h;
    h.Update(data, n);
    return h.HexDigest();
}

std::string FileSha256(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    Sha256 h;
    uint8_t buf[65536];
    while (in) {
        in.read(reinterpret_cast<char*>(buf), sizeof(buf));
        std::streamsize got = in.gcount();
        if (got > 0) h.Update(buf, (size_t)got);
    }
    return h.HexDigest();
}

// ---------------------------------------------------------------------------
// The recorder
// ---------------------------------------------------------------------------
Recorder::Recorder() {}
Recorder::~Recorder() { Close("unknown", "unknown"); }

bool Recorder::Open(const std::string& dir, const std::string& binary_path, int seconds_cap, uint64_t bytes_cap,
                    std::string* err) {
    failed_ = false;
    error_.clear();
    if (dir.empty()) {
        if (err) *err = "the capture directory is empty";
        return false;
    }
    // The bundle is evidence for the hardware room and is never committed,
    // so a directory inside a repository is refused. The path is resolved
    // first: a symlink is a way to aim a bundle at one.
    const std::string resolved = AbsolutePath(dir);
    if (IsInsideRepo(resolved)) {
        if (err) {
            *err = "the capture directory is inside a git repository (" + resolved +
                   "); the bundle is never committed — choose a directory outside the repository";
        }
        return false;
    }
    std::string replies = resolved + "/replies";
    if (!MakeDirs(replies)) {
        if (err) *err = "cannot create the capture directory " + replies;
        return false;
    }
    // An existing directory that cannot be written is not a capture. Probe
    // it at startup, so the run says so before the first blocking call
    // instead of discovering it at the first reply.
    const std::string probe = replies + "/.write-probe";
    {
        std::ofstream p(probe, std::ios::binary | std::ios::trunc);
        bool ok = static_cast<bool>(p);
        if (ok) {
            p.put('\0');
            p.flush();
            ok = p.good();
            p.close();
            ok = ok && !p.fail();
        }
        unlink(probe.c_str());
        if (!ok) {
            if (err) *err = "the capture directory is not writable: " + replies;
            return false;
        }
    }
    dir_ = resolved;
    binary_path_ = binary_path;
    seconds_cap_ = seconds_cap;
    bytes_cap_ = bytes_cap;
    start_ms_ = NowMs();
    end_ms_ = start_ms_;
    open_ = true;
    return true;
}

void Recorder::Record(uint16_t port, const std::string& method, uint64_t msgid,
                      const std::vector<uint8_t>& bytes) {
    if (!open_ || failed_ || capped_) return;
    if (seconds_cap_ > 0 && NowMs() - start_ms_ >= (uint64_t)seconds_cap_ * 1000) {
        capped_seconds_ = true;
        capped_ = true;
        return;
    }
    if (method.size() > 0xffff) return;  // a method name that long is not a method name
    uint64_t body_len = 8 + 2 + 2 + (uint64_t)method.size() + 8 + 8 + (uint64_t)bytes.size();
    if (body_len > 0xffffffffull) return;
    if (bytes_cap_ > 0 && bytes_written_ + 4 + body_len > bytes_cap_) {
        capped_bytes_ = true;
        capped_ = true;
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    PortFile& pf = files_[port];
    if (!pf.out) {
        const std::string path = dir_ + "/replies/port-" + std::to_string(port) + ".bin";
        pf.out.reset(new std::ofstream(path, std::ios::binary | std::ios::trunc));
        if (!pf.out || !pf.out->good()) {
            pf.out.reset();
            Fail("cannot open the reply file " + path);
            return;
        }
        std::string header;
        header.append(kMagic, 4);
        PutU32(&header, kFormatVersion);
        PutU16(&header, port);
        PutU16(&header, 0);
        pf.out->write(header.data(), (std::streamsize)header.size());
    }
    std::string body;
    PutU64(&body, NowMs());
    PutU16(&body, port);
    PutU16(&body, (uint16_t)method.size());
    body.append(method);
    PutU64(&body, msgid);
    PutU64(&body, (uint64_t)bytes.size());
    body.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());

    std::string prefix;
    PutU32(&prefix, (uint32_t)body.size());
    pf.out->write(prefix.data(), (std::streamsize)prefix.size());
    pf.out->write(body.data(), (std::streamsize)body.size());
    if (!pf.out->good()) {
        Fail("a write to " + dir_ + "/replies/port-" + std::to_string(port) + ".bin failed (the disk may be full)");
        return;
    }
    pf.records++;
    records_++;
    bytes_written_ += prefix.size() + body.size();
}

void Recorder::NoteReadTimeout(uint16_t port, const std::string& method, int timeout_ms) {
    if (failed_) return;
    std::string why = "no complete reply on port " + std::to_string(port) + " for " + method;
    if (timeout_ms > 0) why += " within " + std::to_string(timeout_ms) + " ms";
    why += "; the capture stopped answering";
    Fail(why);
}

void Recorder::Fail(const std::string& why) {
    if (failed_) return;
    failed_ = true;
    error_ = why;
    std::fprintf(stderr, "[capture] FAILED: %s\n", why.c_str());
    std::fflush(stderr);
}

bool Recorder::SecondsBudgetExpired() const {
    return open_ && seconds_cap_ > 0 && NowMs() - start_ms_ >= (uint64_t)seconds_cap_ * 1000;
}

std::string Recorder::Close(const std::string& branch, const std::string& commit) {
    if (!open_) return "";
    end_ms_ = NowMs();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& kv : files_) {
            if (kv.second.out) {
                kv.second.out->flush();
                kv.second.out->close();
                kv.second.out.reset();
            }
        }
    }
    open_ = false;

    // The manifest: the run's identity, and every written file's hash.
    std::string json = "{\n";
    json += "  \"tool\": \"raptor_client\",\n";
    json += "  \"format\": \"RCRP\",\n";
    json += "  \"format_version\": " + std::to_string(kFormatVersion) + ",\n";
    json += "  \"capture_dir\": " + JsonString(dir_) + ",\n";
    json += "  \"run_start_unix_ms\": " + std::to_string(start_ms_) + ",\n";
    json += "  \"run_end_unix_ms\": " + std::to_string(end_ms_) + ",\n";
    json += "  \"run_start_utc\": " + JsonString(UtcStamp(start_ms_)) + ",\n";
    json += "  \"run_end_utc\": " + JsonString(UtcStamp(end_ms_)) + ",\n";
    json += "  \"branch\": " + JsonString(branch) + ",\n";
    json += "  \"commit\": " + JsonString(commit) + ",\n";
    json += "  \"binary_path\": " + JsonString(binary_path_) + ",\n";
    json += "  \"binary_sha256\": " + JsonString(FileSha256(binary_path_)) + ",\n";
    json += "  \"limits\": {\"seconds\": " + std::to_string(seconds_cap_) + ", \"bytes\": " +
            std::to_string(bytes_cap_) + "},\n";
    json += "  \"limits_hit\": {\"seconds\": " + std::string(capped_seconds_ ? "true" : "false") +
            ", \"bytes\": " + std::string(capped_bytes_ ? "true" : "false") + "},\n";
    json += "  \"record_count\": " + std::to_string(records_) + ",\n";
    json += "  \"bytes_written\": " + std::to_string(bytes_written_) + ",\n";
    json += "  \"files\": [\n";
    bool first = true;
    for (const auto& kv : files_) {
        const std::string rel = "replies/port-" + std::to_string(kv.first) + ".bin";
        const std::string abs = dir_ + "/" + rel;
        struct stat st;
        uint64_t size = 0;
        if (stat(abs.c_str(), &st) == 0) size = (uint64_t)st.st_size;
        if (!first) json += ",\n";
        first = false;
        json += "    {\"path\": " + JsonString(rel) + ", \"port\": " + std::to_string(kv.first) +
                ", \"size\": " + std::to_string(size) + ", \"sha256\": " + JsonString(FileSha256(abs)) +
                ", \"records\": " + std::to_string(kv.second.records) + "}";
    }
    json += "\n  ]\n}\n";

    const std::string manifest = dir_ + "/manifest.json";
    std::ofstream out(manifest, std::ios::binary | std::ios::trunc);
    bool wrote = static_cast<bool>(out);
    if (wrote) out.write(json.data(), (std::streamsize)json.size());
    out.close();
    wrote = wrote && !out.fail();
    if (!wrote) {
        Fail("cannot write the manifest " + manifest);
        return "";
    }
    return manifest;
}

// ---------------------------------------------------------------------------
// The reader
// ---------------------------------------------------------------------------
bool Reader::Open(const std::string& dir, std::string* err) {
    dir_ = AbsolutePath(dir);
    const std::string replies = dir_ + "/replies";
    std::vector<std::string> names = ListDir(replies);
    for (const std::string& n : names) {
        if (n.size() > 8 && n.compare(0, 5, "port-") == 0 && n.compare(n.size() - 4, 4, ".bin") == 0) {
            files_.push_back(n);
        }
    }
    if (files_.empty()) {
        error_ = "no replies/port-*.bin under " + dir_;
        if (err) *err = error_;
        return false;
    }
    file_index_ = 0;
    done_ = false;
    return OpenNext();
}

bool Reader::OpenNext() {
    if (file_index_ >= files_.size()) {
        done_ = true;
        return false;
    }
    return OpenFile(dir_ + "/replies/" + files_[file_index_++]);
}

bool Reader::OpenFile(const std::string& path) {
    in_.close();
    in_.clear();
    in_.open(path, std::ios::binary);
    if (!in_) {
        error_ = "cannot open " + path;
        return false;
    }
    uint8_t header[12];
    in_.read(reinterpret_cast<char*>(header), sizeof(header));
    if (in_.gcount() != (std::streamsize)sizeof(header) || std::memcmp(header, kMagic, 4) != 0) {
        error_ = path + ": not a capture file";
        in_.close();
        return false;
    }
    if (GetU32(header + 4) != kFormatVersion) {
        error_ = path + ": unsupported capture format version";
        in_.close();
        return false;
    }
    port_ = GetU16(header + 8);
    return true;
}

bool Reader::Next(Record* out) {
    while (true) {
        if (done_) return false;
        if (!in_.is_open()) {
            if (!OpenNext()) return false;
        }
        uint8_t lenbuf[4];
        in_.read(reinterpret_cast<char*>(lenbuf), sizeof(lenbuf));
        if (in_.gcount() != (std::streamsize)sizeof(lenbuf)) {
            if (in_.eof()) {
                // This file is done; the next one starts.
                in_.close();
                if (OpenNext()) continue;
                done_ = true;
                return false;
            }
            error_ = "a truncated record length";
            return false;
        }
        uint32_t body_len = GetU32(lenbuf);
        if (body_len < 28 || body_len > (1u << 26)) {
            error_ = "an implausible record length";
            return false;
        }
        std::vector<uint8_t> body(body_len);
        in_.read(reinterpret_cast<char*>(body.data()), body_len);
        if (in_.gcount() != (std::streamsize)body_len) {
            error_ = "a truncated record body";
            return false;
        }
        const uint8_t* p = body.data();
        Record r;
        r.ts_ms = GetU64(p);
        p += 8;
        r.port = GetU16(p);
        p += 2;
        uint16_t method_len = GetU16(p);
        p += 2;
        if (method_len > body_len) {
            error_ = "a record with an implausible method length";
            return false;
        }
        r.method.assign(reinterpret_cast<const char*>(p), method_len);
        p += method_len;
        r.msgid = GetU64(p);
        p += 8;
        uint64_t byte_len = GetU64(p);
        p += 8;
        if (byte_len > body_len) {
            error_ = "a record with an implausible byte length";
            return false;
        }
        r.bytes.assign(p, p + byte_len);
        *out = r;
        return true;
    }
}

// ---------------------------------------------------------------------------
// The repository
// ---------------------------------------------------------------------------
bool IsInsideRepo(const std::string& path) {
    std::string dir = AbsolutePath(path);
    while (true) {
        struct stat st;
        if (stat((dir + "/.git").c_str(), &st) == 0) return true;
        size_t slash = dir.find_last_of('/');
        if (slash == std::string::npos) return false;
        if (slash == 0) return stat("/.git", &st) == 0;
        dir = dir.substr(0, slash);
    }
}

void ReadGitBranchAndCommit(const std::string& start_dir, std::string* branch, std::string* commit) {    *branch = "unknown";
    *commit = "unknown";
    std::string root = FindGitRoot(start_dir);
    if (root.empty()) return;

    std::string git_dir;
    struct stat st;
    if (stat((root + "/.git").c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            git_dir = root + "/.git";
        } else {
            // A worktree: the .git file names the git directory.
            std::string text = ReadWholeFile(root + "/.git");
            size_t colon = text.find(':');
            if (colon == std::string::npos) return;
            std::string p = text.substr(colon + 1);
            while (!p.empty() && (p[0] == ' ' || p[0] == '\n' || p[0] == '\r')) p.erase(0, 1);
            while (!p.empty() && (p.back() == '\n' || p.back() == '\r' || p.back() == ' ')) p.pop_back();
            if (p.empty()) return;
            git_dir = (p[0] == '/') ? p : root + "/" + p;
        }
    } else {
        return;
    }

    // The common directory holds the refs; a worktree's git dir points at it.
    std::string common = git_dir;
    std::string commondir = ReadWholeFile(git_dir + "/commondir");
    while (!commondir.empty() && (commondir.back() == '\n' || commondir.back() == '\r' || commondir.back() == ' ')) {
        commondir.pop_back();
    }
    if (!commondir.empty()) {
        common = (commondir[0] == '/') ? commondir : git_dir + "/" + commondir;
    }

    std::string head = ReadWholeFile(git_dir + "/HEAD");
    while (!head.empty() && (head.back() == '\n' || head.back() == '\r' || head.back() == ' ')) head.pop_back();
    if (head.empty()) return;
    if (head.compare(0, 5, "ref: ") == 0) {
        std::string ref = head.substr(5);
        // The branch is the ref without its "refs/heads/" prefix; the
        // manifest names the branch as the user knows it.
        const std::string heads = "refs/heads/";
        if (ref.compare(0, heads.size(), heads) == 0) {
            *branch = ref.substr(heads.size());
        } else {
            *branch = ref.substr(ref.find_last_of('/') + 1);
        }
        std::string sha = ReadWholeFile(common + "/" + ref);
        while (!sha.empty() && (sha.back() == '\n' || sha.back() == '\r' || sha.back() == ' ')) sha.pop_back();
        if (!IsHex40(sha)) {
            // The ref may live in the packed-refs file.
            std::string packed = ReadWholeFile(common + "/packed-refs");
            std::istringstream ps(packed);
            std::string line;
            while (std::getline(ps, line)) {
                if (line.empty() || line[0] == '#' || line[0] == '^') continue;
                size_t sp = line.find(' ');
                if (sp == std::string::npos) continue;
                if (line.substr(sp + 1) == ref) {
                    sha = line.substr(0, sp);
                    break;
                }
            }
        }
        if (IsHex40(sha)) *commit = sha;
    } else if (IsHex40(head)) {
        *commit = head;
        *branch = "HEAD (detached)";
    }
}

// ---------------------------------------------------------------------------
// The clock
// ---------------------------------------------------------------------------
void RemoveTree(const std::string& path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR* d = opendir(path.c_str());
        if (d) {
            while (struct dirent* e = readdir(d)) {
                std::string name = e->d_name;
                if (name == "." || name == "..") continue;
                RemoveTree(path + "/" + name);
            }
            closedir(d);
        }
        rmdir(path.c_str());
        return;
    }
    unlink(path.c_str());
}

uint64_t NowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000);
}

std::string UtcStamp(uint64_t ms) {
    time_t secs = (time_t)(ms / 1000);
    struct tm tm_utc;
    gmtime_r(&secs, &tm_utc);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    return std::string(buf);
}

}  // namespace capture
