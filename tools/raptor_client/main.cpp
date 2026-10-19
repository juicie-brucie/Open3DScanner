// raptor_client — Phase B (C1): our own msgpack-RPC + GVCP client for the
// Creality RPCServer daemon. No OBScan, no libOrbbecSDK, no obscan_* calls.
//
// Implements docs/recon/findings/protocol-spec.md (row #96 / G-A):
//   - GVCP discovery on UDP 3956 (DISCOVERY_CMD)
//   - msgpack-RPC over TCP (rpclib framing), method names with the
//     <device><interface> suffix, ports 10010/10011/10012/10013
//   - the vendor-command framing inside ob_rpc_send_data ("MG"/"BR",
//     8-byte ReqHeader, SET_PROPERTY opcode 2)
//
// Usage:
//   raptor_client --dry-run                  # framing selftest, no device
//   raptor_client --dry-run-live             # bytes each call would send
//   raptor_client --discover                 # GVCP discovery only
//   raptor_client --replay <file> [host] [port] [--dry-run] [--quiet]
//       recon's captured request frames, unchanged, in order (hex lines)
//   raptor_client --live <host> [port] [seconds] [out.raw] [--pull-ms N] [--quiet]
//       per-call logging is on by default: every get_frame result (bin size /
//       empty / error) and every SET_PROPERTY (prop, value, ACCEPT/REJECT).
//
// Wall: tools/** only. Clean-room: implements the spec; no vendor binary.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------------------
// Minimal msgpack (the subset the daemon's wire uses)
// ---------------------------------------------------------------------------

class Packer {
public:
    Bytes out;
    void u8(uint8_t v) { out.push_back(v); }
    void raw(const uint8_t* p, size_t n) { out.insert(out.end(), p, p + n); }
    void nil() { u8(0xc0); }
    void boolean(bool b) { u8(b ? 0xc3 : 0xc2); }
    void uint(uint64_t v) {
        if (v < 0x80) { u8(static_cast<uint8_t>(v)); }
        else if (v <= 0xff) { u8(0xcc); u8(static_cast<uint8_t>(v)); }
        else if (v <= 0xffff) { u8(0xcd); be16(static_cast<uint16_t>(v)); }
        else if (v <= 0xffffffffULL) { u8(0xce); be32(static_cast<uint32_t>(v)); }
        else { u8(0xcf); be64(v); }
    }
    void integer(int64_t v) {
        if (v >= 0) { uint(static_cast<uint64_t>(v)); return; }
        if (v >= -32) { u8(static_cast<uint8_t>(0xe0 | (v + 32))); }
        else if (v >= -128) { u8(0xd0); u8(static_cast<uint8_t>(v)); }
        else if (v >= -32768) { u8(0xd1); be16(static_cast<uint16_t>(v)); }
        else if (v >= -2147483648LL) { u8(0xd2); be32(static_cast<uint32_t>(v)); }
        else { u8(0xd3); be64(static_cast<uint64_t>(v)); }
    }
    void float64(double v) { u8(0xcb); uint64_t u; std::memcpy(&u, &v, 8); be64(u); }
    void str(const std::string& s) {
        size_t n = s.size();
        if (n < 32) u8(static_cast<uint8_t>(0xa0 | n));
        else if (n <= 0xff) { u8(0xd9); u8(static_cast<uint8_t>(n)); }
        else { u8(0xda); be16(static_cast<uint16_t>(n)); }
        raw(reinterpret_cast<const uint8_t*>(s.data()), n);
    }
    void bin(const Bytes& b) {
        size_t n = b.size();
        u8(0xc4); u8(static_cast<uint8_t>(n));
        raw(b.data(), n);
    }
    void array(uint32_t n) {
        if (n < 16) u8(static_cast<uint8_t>(0x90 | n));
        else if (n <= 0xffff) { u8(0xdc); be16(static_cast<uint16_t>(n)); }
        else { u8(0xdd); be32(n); }
    }
private:
    void be16(uint16_t v) { u8(v >> 8); u8(v & 0xff); }
    void be32(uint32_t v) { for (int i = 3; i >= 0; --i) u8((v >> (i * 8)) & 0xff); }
    void be64(uint64_t v) { for (int i = 7; i >= 0; --i) u8((v >> (i * 8)) & 0xff); }
};

struct Value {
    enum class Kind { Nil, Bool, Int, UInt, Double, Str, Bin, Array, Map, Error } kind = Kind::Nil;
    bool b = false;
    int64_t i = 0;
    uint64_t u = 0;
    double d = 0;
    std::string s;
    Bytes bin;
    std::vector<Value> array;
    std::map<std::string, Value> map;
    std::string error;
};

class Reader {
public:
    Reader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    bool ok() const { return ok_; }
    Value parse() {
        Value v;
        if (!ok_ || pos_ >= n_) { v.kind = Value::Kind::Error; v.error = "eof"; ok_ = false; return v; }
        uint8_t b = p_[pos_++];
        if (b <= 0x7f) { v.kind = Value::Kind::UInt; v.u = b; return v; }
        if (b >= 0xe0) { v.kind = Value::Kind::Int; v.i = static_cast<int8_t>(b); return v; }
        if ((b & 0xf0) == 0x80) return str(b & 0x0f);
        if ((b & 0xf0) == 0x90) return arr(b & 0x0f);
        if ((b & 0xf0) == 0xa0) return str(b & 0x1f);
        switch (b) {
            case 0xc0: v.kind = Value::Kind::Nil; return v;
            case 0xc2: v.kind = Value::Kind::Bool; v.b = false; return v;
            case 0xc3: v.kind = Value::Kind::Bool; v.b = true; return v;
            case 0xc4: { uint32_t n = u8(); return bin(n); }
            case 0xc5: { uint32_t n = be16(); return bin(n); }
            case 0xc6: { uint32_t n = be32(); return bin(n); }
            case 0xca: { uint32_t u = be32(); v.kind = Value::Kind::Double; float f; std::memcpy(&f, &u, 4); v.d = f; return v; }
            case 0xcb: { uint64_t u = be64(); v.kind = Value::Kind::Double; std::memcpy(&v.d, &u, 8); return v; }
            case 0xcc: v.kind = Value::Kind::UInt; v.u = u8(); return v;
            case 0xcd: v.kind = Value::Kind::UInt; v.u = be16(); return v;
            case 0xce: v.kind = Value::Kind::UInt; v.u = be32(); return v;
            case 0xcf: v.kind = Value::Kind::UInt; v.u = be64(); return v;
            case 0xd0: v.kind = Value::Kind::Int; v.i = static_cast<int8_t>(u8()); return v;
            case 0xd1: v.kind = Value::Kind::Int; v.i = static_cast<int16_t>(be16()); return v;
            case 0xd2: v.kind = Value::Kind::Int; v.i = static_cast<int32_t>(be32()); return v;
            case 0xd3: v.kind = Value::Kind::Int; v.i = static_cast<int64_t>(be64()); return v;
            case 0xd9: return str(u8());
            case 0xda: return str(be16());
            case 0xdc: return arr(be16());
            case 0xdd: return arr(be32());
            default:
                v.kind = Value::Kind::Error; v.error = "unhandled msgpack byte " + std::to_string(b); ok_ = false; return v;
        }
    }
private:
    const uint8_t* p_;
    size_t n_;
    size_t pos_ = 0;
    bool ok_ = true;
    uint8_t next() { if (pos_ >= n_) { ok_ = false; return 0; } return p_[pos_++]; }
    uint8_t u8() { return next(); }
    uint16_t be16() { uint16_t v = 0; v = (uint16_t)(v << 8) | next(); v = (uint16_t)(v << 8) | next(); return v; }
    uint32_t be32() { uint32_t v = 0; for (int i = 0; i < 4; ++i) v = (v << 8) | next(); return v; }
    uint64_t be64() { uint64_t v = 0; for (int i = 0; i < 8; ++i) v = (v << 8) | next(); return v; }
    Value str(uint32_t n) { Value v; v.kind = Value::Kind::Str; v.s.resize(n); for (uint32_t i = 0; i < n; ++i) v.s[i] = (char)next(); return v; }
    Value bin(uint32_t n) { Value v; v.kind = Value::Kind::Bin; v.bin.resize(n); for (uint32_t i = 0; i < n; ++i) v.bin[i] = next(); return v; }
    Value arr(uint32_t n) { Value v; v.kind = Value::Kind::Array; for (uint32_t i = 0; i < n; ++i) v.array.push_back(parse()); if (!ok_) v.kind = Value::Kind::Error; return v; }
};

// ---------------------------------------------------------------------------
// The vendor-command framing (protocol-spec §2)
// ---------------------------------------------------------------------------
constexpr uint16_t kHpRequestMagic = 0x4d47;   // wire 47 4d
constexpr uint16_t kHpResponseMagic = 0x4252;  // wire 52 42
constexpr uint16_t kOpGetProperty = 1;
constexpr uint16_t kOpSetProperty = 2;

Bytes BuildVendorRequest(uint16_t opcode, uint16_t request_id, const Bytes& payload) {
    Bytes out;
    size_t half_words = (8 + payload.size()) / 2;
    out.push_back(0x47); out.push_back(0x4d);                 // magic LE 0x4d47
    out.push_back(half_words & 0xff); out.push_back(half_words >> 8);
    out.push_back(opcode & 0xff); out.push_back(opcode >> 8);
    out.push_back(request_id & 0xff); out.push_back(request_id >> 8);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

Bytes SetPropertyPayload(uint32_t prop, int32_t value) {
    Bytes p;
    for (int i = 0; i < 4; ++i) p.push_back((prop >> (i * 8)) & 0xff);
    for (int i = 0; i < 4; ++i) p.push_back((static_cast<uint32_t>(value) >> (i * 8)) & 0xff);
    return p;
}

// ---------------------------------------------------------------------------
// GVCP discovery (protocol-spec §5)
// ---------------------------------------------------------------------------
const Bytes kDiscoveryCmd = {0x42, 0x11, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01};

void GvcpDiscover(int wait_ms) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { std::perror("socket"); return; }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    struct timeval tv = {wait_ms / 1000, (wait_ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    sockaddr_in dst = {};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(3956);
    dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(fd, kDiscoveryCmd.data(), kDiscoveryCmd.size(), 0, (sockaddr*)&dst, sizeof(dst));
    dst.sin_addr.s_addr = inet_addr("127.0.0.1");
    sendto(fd, kDiscoveryCmd.data(), kDiscoveryCmd.size(), 0, (sockaddr*)&dst, sizeof(dst));
    uint8_t buf[2048];
    int n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) {
        std::printf("[gvcp] no DISCOVERY_ACK within %d ms (spec §6.4: the ack shape is unsettled)\n", wait_ms);
    } else {
        std::printf("[gvcp] reply %d bytes:", n);
        for (int i = 0; i < n; ++i) std::printf(" %02x", buf[i]);
        std::printf("\n");
    }
    close(fd);
}

// ---------------------------------------------------------------------------
// msgpack-RPC over TCP (protocol-spec §1.1)
// ---------------------------------------------------------------------------
class RpcClient {
public:
    bool connect(const std::string& host, uint16_t port) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = inet_addr(host.c_str());
        if (::connect(fd_, (sockaddr*)&a, sizeof(a)) != 0) { close(fd_); fd_ = -1; return false; }
        return true;
    }
    ~RpcClient() { if (fd_ >= 0) close(fd_); }

    // One rpclib request: [0, msgid, method, params]. Returns the 4th element
    // of the [1, msgid, nil, result] response, or an Error Value.
    Value call(const std::string& method, const Bytes& params) {
        Packer p;
        p.array(4);
        p.uint(0);
        p.uint(++msgid_);
        p.str(method);
        p.raw(params.data(), params.size());
        if (!WriteAll(p.out)) return Error("write failed");
        Value resp = ReadMessage();
        if (resp.kind != Value::Kind::Array || resp.array.size() < 4) return Error("bad response");
        if (resp.array[0].kind != Value::Kind::UInt || resp.array[0].u != 1) return Error("not a response");
        return resp.array[3];
    }

    static Value Error(const std::string& s) { Value v; v.kind = Value::Kind::Error; v.error = s; return v; }

    // Raw send/receive for --replay: the bytes come from recon's dump, not
    // from our own encoder.
    bool sendRaw(const Bytes& frame) { return WriteAll(frame); }
    Value readReply() { return ReadMessage(); }

private:
    int fd_ = -1;
    uint32_t msgid_ = 0;
    bool WriteAll(const Bytes& b) {
        size_t sent = 0;
        while (sent < b.size()) {
            ssize_t n = send(fd_, b.data() + sent, b.size() - sent, 0);
            if (n <= 0) return false;
            sent += (size_t)n;
        }
        return true;
    }
    Value ReadMessage() {
        // One response per call; read until the msgpack value parses complete
        // (a partial value reports an error, so we read more and retry).
        Bytes buf;
        uint8_t tmp[65536];
        for (;;) {
            Reader r(buf.data(), buf.size());
            Value v = r.parse();
            if (r.ok()) return v;
            ssize_t n = recv(fd_, tmp, sizeof(tmp), 0);
            if (n <= 0) return Error("eof");
            buf.insert(buf.end(), tmp, tmp + n);
            if (buf.size() > (1u << 26)) return Error("response too large");
        }
    }
};

// ---------------------------------------------------------------------------
// Selftest (dry-run): spec-recorded byte shapes
// ---------------------------------------------------------------------------
int DryRun() {
    int failures = 0;
    auto check = [&](const char* name, bool ok) {
        std::printf("[dry-run] %-52s %s\n", name, ok ? "PASS" : "FAIL");
        if (!ok) ++failures;
    };

    // §2.1: request [6,9,1920,1200,50] encodes as 95 06 09 cd0780 cd04b032.
    Packer p;
    p.array(5);
    p.uint(6); p.uint(9); p.uint(1920); p.uint(1200); p.uint(50);
    const std::vector<uint8_t> expect = {0x95, 0x06, 0x09, 0xcd, 0x07, 0x80, 0xcd, 0x04, 0xb0, 0x32};
    check("start_stream params [6,9,1920,1200,50]", p.out == expect);

    // §2.1: SET_PROPERTY vendor header: magic 474d, size in half-words,
    // opcode 2, request id. Payload {u32 prop, i32 value} = 8 B.
    Bytes vendor = BuildVendorRequest(kOpSetProperty, 7, SetPropertyPayload(3, 1));
    bool req_ok = vendor.size() == 16 && vendor[0] == 0x47 && vendor[1] == 0x4d &&
                  vendor[2] == 0x08 && vendor[3] == 0x00 && vendor[4] == 0x02 && vendor[5] == 0x00 &&
                  vendor[6] == 0x07 && vendor[7] == 0x00;
    check("vendor ReqHeader magic/size/opcode/id", req_ok);

    // §1.3: the frame response is a msgpack bin; decode one.
    const std::vector<uint8_t> binframe = {0xc4, 0x03, 0x0a, 0x0b, 0x0c};
    Reader r(binframe.data(), binframe.size());
    Value v = r.parse();
    check("frame response decodes as bin(3)", v.kind == Value::Kind::Bin && v.bin.size() == 3);

    // §2.1: response header 5242 5300 0300 0000 0000.
    check("vendor response magic 0x4252", (kHpResponseMagic & 0xff) == 0x52 && (kHpResponseMagic >> 8) == 0x42);

    // §5: DISCOVERY_CMD bytes.
    const std::vector<uint8_t> dexpect = {0x42, 0x11, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01};
    check("GVCP DISCOVERY_CMD", kDiscoveryCmd == dexpect);

    std::printf("[dry-run] %s (%d failure(s))\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Live session (needs the daemon + device; hardware room)
// ---------------------------------------------------------------------------
std::string HexHead(const Bytes& b, size_t max = 16) {
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < b.size() && i < max; ++i) {
        if (i) s += ' ';
        s += hex[b[i] >> 4];
        s += hex[b[i] & 0xf];
    }
    if (b.size() > max) s += " …";
    return s;
}

Value StartStream(RpcClient& c, const std::string& suffix, uint8_t command, uint32_t w, uint32_t h, uint32_t fps) {
    Packer p;
    p.array(5);
    p.uint(command); p.uint(9); p.uint(w); p.uint(h); p.uint(fps);
    return c.call("ob_rpc_start_stream" + suffix, p.out);
}

// Decode the vendor response (RespHeader: magic 5242, size, opcode, requestId,
// errorCode) if it is a bin; returns true when errorCode == 0.
bool VendorAccepted(const Value& rpc_result, uint16_t* out_opcode, uint16_t* out_reqid, uint16_t* out_error) {
    if (rpc_result.kind != Value::Kind::Bin || rpc_result.bin.size() < 10) return false;
    const Bytes& b = rpc_result.bin;
    auto le16 = [&](size_t o) { return (uint16_t)(b[o] | (b[o + 1] << 8)); };
    if (le16(0) != 0x4252) return false;  // "BR"
    if (out_opcode) *out_opcode = le16(4);
    if (out_reqid) *out_reqid = le16(6);
    if (out_error) *out_error = le16(8);
    return le16(8) == 0;
}

void SetProperty(RpcClient& c, const std::string& suffix, uint32_t prop, int32_t value, uint16_t request_id,
                 bool log) {
    Bytes vendor = BuildVendorRequest(kOpSetProperty, request_id, SetPropertyPayload(prop, value));
    Packer p;
    p.array(2);
    p.bin(vendor);
    p.uint(64);  // expected response size for an ack (spec §2.1)
    Value r = c.call("ob_rpc_send_data" + suffix, p.out);
    uint16_t opcode = 0, reqid = 0, err = 0xffff;
    bool accepted = VendorAccepted(r, &opcode, &reqid, &err);
    if (log) {
        if (r.kind == Value::Kind::Error) {
            std::printf("[live] SET prop=%u value=%d reqid=%u -> rpc ERROR: %s\n", prop, value, request_id,
                        r.error.c_str());
        } else if (r.kind == Value::Kind::Bin) {
            std::printf("[live] SET prop=%u value=%d reqid=%u -> vendor %s (opcode=%u reqid=%u err=%u, %zu B)\n",
                        prop, value, request_id, accepted ? "ACCEPT" : "REJECT", opcode, reqid, err, r.bin.size());
        } else {
            std::printf("[live] SET prop=%u value=%d reqid=%u -> rpc kind=%d\n", prop, value, request_id,
                        (int)r.kind);
        }
        std::fflush(stdout);
    }
}

int Live(const std::string& host, uint16_t port, int seconds, const std::string& out_path, int pull_ms, bool log) {
    RpcClient rpc;
    if (!rpc.connect(host, port)) {
        std::fprintf(stderr, "[live] cannot connect to %s:%u (is RPCServer running?)\n", host.c_str(), port);
        return 1;
    }
    std::printf("[live] connected %s:%u (pull_ms=%d, per-call logging=%s)\n", host.c_str(), port, pull_ms,
                log ? "on" : "off");
    Value started = StartStream(rpc, "", 6, 1920, 1200, 50);
    if (started.kind == Value::Kind::Error) {
        std::fprintf(stderr, "[live] start_stream failed: %s\n", started.error.c_str());
        return 1;
    }
    std::printf("[live] start_stream -> %s\n", started.kind == Value::Kind::Bool && started.b ? "true" : "non-error");
    std::fflush(stdout);

    uint64_t get_calls = 0, bins = 0, empties = 0, errors = 0;
    uint16_t request_id = 0;
    bool saved = false;
    auto last_log = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        Packer p;
        p.array(0);
        Value frame = rpc.call("ob_rpc_get_frame", p.out);
        ++get_calls;
        bool should_log = false;
        if (frame.kind == Value::Kind::Bin) {
            ++bins;
            if (!saved) {
                std::ofstream f(out_path, std::ios::binary);
                f.write(reinterpret_cast<const char*>(frame.bin.data()), frame.bin.size());
                std::printf("[live] saved first frame: %s (%zu bytes)\n", out_path.c_str(), frame.bin.size());
                saved = true;
                should_log = true;
            } else {
                auto now = std::chrono::steady_clock::now();
                should_log = now - last_log >= std::chrono::milliseconds(250);
            }
        } else if (frame.kind == Value::Kind::Error) {
            ++errors;
            should_log = true;  // always show errors
        } else {
            ++empties;
            should_log = get_calls <= 5 || (get_calls % 100 == 0);
        }
        if (log && should_log) {
            last_log = std::chrono::steady_clock::now();
            if (frame.kind == Value::Kind::Bin) {
                std::printf("[live] GET_FRAME #%llu -> bin(%zu) head=[%s]\n", (unsigned long long)get_calls,
                            frame.bin.size(), HexHead(frame.bin).c_str());
            } else if (frame.kind == Value::Kind::Error) {
                std::printf("[live] GET_FRAME #%llu -> ERROR: %s\n", (unsigned long long)get_calls,
                            frame.error.c_str());
            } else {
                std::printf("[live] GET_FRAME #%llu -> empty (kind=%d)\n", (unsigned long long)get_calls,
                            (int)frame.kind);
            }
            std::fflush(stdout);
        }
        // §3.1 host-driven loops: laser trio and exposure, at the observed cadence.
        SetProperty(rpc, "", 3, 0, ++request_id, log);
        SetProperty(rpc, "", 152, 0, ++request_id, log);
        SetProperty(rpc, "", 153, 1, ++request_id, log);
        SetProperty(rpc, "", 154, 866000, ++request_id, log);
        SetProperty(rpc, "", 155, 866000, ++request_id, log);
        if (pull_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(pull_ms));
    }
    StartStream(rpc, "", 7, 1920, 1200, 50);  // stop
    std::printf("[live] done: get_frame=%llu bin=%llu empty=%llu error=%llu; stream stopped\n",
                (unsigned long long)get_calls, (unsigned long long)bins, (unsigned long long)empties,
                (unsigned long long)errors);
    return bins > 0 ? 0 : 1;
}

// Prints the exact bytes each call would send, so the logging path is
// exercisable without the daemon (the row's "what can be tested locally").
int DryRunLive() {
    int failures = 0;
    auto show = [&](const char* what, const Bytes& b) {
        std::printf("[dry-run-live] %-28s %zu B: %s\n", what, b.size(), HexHead(b, 24).c_str());
        if (b.empty()) ++failures;
    };
    RpcClient unused;  // not connected; we only encode here
    (void)unused;
    Packer start;
    start.array(5);
    start.uint(6); start.uint(9); start.uint(1920); start.uint(1200); start.uint(50);
    show("start_stream params", start.out);
    Packer getf;
    getf.array(0);
    show("get_frame params", getf.out);
    for (uint32_t prop : {3u, 152u, 153u, 154u, 155u}) {
        Bytes vendor = BuildVendorRequest(kOpSetProperty, (uint16_t)(prop & 0xffff), SetPropertyPayload(prop, 0));
        show("SET_PROPERTY vendor", vendor);
    }
    std::printf("[dry-run-live] %s (%d failure(s))\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// --replay: send recon's captured request frames, unchanged, in order
//
// Input format (text, one frame per line):
//   [+<ms> ]<hex bytes>      optional delay; the space after the delay is
//                            required (it separates it from the hex payload)
//   # comment / blank lines are ignored
// Bytes are the raw msgpack-RPC request frame as captured (e.g. the 0x94…
// [0, msgid, method, params] frame). We do not re-encode; this is the vendor's
// own bytes.
// ---------------------------------------------------------------------------
bool ParseReplayFile(const std::string& path, std::vector<Bytes>* frames, std::vector<int>* delays,
                     std::string* err) {
    std::ifstream in(path);
    if (!in) { *err = "cannot open " + path; return false; }
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        // strip comments, then trim the line (do NOT strip the space that
        // separates the '+<ms>' prefix from the hex payload).
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        size_t last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, last - first + 1);
        int delay = 0;
        std::string hex = line;
        if (hex[0] == '+') {
            // Split on the first whitespace: "+<ms>" is the delay, the rest
            // is the hex payload. Scanning digits across the space was the
            // bug (hex bytes 94/00… are also decimal characters).
            size_t sp = hex.find_first_of(" \t");
            if (sp == std::string::npos) {
                *err = "line " + std::to_string(lineno) + ": '+<ms>' must be followed by whitespace and hex";
                return false;
            }
            std::string prefix = hex.substr(1, sp - 1);
            if (prefix.empty()) { *err = "line " + std::to_string(lineno) + ": empty delay"; return false; }
            for (char c : prefix) {
                if (!std::isdigit((unsigned char)c)) {
                    *err = "line " + std::to_string(lineno) + ": non-digit in delay";
                    return false;
                }
                delay = delay * 10 + (c - '0');
            }
            hex = hex.substr(sp + 1);
        }
        std::string payload;
        for (char c : hex) if (!std::isspace((unsigned char)c)) payload += c;
        hex = payload;
        if (hex.empty()) continue;
        if (hex.size() % 2 != 0) { *err = "line " + std::to_string(lineno) + ": odd hex length"; return false; }
        Bytes b;
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        for (size_t i = 0; i < hex.size(); i += 2) {
            int hi = nib(hex[i]), lo = nib(hex[i + 1]);
            if (hi < 0 || lo < 0) { *err = "line " + std::to_string(lineno) + ": bad hex"; return false; }
            b.push_back((uint8_t)((hi << 4) | lo));
        }
        if (b.empty()) continue;
        frames->push_back(b);
        delays->push_back(delay);
    }
    return true;
}

// Regression for the row #105 parser bug: '+25429 94000d…' — the space after
// the delay must delimit it, or the digit scanner eats 94000 (hex chars that
// are also decimal digits).
int ReplayParserSelftest() {
    const char* line = "+25429 94000db66f625f7270635f6765745f6672616d65302d322d313290\n";
    char path[] = "/tmp/raptor_replay_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { std::printf("[replay-selftest] SKIP (mkstemp failed)\n"); return 0; }
    ssize_t ignored = write(fd, line, std::strlen(line));
    (void)ignored;
    close(fd);
    std::vector<Bytes> frames;
    std::vector<int> delays;
    std::string err;
    bool ok = ParseReplayFile(path, &frames, &delays, &err);
    unlink(path);
    bool pass = ok && frames.size() == 1 && delays.size() == 1 && delays[0] == 25429 && !frames[0].empty();
    std::printf("[replay-selftest] delay-then-hex-digits %s%s%s\n", pass ? "PASS" : "FAIL",
                pass ? "" : ": ", pass ? "" : err.c_str());
    return pass ? 0 : 1;
}

int Replay(const std::string& host, uint16_t port, const std::string& path, bool dry_run, bool log) {
    std::vector<Bytes> frames;
    std::vector<int> delays;
    std::string err;
    if (!ParseReplayFile(path, &frames, &delays, &err)) {
        std::fprintf(stderr, "[replay] %s\n", err.c_str());
        return 2;
    }
    std::printf("[replay] %zu frame(s) from %s%s\n", frames.size(), path.c_str(),
                dry_run ? " (dry-run: not sent)" : "");
    if (frames.empty()) return 2;
    if (dry_run) {
        for (size_t i = 0; i < frames.size(); ++i) {
            std::printf("[replay] frame %zu: +%dms %zu B: %s\n", i + 1, delays[i], frames[i].size(),
                        HexHead(frames[i]).c_str());
        }
        return 0;
    }
    RpcClient rpc;
    if (!rpc.connect(host, port)) {
        std::fprintf(stderr, "[replay] cannot connect to %s:%u (is RPCServer running?)\n", host.c_str(), port);
        return 1;
    }
    std::printf("[replay] connected %s:%u; sending unchanged request frames\n", host.c_str(), port);
    uint64_t bins = 0, empties = 0, errors = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (delays[i] > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delays[i]));
        if (!rpc.sendRaw(frames[i])) {
            std::fprintf(stderr, "[replay] frame %zu: send failed\n", i + 1);
            ++errors;
            break;
        }
        Value reply = rpc.readReply();
        if (reply.kind == Value::Kind::Bin) {
            ++bins;
            if (log) std::printf("[replay] frame %zu -> bin(%zu) head=[%s]\n", i + 1, reply.bin.size(),
                                 HexHead(reply.bin).c_str());
        } else if (reply.kind == Value::Kind::Error) {
            ++errors;
            if (log) std::printf("[replay] frame %zu -> ERROR: %s\n", i + 1, reply.error.c_str());
        } else {
            ++empties;
            if (log) std::printf("[replay] frame %zu -> kind=%d\n", i + 1, (int)reply.kind);
        }
        std::fflush(stdout);
    }
    std::printf("[replay] done: bin=%llu empty=%llu error=%llu\n", (unsigned long long)bins,
                (unsigned long long)empties, (unsigned long long)errors);
    return bins > 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--dry-run") {
        int rc = DryRun();
        if (rc) return rc;
        return ReplayParserSelftest();
    }
    if (argc >= 2 && std::string(argv[1]) == "--dry-run-live") return DryRunLive();
    if (argc >= 2 && std::string(argv[1]) == "--discover") { GvcpDiscover(1000); return 0; }
    if (argc >= 2 && std::string(argv[1]) == "--replay") {
        if (argc < 3) { std::fprintf(stderr, "usage: raptor_client --replay <file> [host] [port] [--dry-run] [--quiet]\n"); return 2; }
        std::string path = argv[2];
        std::string host = argc > 3 ? argv[3] : "127.0.0.1";
        uint16_t port = argc > 4 ? (uint16_t)std::atoi(argv[4]) : 10013;
        bool dry_run = false, log = true;
        for (int i = 3; i < argc; ++i) {
            if (std::string(argv[i]) == "--dry-run") dry_run = true;
            if (std::string(argv[i]) == "--quiet") log = false;
        }
        return Replay(host, port, path, dry_run, log);
    }
    if (argc >= 2 && std::string(argv[1]) == "--live") {
        std::string host = argc > 2 ? argv[2] : "127.0.0.1";
        uint16_t port = argc > 3 ? (uint16_t)std::atoi(argv[3]) : 10013;
        int seconds = argc > 4 ? std::atoi(argv[4]) : 5;
        std::string out = argc > 5 ? argv[5] : "/tmp/raptor_frame.raw";
        int pull_ms = 0;
        bool log = true;
        for (int i = 2; i < argc; ++i) {
            if (std::string(argv[i]) == "--pull-ms" && i + 1 < argc) pull_ms = std::atoi(argv[++i]);
            if (std::string(argv[i]) == "--quiet") log = false;
        }
        return Live(host, port, seconds, out, pull_ms, log);
    }
    std::printf(
        "raptor_client — Phase B (C1) client for the Creality RPCServer daemon\n"
        "Usage:\n"
        "  raptor_client --dry-run                  framing selftest, no device\n"
        "  raptor_client --dry-run-live             print the bytes each call would send\n"
        "  raptor_client --discover                 GVCP discovery only\n"
        "  raptor_client --replay <file> [host] [port] [--dry-run] [--quiet]\n"
        "      sends recon's captured request frames unchanged, in order; file\n"
        "      format: one hex frame per line, optional '+<ms> ' delay prefix,\n"
        "      '#' comments/blank lines ignored\n"
        "  raptor_client --live <host> [port] [seconds] [out.raw] [--pull-ms N] [--quiet]\n"
        "      per-call logging is ON by default (--quiet disables it); --pull-ms 0 = as fast\n");
    return 0;
}
