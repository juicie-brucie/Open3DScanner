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
//   raptor_client --discover                 # GVCP discovery only
//   raptor_client --live <host> [port] [seconds] [out.raw]
//
// Wall: tools/** only. Clean-room: implements the spec; no vendor binary.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
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
Value StartStream(RpcClient& c, const std::string& suffix, uint8_t command, uint32_t w, uint32_t h, uint32_t fps) {
    Packer p;
    p.array(5);
    p.uint(command); p.uint(9); p.uint(w); p.uint(h); p.uint(fps);
    return c.call("ob_rpc_start_stream" + suffix, p.out);
}

void SetProperty(RpcClient& c, const std::string& suffix, uint32_t prop, int32_t value, uint16_t request_id) {
    Bytes vendor = BuildVendorRequest(kOpSetProperty, request_id, SetPropertyPayload(prop, value));
    Packer p;
    p.array(2);
    p.bin(vendor);
    p.uint(64);  // expected response size for an ack (spec §2.1)
    c.call("ob_rpc_send_data" + suffix, p.out);
}

int Live(const std::string& host, uint16_t port, int seconds, const std::string& out_path) {
    RpcClient rpc;
    if (!rpc.connect(host, port)) {
        std::fprintf(stderr, "[live] cannot connect to %s:%u (is RPCServer running?)\n", host.c_str(), port);
        return 1;
    }
    std::printf("[live] connected %s:%u\n", host.c_str(), port);
    Value started = StartStream(rpc, "", 6, 1920, 1200, 50);
    if (started.kind == Value::Kind::Error) {
        std::fprintf(stderr, "[live] start_stream failed: %s\n", started.error.c_str());
        return 1;
    }
    std::printf("[live] stream started; pulling frames for %ds\n", seconds);

    uint32_t frames = 0;
    uint16_t request_id = 0;
    bool saved = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        Packer p;
        p.array(0);
        Value frame = rpc.call("ob_rpc_get_frame", p.out);
        if (frame.kind == Value::Kind::Bin) {
            ++frames;
            if (!saved) {
                std::ofstream f(out_path, std::ios::binary);
                f.write(reinterpret_cast<const char*>(frame.bin.data()), frame.bin.size());
                std::printf("[live] saved first frame: %s (%zu bytes)\n", out_path.c_str(), frame.bin.size());
                saved = true;
            }
        }
        // §3.1 host-driven loops: laser trio and exposure, at the observed cadence.
        SetProperty(rpc, "", 3, 0, ++request_id);
        SetProperty(rpc, "", 152, 0, ++request_id);
        SetProperty(rpc, "", 153, 1, ++request_id);
        SetProperty(rpc, "", 154, 866000, ++request_id);
        SetProperty(rpc, "", 155, 866000, ++request_id);
    }
    StartStream(rpc, "", 7, 1920, 1200, 50);  // stop
    std::printf("[live] pulled %u frames; stream stopped\n", frames);
    return frames > 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--dry-run") return DryRun();
    if (argc >= 2 && std::string(argv[1]) == "--discover") { GvcpDiscover(1000); return 0; }
    if (argc >= 2 && std::string(argv[1]) == "--live") {
        std::string host = argc > 2 ? argv[2] : "127.0.0.1";
        uint16_t port = argc > 3 ? (uint16_t)std::atoi(argv[3]) : 10013;
        int seconds = argc > 4 ? std::atoi(argv[4]) : 5;
        std::string out = argc > 5 ? argv[5] : "/tmp/raptor_frame.raw";
        return Live(host, port, seconds, out);
    }
    std::printf(
        "raptor_client — Phase B (C1) client for the Creality RPCServer daemon\n"
        "Usage:\n"
        "  raptor_client --dry-run                  framing selftest, no device\n"
        "  raptor_client --discover                 GVCP discovery only\n"
        "  raptor_client --live <host> [port] [seconds] [out.raw]\n");
    return 0;
}
