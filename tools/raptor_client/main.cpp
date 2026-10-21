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
//   raptor_client --selftest-timeout         # the bounded reply read, no device
//   raptor_client --selftest-dump            # the reply-structure dump, no device
//   raptor_client --discover                 # GVCP discovery only
//   raptor_client --replay <file> [host] [--dry-run] [--quiet]
//       [--reply-timeout-ms N] [--ir-port N] [--depth-port N] [--dump-replies]
//       [--capture DIR] [--capture-seconds N] [--capture-bytes N]
//       recon's captured request frames, unchanged, in order (hex lines).
//       The '+<ms>' prefix is an ABSOLUTE capture timestamp (ms since the
//       capture started), so the replay paces on the inter-arrival deltas
//       between stamps. Each frame is routed by the interface suffix on
//       its method name to that interface's own port.
//   raptor_client --live <host> [port] [seconds] [out.raw] [--pull-ms N] [--quiet]
//       one interface per run: the port picks the interface and every method
//       name carries that interface's suffix (ob_rpc_start_stream0-2-12 on
//       port 10013, ob_rpc_start_stream0-2-10 on port 10010).
//       per-call logging is on by default: every get_frame result (band size /
//       empty / error) and every SET_PROPERTY (prop, value, ACCEPT/REJECT).
//       [--dump-replies] adds one structure line per reply: the RPC kind, the
//       msgid, and the result's shape (an array's length and each element's
//       kind, with a hex head for any bin or string). For a get_frame reply
//       the raw reply bytes follow, so the envelope sits next to the
//       unwrapped result. It is off by default: the output above must not
//       change.
//       [--capture DIR] records every reply, byte for byte, into a directory
//       outside the repository (the bundle is never committed): one file per
//       port plus a manifest with the run's identity and every file's hash.
//       [--reply-timeout-ms N] bounds the connect and every reply read
//       (default 5000); on expiry the run names the endpoint, interface and
//       unanswered method and exits 3 instead of hanging.
//   raptor_client --selftest-dump              the reply-structure dump, no device
//   raptor_client --selftest-capture           the capture round-trip, no device
//   raptor_client --selftest-live              the live path's method names, no device
//   raptor_client --selftest-live-timeout      the live path's bounded wait, no device
//   raptor_client --capture-dump <dir>         read a capture bundle back
//
// Wall: tools/** only. Clean-room: implements the spec; no vendor binary.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "capture.h"

namespace {

using Bytes = std::vector<uint8_t>;

// Bytes from a hex string.
Bytes FromHex(const char* h) {
    Bytes b;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; h[i] && h[i + 1]; i += 2) b.push_back((uint8_t)((nib(h[i]) << 4) | nib(h[i + 1])));
    return b;
}

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
    // Timeout is last so the numeric kind values the older logs recorded
    // (e.g. "kind=7" for the RPC envelope array) keep their meaning.
    enum class Kind { Nil, Bool, Int, UInt, Double, Str, Bin, Array, Map, Error, Timeout } kind = Kind::Nil;
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

// The text of an RPC error value. rpclib reports one as a string, or as a
// [code, message] pair.
std::string RpcErrorText(const Value& e) {
    if (e.kind == Value::Kind::Str) return e.s;
    if (e.kind == Value::Kind::Array && e.array.size() >= 2 && e.array[1].kind == Value::Kind::Str)
        return e.array[1].s;
    return "rpc error";
}

// The payload of an RPC reply.
//
// A reply is the msgpack-RPC envelope [1, msgid, nil, result]
// (protocol-spec.md §1.1): the result is at index 3, and a non-nil
// index 2 is an error (the rpclib convention — an error reply carries
// the error there and nil in the result slot).
//
// So a reply that arrives as a whole envelope must be unwrapped before
// its payload can be looked at: a frame is the bin at index 3, not the
// envelope array. A reply that is NOT an envelope is returned unchanged,
// so a peer that answers with a bare payload still works.
Value Result(const Value& reply) {
    if (reply.kind != Value::Kind::Array || reply.array.size() < 4) return reply;
    if (reply.array[0].kind != Value::Kind::UInt || reply.array[0].u != 1) return reply;
    // A non-nil error slot: the reply is an error, not a payload.
    if (reply.array[2].kind != Value::Kind::Nil) {
        Value err;
        err.kind = Value::Kind::Error;
        err.error = RpcErrorText(reply.array[2]);
        return err;
    }
    return reply.array[3];
}

class Reader {
public:
    Reader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    bool ok() const { return ok_; }
    // How many bytes the last parse consumed. A reader over a buffer that
    // holds more than one message needs it to find the next one.
    size_t consumed() const { return pos_; }
    Value parse() {
        Value v;
        if (!ok_ || pos_ >= n_) { v.kind = Value::Kind::Error; v.error = "eof"; ok_ = false; return v; }
        uint8_t b = p_[pos_++];
        if (b <= 0x7f) { v.kind = Value::Kind::UInt; v.u = b; return v; }
        if (b >= 0xe0) { v.kind = Value::Kind::Int; v.i = static_cast<int8_t>(b); return v; }
        if ((b & 0xf0) == 0x80) return str(b & 0x0f);
        if ((b & 0xf0) == 0x90) return arr(b & 0x0f);
        // fixstr spans 0xa0-0xbf, so the mask is 0xe0: with 0xf0 the
        // 16-31 byte lengths (0xb0-0xbf) fell through to the unhandled
        // case, and a reply carrying one read as an error.
        if ((b & 0xe0) == 0xa0) return str(b & 0x1f);
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

// Row #161: how long the live path may wait on any one blocking step — the
// TCP handshake and each reply read. A daemon that holds the connection and
// never answers is then a named rejection, not a hang. The bound is per
// step, not per run: a run's window (seconds / --frames) still bounds it.
constexpr int kDefaultLiveTimeoutMs = 5000;

// The live path's exit codes. A timeout is its own code so a hardware record
// can tell "the daemon never answered" from "it answered and served no
// frame" (1) and from a usage error (2).
constexpr int kExitLiveFailed = 1;
constexpr int kExitLiveTimeout = 3;

// Waits for fd to become ready, up to timeout_ms. Returns 1 when ready, 0 on
// the deadline, -1 on error. A signal-interrupted wait is resumed, so the
// bound is a deadline and not a single poll.
int WaitEvent(int fd, short events, int timeout_ms) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);
    for (;;) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return 0;
        int remaining = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        if (remaining < 0) remaining = 0;
        struct pollfd p = {fd, events, 0};
        int pr = poll(&p, 1, remaining);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (pr == 0) return 0;
        return 1;
    }
}

class RpcClient {
public:
    // The two bounds the live path sets (row #161). Both default to the
    // live timeout, so a caller that sets nothing still cannot block
    // forever. The replay path keeps its own per-reply bound.
    void set_timeout(int ms) {
        timeout_ms_ = ms > 0 ? ms : kDefaultLiveTimeoutMs;
    }
    int timeout() const { return timeout_ms_; }

    // Why the last connect() failed, and whether it failed on the deadline
    // rather than on a refusal. The live path turns the latter into the
    // timeout exit code.
    const std::string& connect_error() const { return connect_error_; }
    bool connect_timed_out() const { return connect_timed_out_; }

    bool connect(const std::string& host, uint16_t port) {
        connect_error_.clear();
        connect_timed_out_ = false;
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) {
            connect_error_ = std::string("socket: ") + std::strerror(errno);
            return false;
        }
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = inet_addr(host.c_str());
        // A non-blocking handshake: a peer that takes the SYN and never
        // completes it must not hold the client. The same bound covers the
        // handshake as covers a reply read.
        int flags = fcntl(fd_, F_GETFL, 0);
        if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) != 0) {
            connect_error_ = std::string("fcntl: ") + std::strerror(errno);
            close(fd_);
            fd_ = -1;
            return false;
        }
        int rc = ::connect(fd_, (sockaddr*)&a, sizeof(a));
        if (rc != 0 && errno != EINPROGRESS) {
            connect_error_ = std::string("connect: ") + std::strerror(errno);
            close(fd_);
            fd_ = -1;
            return false;
        }
        if (rc != 0) {
            int pr = WaitEvent(fd_, POLLOUT, timeout_ms_);
            if (pr == 0) {
                connect_timed_out_ = true;
                connect_error_ = "no TCP handshake within " + std::to_string(timeout_ms_) + " ms";
                close(fd_);
                fd_ = -1;
                return false;
            }
            if (pr < 0) {
                connect_error_ = std::string("poll: ") + std::strerror(errno);
                close(fd_);
                fd_ = -1;
                return false;
            }
            int err = 0;
            socklen_t len = sizeof(err);
            if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
                connect_error_ = std::string("connect: ") + std::strerror(err ? err : errno);
                close(fd_);
                fd_ = -1;
                return false;
            }
        }
        fcntl(fd_, F_SETFL, flags);
        // A send must be bounded too: a peer that stops reading fills the
        // socket and blocks send(). SO_SNDTIMEO turns that into a failed
        // write, which call() reports as an error value.
        struct timeval tv = {timeout_ms_ / 1000, (timeout_ms_ % 1000) * 1000};
        setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        port_ = port;
        return true;
    }
    ~RpcClient() { if (fd_ >= 0) close(fd_); }

    // One rpclib request: [0, msgid, method, params]. Returns the reply
    // envelope [1, msgid, nil, result]; the caller unwraps it with Result().
    // The read is bounded by timeout_ms_ (row #161): an unanswered call is
    // a Timeout value, never an indefinite wait.
    Value call(const std::string& method, const Bytes& params) {
        Packer p;
        p.array(4);
        p.uint(0);
        p.uint(++msgid_);
        p.str(method);
        p.raw(params.data(), params.size());
        if (!WriteAll(p.out)) return Error("write failed");
        Value resp = ReadMessageWithDeadline(timeout_ms_);
        // The capture records the bytes as they arrived, so it runs before
        // the shape checks: a reply that fails them is still evidence.
        RecordCapture(method, resp);
        if (resp.kind == Value::Kind::Timeout) return resp;
        if (resp.kind != Value::Kind::Array || resp.array.size() < 4) return Error("bad response");
        if (resp.array[0].kind != Value::Kind::UInt || resp.array[0].u != 1) return Error("not a response");
        return resp;
    }

    static Value Error(const std::string& s) { Value v; v.kind = Value::Kind::Error; v.error = s; return v; }
    // A deadline expiry is its own kind so the replay can name the frame it
    // was waiting for, instead of lumping it in with a transport error.
    static Value Timeout() { Value v; v.kind = Value::Kind::Timeout; v.error = "timeout"; return v; }

    // Raw send/receive for --replay: the bytes come from recon's dump, not
    // from our own encoder.
    bool sendRaw(const Bytes& frame) { return WriteAll(frame); }
    Value readReply(const std::string& method = "") {
        Value v = ReadMessage();
        RecordCapture(method, v);
        return v;
    }

    // Bounded read for the replay path (row #117): the WHOLE reply must
    // arrive within timeout_ms, not just its first segment — a reply that
    // trickles in forever still times out. Returns a Timeout value when the
    // deadline passes. The live path uses the same bound through call().
    Value readReplyWithDeadline(int timeout_ms, const std::string& method = "") {
        Value v = ReadMessageWithDeadline(timeout_ms);
        RecordCapture(method, v);
        return v;
    }

    // The bytes of the last reply read, as they arrived. The reply dump
    // prints a head of them, so the envelope's own bytes sit next to the
    // unwrapped result.
    const Bytes& lastRaw() const { return last_raw_; }

    // The port this connection talks to.
    uint16_t port() const { return port_; }

    // The capture recorder (row #140). When set, every reply this
    // connection reads is written to the bundle, byte for byte.
    void set_capture(capture::Recorder* c) { capture_ = c; }

private:
    // Hand the reply to the capture, if one is running. A read that did
    // not complete (a timeout) records nothing.
    void RecordCapture(const std::string& method, const Value& envelope) {
        if (!capture_ || !last_read_ok_) return;
        uint64_t msgid = 0;
        if (envelope.kind == Value::Kind::Array && envelope.array.size() >= 2 &&
            envelope.array[1].kind == Value::Kind::UInt) {
            msgid = envelope.array[1].u;
        }
        capture_->Record(port_, method, msgid, last_raw_);
    }

    int fd_ = -1;
    uint32_t msgid_ = 0;
    uint16_t port_ = 0;
    int timeout_ms_ = kDefaultLiveTimeoutMs;
    std::string connect_error_;
    bool connect_timed_out_ = false;
    Bytes last_raw_;
    bool last_read_ok_ = false;
    capture::Recorder* capture_ = nullptr;
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
        last_read_ok_ = false;
        for (;;) {
            Reader r(buf.data(), buf.size());
            Value v = r.parse();
            if (r.ok()) { last_raw_ = buf; last_read_ok_ = true; return v; }
            ssize_t n = recv(fd_, tmp, sizeof(tmp), 0);
            if (n <= 0) return Error("eof");
            buf.insert(buf.end(), tmp, tmp + n);
            if (buf.size() > (1u << 26)) return Error("response too large");
        }
    }

    // Bounded twin of ReadMessage(): the whole reply must arrive within
    // timeout_ms. The deadline covers the entire read, so a reply that
    // arrives in dribbles still times out. Uses poll() on the socket rather
    // than SO_RCVTIMEO, so the socket's own receive options stay untouched.
    Value ReadMessageWithDeadline(int timeout_ms) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        Bytes buf;
        uint8_t tmp[65536];
        last_read_ok_ = false;
        for (;;) {
            Reader r(buf.data(), buf.size());
            Value v = r.parse();
            if (r.ok()) { last_raw_ = buf; last_read_ok_ = true; return v; }
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return Timeout();
            int remaining = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            if (remaining < 0) remaining = 0;
            int pr = WaitEvent(fd_, POLLIN, remaining);
            if (pr < 0) return Error("poll failed");
            if (pr == 0) return Timeout();
            ssize_t n = recv(fd_, tmp, sizeof(tmp), 0);
            if (n == 0) return Error("eof");
            if (n < 0) {
                if (errno == EINTR) continue;
                return Error("recv failed");
            }
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

    // The daemon's own method names are 17-19 bytes, so the reader must
    // take a fixstr of 16-31 bytes (and the str8 form above it).
    auto decodes_as = [](const std::string& s) {
        Packer p;
        p.str(s);
        Reader r(p.out.data(), p.out.size());
        Value v = r.parse();
        return r.ok() && v.kind == Value::Kind::Str && v.s == s;
    };
    check("a 15-byte string decodes", decodes_as(std::string(15, 'a')));
    check("a 17-byte string decodes", decodes_as("ob_rpc_get_frame"));
    check("a 31-byte string decodes", decodes_as(std::string(31, 'b')));
    check("a 32-byte string decodes", decodes_as(std::string(32, 'c')));

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

// ---------------------------------------------------------------------------
// The reply-structure dump (row #127)
//
// A reply's result is an array, so what the array holds is the open
// question. The dump prints that structure: the kind of the unwrapped
// result, the msgid, the array's length, and each element's kind — with
// the byte length and a hex head for the byte-carrying ones. For a frame
// pull the raw reply bytes follow, so the envelope's own shape sits next
// to the unwrapped result.
//
// The dump is off by default: the tool's existing output is what the
// hardware records parse, and it must not change.
// ---------------------------------------------------------------------------

// How many bytes of a bin or a string the dump shows.
constexpr size_t kDumpHeadBytes = 32;
// How many bytes of the raw reply message the dump shows.
constexpr size_t kDumpRawBytes = 48;

// The dump's vocabulary: the same set of kinds the "kind=N" lines record,
// spelled out.
const char* KindName(Value::Kind k) {
    switch (k) {
        case Value::Kind::Nil: return "Nil";
        case Value::Kind::Bool: return "Bool";
        case Value::Kind::Int: return "Int";
        case Value::Kind::UInt: return "UInt";
        case Value::Kind::Double: return "Double";
        case Value::Kind::Str: return "Str";
        case Value::Kind::Bin: return "Bin";
        case Value::Kind::Array: return "Array";
        case Value::Kind::Map: return "Map";
        case Value::Kind::Error: return "Error";
        case Value::Kind::Timeout: return "Timeout";
    }
    return "Unknown";
}

// The bytes of a string, for the hex head.
Bytes BytesOfString(const std::string& s) {
    Bytes b;
    b.reserve(s.size());
    for (char c : s) b.push_back(static_cast<uint8_t>(c));
    return b;
}

// A hex head of at most max bytes: space-separated pairs, and "..." when
// the value is longer. ASCII only, so a dump line is byte-identical
// wherever it is captured.
std::string HexBytes(const Bytes& b, size_t max) {
    static const char* hex = "0123456789abcdef";
    size_t n = b.size() < max ? b.size() : max;
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        if (i) s += ' ';
        s += hex[b[i] >> 4];
        s += hex[b[i] & 0xf];
    }
    if (b.size() > n) s += " ...";
    return s;
}

// One value, described: its kind, and for the byte-carrying kinds the
// length and a hex head.
std::string DescribeValue(const Value& v) {
    switch (v.kind) {
        case Value::Kind::Nil:
            return "Nil";
        case Value::Kind::Bool:
            return v.b ? "Bool(true)" : "Bool(false)";
        case Value::Kind::Int:
            return "Int(" + std::to_string(v.i) + ")";
        case Value::Kind::UInt:
            return "UInt(" + std::to_string(v.u) + ")";
        case Value::Kind::Double:
            return "Double(" + std::to_string(v.d) + ")";
        case Value::Kind::Str:
            return "Str(" + std::to_string(v.s.size()) + ") head=[" +
                   HexBytes(BytesOfString(v.s), kDumpHeadBytes) + "]";
        case Value::Kind::Bin:
            return "Bin(" + std::to_string(v.bin.size()) + ") head=[" + HexBytes(v.bin, kDumpHeadBytes) + "]";
        case Value::Kind::Array:
            return "Array(" + std::to_string(v.array.size()) + ")";
        case Value::Kind::Map:
            return "Map(" + std::to_string(v.map.size()) + ")";
        case Value::Kind::Error:
            return "Error(" + v.error + ")";
        case Value::Kind::Timeout:
            return "Timeout";
    }
    return "Unknown";
}

// The result of a reply, with an array's elements spelled out one by one.
std::string DescribeResult(const Value& v) {
    if (v.kind != Value::Kind::Array) return DescribeValue(v);
    std::string s = "array(" + std::to_string(v.array.size()) + ")";
    for (size_t i = 0; i < v.array.size(); ++i) {
        s += " [" + std::to_string(i) + "]=" + DescribeValue(v.array[i]);
    }
    return s;
}

// The frame pull, by method-name prefix: the live path calls
// "ob_rpc_get_frame", and a captured frame's method name carries the
// interface suffix ("ob_rpc_get_frame0-2-12").
bool IsGetFrameMethod(const std::string& method) {
    static const char kPrefix[] = "ob_rpc_get_frame";
    return method.rfind(kPrefix, 0) == 0;
}

// The dump's text for one reply: the method, the kind of the unwrapped
// result, the msgid, the kind of the envelope the reply arrived in, and
// the result's shape.
std::string FormatReplyDump(const std::string& method, const Value& envelope, const Bytes& raw) {
    Value result = Result(envelope);
    std::string msgid = "-";
    if (envelope.kind == Value::Kind::Array && envelope.array.size() >= 2 &&
        envelope.array[1].kind == Value::Kind::UInt) {
        msgid = std::to_string(envelope.array[1].u);
    }
    std::string line = "[dump] reply method=" + method + " kind=" + KindName(result.kind) +
                       " msgid=" + msgid + " envelope=" + KindName(envelope.kind) +
                       " result=" + DescribeResult(result);
    if (IsGetFrameMethod(method)) {
        line += "\n[dump] get_frame raw len=" + std::to_string(raw.size()) +
                " head=[" + HexBytes(raw, kDumpRawBytes) + "]";
    }
    return line;
}

// --dump-replies: off by default.
bool g_dump_replies = false;

// Where the dump goes: stdout by default; the selftest points it at a
// string so the lines can be compared byte for byte.
std::string* g_dump_sink = nullptr;

void DumpReply(const std::string& method, const Value& envelope, const Bytes& raw) {
    if (!g_dump_replies) return;
    std::string text = FormatReplyDump(method, envelope, raw);
    if (g_dump_sink) {
        *g_dump_sink += text + "\n";
    } else {
        std::printf("%s\n", text.c_str());
        std::fflush(stdout);
    }
}

// ---------------------------------------------------------------------------
// The capture bundle (row #140)
//
// --capture DIR records every reply, byte for byte, into a directory
// OUTSIDE the repository: one file per port plus a manifest with the run's
// identity and every file's hash. The bundle is evidence for the hardware
// room and is never committed, so the recorder refuses a directory inside a
// repository.
// ---------------------------------------------------------------------------

// The capture's default bounds. The capture is opt-in; when it is on, it is
// bounded so a run cannot fill the disk.
constexpr int kDefaultCaptureSeconds = 30;
constexpr uint64_t kDefaultCaptureBytes = 64ull * 1024 * 1024;

// The path of this binary, for the manifest's hash.
std::string g_binary_path;

// The capture session: the bundle's lifetime.
class CaptureSession {
public:
    ~CaptureSession() { Stop(); }
    bool Start(const std::string& dir, int seconds_cap, uint64_t bytes_cap) {
        recorder_.reset(new capture::Recorder());
        std::string err;
        if (!recorder_->Open(dir, g_binary_path, seconds_cap, bytes_cap, &err)) {
            std::fprintf(stderr, "[capture] %s\n", err.c_str());
            recorder_.reset();
            return false;
        }
        std::printf("[capture] recording every reply into %s (bounds: %d s, %llu B)\n",
                    recorder_->dir().c_str(), seconds_cap, (unsigned long long)bytes_cap);
        return true;
    }
    capture::Recorder* recorder() { return recorder_.get(); }
    // Close the bundle and write the manifest.
    void Stop() {
        if (!recorder_) return;
        std::string branch, commit;
        capture::ReadGitBranchAndCommit(".", &branch, &commit);
        std::string manifest = recorder_->Close(branch, commit);
        std::printf("[capture] %llu record(s), %llu byte(s) written\n",
                    (unsigned long long)recorder_->RecordCount(),
                    (unsigned long long)recorder_->BytesWritten());
        if (!manifest.empty()) std::printf("[capture] manifest: %s\n", manifest.c_str());
        recorder_.reset();
    }

private:
    std::unique_ptr<capture::Recorder> recorder_;
};

// Read a capture bundle back and print what it holds. The bundle is never
// committed, so this is how it is looked at.
int CaptureDump(const std::string& dir) {
    capture::Reader reader;
    std::string err;
    if (!reader.Open(dir, &err)) {
        std::fprintf(stderr, "[capture-dump] %s\n", err.c_str());
        return 1;
    }
    uint64_t records = 0, bytes = 0;
    capture::Record r;
    while (reader.Next(&r)) {
        ++records;
        bytes += r.bytes.size();
        std::printf("[capture-dump] port=%u ts=%llu method=%s msgid=%llu len=%zu head=[%s]\n", (unsigned)r.port,
                    (unsigned long long)r.ts_ms, r.method.c_str(), (unsigned long long)r.msgid, r.bytes.size(),
                    HexBytes(r.bytes, 32).c_str());
    }
    if (!reader.error().empty()) {
        std::fprintf(stderr, "[capture-dump] %s\n", reader.error().c_str());
        return 1;
    }
    std::printf("[capture-dump] %llu record(s), %llu byte(s) of replies, %zu file(s)\n",
                (unsigned long long)records, (unsigned long long)bytes, reader.files().size());
    return 0;
}

// ---------------------------------------------------------------------------
// The frame array (row #140)
//
// The `get_frame` result is the 11-element array the producer writes; the
// element layout is in docs/recon/findings/frame-codec.md §10.3. Element 4
// is the frame's primary data buffer, and the no-data form is the same
// array with all three buffers empty — a decoder branches on the buffers,
// never on the wire types (§10.4).
// ---------------------------------------------------------------------------
constexpr size_t kFrameArrayElements = 11;
constexpr size_t kFrameBufferElement = 4;   // the primary data buffer
constexpr size_t kFrameBuffer2Element = 6;  // the second data plane
constexpr size_t kFrameBuffer3Element = 8;  // the third data plane

// What a frame result holds.
struct FrameArray {
    bool is_form = false;  // the result is the 11-element array
    bool no_data = false;  // all three buffers are empty: no frame, no error
    size_t band_len = 0;   // element 4's length
    size_t data2_len = 0;  // element 6's length
    size_t data3_len = 0;  // element 8's length
};

FrameArray ParseFrameArray(const Value& result) {
    FrameArray fa;
    if (result.kind != Value::Kind::Array || result.array.size() != kFrameArrayElements) return fa;
    const Value& primary = result.array[kFrameBufferElement];
    const Value& second = result.array[kFrameBuffer2Element];
    const Value& third = result.array[kFrameBuffer3Element];
    // The three data planes are bins; anything else is not this form.
    if (primary.kind != Value::Kind::Bin || second.kind != Value::Kind::Bin || third.kind != Value::Kind::Bin) {
        return fa;
    }
    fa.is_form = true;
    fa.band_len = primary.bin.size();
    fa.data2_len = second.bin.size();
    fa.data3_len = third.bin.size();
    fa.no_data = (fa.band_len == 0 && fa.data2_len == 0 && fa.data3_len == 0);
    return fa;
}

// The dump's summary of a frame result: what the array holds.
std::string FormatFrameSummary(const FrameArray& fa, const Value& result) {
    if (fa.is_form) {
        if (fa.no_data) return "empty";
        return "band(" + std::to_string(fa.band_len) + ")";
    }
    // Not the array form: say what it is, rather than guess.
    return DescribeValue(result);
}

// The per-port frame tally: how many replies carried data and how many were
// empty.
class FrameTally {
public:
    void Add(uint16_t port, const FrameArray& fa) {
        Counts& c = counts_[port];
        if (!fa.is_form) {
            ++c.other;
        } else if (fa.no_data) {
            ++c.empty;
        } else {
            ++c.data;
        }
    }
    // One line per port, with the share of replies that carried data.
    std::string Lines() const {
        std::string out;
        for (const auto& kv : counts_) {
            const Counts& c = kv.second;
            uint64_t frames = c.data + c.empty;
            char share[32];
            if (frames == 0) {
                std::snprintf(share, sizeof(share), "n/a");
            } else {
                std::snprintf(share, sizeof(share), "%.1f%%", 100.0 * (double)c.data / (double)frames);
            }
            if (!out.empty()) out += "\n";
            out += "[dump] tally: port " + std::to_string(kv.first) + " data=" + std::to_string(c.data) +
                   " empty=" + std::to_string(c.empty) + " other=" + std::to_string(c.other) +
                   " (data share " + share + ")";
        }
        return out;
    }
    bool Empty() const { return counts_.empty(); }

private:
    struct Counts {
        uint64_t data = 0;
        uint64_t empty = 0;
        uint64_t other = 0;
    };
    std::map<uint16_t, Counts> counts_;
};

// The dump's frame summary, printed next to the reply's own lines.
void DumpFrameSummary(const FrameArray& fa, const Value& result) {
    if (!g_dump_replies) return;
    std::string text = "[dump] GET_FRAME -> " + FormatFrameSummary(fa, result);
    if (g_dump_sink) {
        *g_dump_sink += text + "\n";
    } else {
        std::printf("%s\n", text.c_str());
        std::fflush(stdout);
    }
}

// The tally, printed at the end of a run.
void DumpTally(const FrameTally& tally) {
    if (!g_dump_replies || tally.Empty()) return;
    std::string text = tally.Lines();
    if (g_dump_sink) {
        *g_dump_sink += text + "\n";
    } else {
        std::printf("%s\n", text.c_str());
        std::fflush(stdout);
    }
}

// The 11-element frame array, as the producer writes it: the three data
// planes and their lengths, and the IR light type.
Bytes FrameArrayResult(const Bytes& band, const Bytes& data2, const Bytes& data3, int64_t light_type) {
    Packer p;
    p.array(kFrameArrayElements);
    p.uint(0);
    p.uint(0);
    p.uint(0);
    p.uint(255);
    p.bin(band);
    p.uint(band.size());
    p.bin(data2);
    p.uint(data2.size());
    p.bin(data3);
    p.uint(data3.size());
    p.integer(light_type);
    return p.out;
}

// The msgpack-RPC envelope around a result.
Bytes Envelope(uint64_t msgid, const Bytes& result) {
    Packer p;
    p.array(4);
    p.uint(1);
    p.uint(msgid);
    p.nil();
    p.raw(result.data(), result.size());
    return p.out;
}

// ---------------------------------------------------------------------------
// Interface routing (rows #120 and #157)
//
// A method name ends in a suffix that identifies the device and the
// interface it addresses (protocol-spec.md §1.1): the device id is
// "0-2-1" and the interface index follows it — "0-2-12" is interface 2 (IR),
// "0-2-10" is interface 0 (depth). Each interface has its own TCP port on
// the daemon, so a client that mixes interfaces must use one connection
// per interface, not one connection for everything.
//
// The replay reads the suffix out of the captured frame and never
// re-encodes it (row #120); the live path resolves its port to the
// interface and builds every method name with that interface's suffix
// (row #157).
// ---------------------------------------------------------------------------

// The two interfaces the capture file exercises, and the port each was
// observed on (protocol-spec.md §1.1: depth 10010, IR 10013).
struct InterfaceRoute {
    std::string suffix;  // the method-name suffix, e.g. "0-2-12"
    std::string name;    // a short label for the diagnostics
    uint16_t port;       // the daemon port for that interface
    // The first parameter of start_stream/stop_stream for this interface:
    // 6 on IR, 7 on depth (protocol-spec.md §1.2/§7). It is the STREAM
    // TYPE, not a start/stop command — the method name carries that.
    uint32_t stream_type;
};

// The suffix→interface→port table. The ports are overridable on the
// command line (--ir-port / --depth-port) for a daemon on other ports.
class RouteTable {
public:
    RouteTable() {
        routes_.push_back({"0-2-12", "IR", 10013, 6});
        routes_.push_back({"0-2-10", "depth", 10010, 7});
    }
    const std::vector<InterfaceRoute>& routes() const { return routes_; }
    // The index of a suffix in the table, or -1 when it is not one this
    // client knows.
    int IndexOfSuffix(const std::string& suffix) const {
        for (size_t i = 0; i < routes_.size(); ++i) {
            if (routes_[i].suffix == suffix) return (int)i;
        }
        return -1;
    }
    // The index of an interface by its label, or -1.
    int IndexOfName(const std::string& name) const {
        for (size_t i = 0; i < routes_.size(); ++i) {
            if (routes_[i].name == name) return (int)i;
        }
        return -1;
    }
    // Point an interface at another port. Returns false for an unknown
    // name, so a typo is an error rather than a silent no-op.
    bool SetPortByName(const std::string& name, uint16_t port) {
        int i = IndexOfName(name);
        if (i < 0) return false;
        routes_[(size_t)i].port = port;
        return true;
    }
    // The known suffixes, for an error message.
    std::string KnownSuffixes() const {
        std::string s;
        for (const InterfaceRoute& r : routes_) {
            if (!s.empty()) s += ", ";
            s += r.suffix;
        }
        return s;
    }

    // The known interfaces, for an error message: name, suffix and port.
    std::string KnownRoutes() const {
        std::string s;
        for (const InterfaceRoute& r : routes_) {
            if (!s.empty()) s += ", ";
            s += r.name + " " + r.suffix + " -> " + std::to_string(r.port);
        }
        return s;
    }

private:
    std::vector<InterfaceRoute> routes_;
};

// The route for a port, or nullptr when the port is not one of the known
// interfaces' ports. The live path resolves its interface this way: the
// port picks the interface, and the interface's suffix is what the
// daemon's method names carry (protocol-spec.md §1.1).
const InterfaceRoute* RouteForPort(const RouteTable& table, uint16_t port) {
    for (const InterfaceRoute& r : table.routes()) {
        if (r.port == port) return &r;
    }
    return nullptr;
}

// The error text for a port that is not an interface port. It names the
// map, so a typo is a message and not a silent default to the wrong
// interface.
std::string UnknownPortMessage(const RouteTable& table, uint16_t port) {
    return "port " + std::to_string(port) + " is not an interface port (known: " + table.KnownRoutes() + ")";
}

// The daemon's method names carry the interface suffix (protocol-spec.md
// §1.1): "ob_rpc_start_stream0-2-12" addresses the IR interface, and a
// name without a suffix is a name the daemon does not have — it answers
// "could not find function '<name>' with argument count <n>". Every
// live-path call is therefore "<base><suffix>".
std::string MethodFor(const InterfaceRoute& route, const char* base) {
    return std::string(base) + route.suffix;
}

// The stream parameters: [streamtype, format, width, height, fps]
// (protocol-spec.md §1.2/§7). The first field is the interface's own
// stream type (6 IR, 7 depth) and the same array is sent to stop.
Bytes StreamParams(const InterfaceRoute& route, uint32_t w, uint32_t h, uint32_t fps) {
    Packer p;
    p.array(5);
    p.uint(route.stream_type); p.uint(9); p.uint(w); p.uint(h); p.uint(fps);
    return p.out;
}

Value StartStream(RpcClient& c, const InterfaceRoute& route, uint32_t w, uint32_t h, uint32_t fps) {
    // Unwrap the reply envelope: the result is the payload at index 3.
    const std::string method = MethodFor(route, "ob_rpc_start_stream");
    Value envelope = c.call(method, StreamParams(route, w, h, fps));
    DumpReply(method, envelope, c.lastRaw());
    return Result(envelope);
}

// The stop is its own method — the method name carries start/stop, not
// the parameters (protocol-spec.md §7). The parameters are the same
// array the start sent, so the stream type is the interface's own.
Value StopStream(RpcClient& c, const InterfaceRoute& route, uint32_t w, uint32_t h, uint32_t fps) {
    const std::string method = MethodFor(route, "ob_rpc_stop_stream");
    Value envelope = c.call(method, StreamParams(route, w, h, fps));
    DumpReply(method, envelope, c.lastRaw());
    return Result(envelope);
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

// Sends one SET_PROPERTY. Returns false when the daemon did not answer
// within the bound (row #161): a timeout ends the run instead of being
// retried five times per frame pull.
bool SetProperty(RpcClient& c, const InterfaceRoute& route, uint32_t prop, int32_t value, uint16_t request_id,
                 bool log) {
    Bytes vendor = BuildVendorRequest(kOpSetProperty, request_id, SetPropertyPayload(prop, value));
    Packer p;
    p.array(2);
    p.bin(vendor);
    p.uint(64);  // expected response size for an ack (spec §2.1)
    const std::string method = MethodFor(route, "ob_rpc_send_data");
    Value envelope = c.call(method, p.out);
    DumpReply(method, envelope, c.lastRaw());
    Value r = Result(envelope);
    uint16_t opcode = 0, reqid = 0, err = 0xffff;
    bool accepted = VendorAccepted(r, &opcode, &reqid, &err);
    if (log) {
        if (r.kind == Value::Kind::Error) {
            std::printf("[live] SET prop=%u value=%d reqid=%u -> rpc ERROR: %s\n", prop, value, request_id,
                        r.error.c_str());
        } else if (r.kind == Value::Kind::Bin) {
            std::printf("[live] SET prop=%u value=%d reqid=%u -> vendor %s (opcode=%u reqid=%u err=%u, %zu B)\n",
                        prop, value, request_id, accepted ? "ACCEPT" : "REJECT", opcode, reqid, err, r.bin.size());
        } else if (r.kind == Value::Kind::Timeout) {
            std::printf("[live] SET prop=%u value=%d reqid=%u -> rpc TIMEOUT\n", prop, value, request_id);
        } else {
            std::printf("[live] SET prop=%u value=%d reqid=%u -> rpc kind=%d\n", prop, value, request_id,
                        (int)r.kind);
        }
        std::fflush(stdout);
    }
    return r.kind != Value::Kind::Timeout;
}

// The number of property sets the live path sends per frame pull (the
// laser trio and the exposure pair, protocol-spec §3.1).
constexpr int kLivePropertySets = 5;

// What a live run saw. The counters are the run's verdict: a run that
// pulled frames is a success, whatever shape the replies had.
struct LiveStats {
    uint64_t get_calls = 0;
    uint64_t data = 0;    // replies that carried a frame
    uint64_t empty = 0;   // replies that carried none
    uint64_t errors = 0;  // error replies
    bool started = false;  // start_stream was accepted
    bool stopped = false;  // stop_stream was sent
    bool timed_out = false;  // a bounded wait expired (row #161)
};

// Row #161: a bounded wait that expires is a named rejection, not a hang.
// The line names the endpoint, the interface, the unanswered method and the
// bound, so the depth retry's record says where the run stopped.
std::string LiveTimeoutMessage(const std::string& host, uint16_t port, const InterfaceRoute& route,
                               const std::string& method, int timeout_ms) {
    return "[live] TIMEOUT: " + host + ":" + std::to_string(port) + " (" + route.name + ", suffix " +
           route.suffix + ") " + method + ": no complete reply within " + std::to_string(timeout_ms) +
           " ms - the connection was established and the daemon did not answer";
}

// The TCP handshake's own expiry, before any method can be sent.
std::string LiveConnectTimeoutMessage(const std::string& host, uint16_t port, int timeout_ms) {
    return "[live] TIMEOUT: " + host + ":" + std::to_string(port) + ": no TCP connection within " +
           std::to_string(timeout_ms) + " ms";
}

// The live session: ONE interface, ONE connection.
//
// The port picks the interface (protocol-spec.md §1.1): every method name
// on this connection carries that interface's suffix, because the daemon
// has no method without one. The daemon serves one interface per port, so
// a run streams the interface its port names and nothing else — a second
// interface is a second run.
//
// timeout_ms bounds the connect and every reply read (row #161). A run
// whose daemon stops answering returns kExitLiveTimeout with the method
// named; it does not wait on the socket.
int Live(const std::string& host, uint16_t port, const InterfaceRoute& route, int seconds,
         const std::string& out_path, int pull_ms, int max_frames, bool log, capture::Recorder* capture,
         LiveStats* stats, int timeout_ms) {
    LiveStats local;
    LiveStats& st = stats ? *stats : local;
    RpcClient rpc;
    rpc.set_timeout(timeout_ms);
    if (!rpc.connect(host, port)) {
        if (rpc.connect_timed_out()) {
            std::fprintf(stderr, "%s\n", LiveConnectTimeoutMessage(host, port, timeout_ms).c_str());
            st.timed_out = true;
            return kExitLiveTimeout;
        }
        std::fprintf(stderr, "[live] cannot connect to %s:%u (%s; is RPCServer running?)\n", host.c_str(), port,
                     rpc.connect_error().c_str());
        return kExitLiveFailed;
    }
    rpc.set_capture(capture);
    std::printf("[live] connected %s:%u (%s, suffix %s, pull_ms=%d, per-call logging=%s, timeout %d ms)\n",
                host.c_str(), port, route.name.c_str(), route.suffix.c_str(), pull_ms, log ? "on" : "off",
                timeout_ms);
    // The line must reach the record before the first blocking call: if the
    // next read stalls, the log already says where the run got to.
    std::fflush(stdout);
    const std::string start_method = MethodFor(route, "ob_rpc_start_stream");
    std::printf("[live] calling %s\n", start_method.c_str());
    std::fflush(stdout);
    Value started = StartStream(rpc, route, 1920, 1200, 50);
    if (started.kind == Value::Kind::Timeout) {
        std::fprintf(stderr, "%s\n", LiveTimeoutMessage(host, port, route, start_method, timeout_ms).c_str());
        st.timed_out = true;
        return kExitLiveTimeout;
    }
    if (started.kind == Value::Kind::Error) {
        std::fprintf(stderr, "[live] start_stream failed: %s\n", started.error.c_str());
        return kExitLiveFailed;
    }
    st.started = true;
    std::printf("[live] start_stream -> %s\n", started.kind == Value::Kind::Bool && started.b ? "true" : "non-error");
    std::fflush(stdout);

    const std::string get_frame_method = MethodFor(route, "ob_rpc_get_frame");
    uint16_t request_id = 0;
    bool saved = false;
    FrameTally tally;
    auto last_log = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (max_frames > 0 && st.get_calls >= (uint64_t)max_frames) break;
        Packer p;
        p.array(0);
        // The reply is the envelope [1, msgid, nil, result]; the frame is
        // the payload at index 3, so unwrap before looking at it.
        Value envelope = rpc.call(get_frame_method, p.out);
        if (envelope.kind == Value::Kind::Timeout) {
            std::fprintf(stderr, "%s\n", LiveTimeoutMessage(host, port, route, get_frame_method, timeout_ms).c_str());
            st.timed_out = true;
            break;
        }
        DumpReply(get_frame_method, envelope, rpc.lastRaw());
        Value frame = Result(envelope);
        // The result is the 11-element array: what it holds is the band's
        // length, or the no-data form.
        FrameArray fa = ParseFrameArray(frame);
        DumpFrameSummary(fa, frame);
        tally.Add(port, fa);
        ++st.get_calls;
        // The bytes that carry the frame: the primary data buffer of the
        // array form, or the whole payload of the older bare-bin form.
        const Bytes* payload = nullptr;
        if (fa.is_form) {
            payload = &frame.array[kFrameBufferElement].bin;
        } else if (frame.kind == Value::Kind::Bin) {
            payload = &frame.bin;
        }
        bool should_log = false;
        if (frame.kind == Value::Kind::Error) {
            ++st.errors;
            should_log = true;  // always show errors
        } else if (payload && !payload->empty()) {
            ++st.data;
            if (!saved) {
                std::ofstream f(out_path, std::ios::binary);
                f.write(reinterpret_cast<const char*>(payload->data()), (std::streamsize)payload->size());
                std::printf("[live] saved first frame: %s (%zu bytes)\n", out_path.c_str(), payload->size());
                saved = true;
                should_log = true;
            } else {
                auto now = std::chrono::steady_clock::now();
                should_log = now - last_log >= std::chrono::milliseconds(250);
            }
        } else {
            ++st.empty;
            should_log = st.get_calls <= 5 || (st.get_calls % 100 == 0);
        }
        if (log && should_log) {
            last_log = std::chrono::steady_clock::now();
            if (frame.kind == Value::Kind::Error) {
                std::printf("[live] GET_FRAME #%llu -> ERROR: %s\n", (unsigned long long)st.get_calls,
                            frame.error.c_str());
            } else if (payload && !payload->empty()) {
                std::printf("[live] GET_FRAME #%llu -> %s(%zu) head=[%s]\n", (unsigned long long)st.get_calls,
                            fa.is_form ? "band" : "bin", payload->size(), HexHead(*payload).c_str());
            } else {
                std::printf("[live] GET_FRAME #%llu -> empty (kind=%d)\n", (unsigned long long)st.get_calls,
                            (int)frame.kind);
            }
            std::fflush(stdout);
        }
        // §3.1 host-driven loops: laser trio and exposure, at the observed cadence.
        const uint32_t props[kLivePropertySets] = {3, 152, 153, 154, 155};
        const int32_t values[kLivePropertySets] = {0, 0, 1, 866000, 866000};
        for (int k = 0; k < kLivePropertySets; ++k) {
            if (!SetProperty(rpc, route, props[k], values[k], ++request_id, log)) {
                std::fprintf(stderr, "%s\n",
                             LiveTimeoutMessage(host, port, route, MethodFor(route, "ob_rpc_send_data"), timeout_ms)
                                 .c_str());
                st.timed_out = true;
                break;
            }
        }
        if (st.timed_out) break;
        if (pull_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(pull_ms));
        // The capture's bounds end the run when they are hit, so a capture
        // cannot outlive its own budget.
        if (capture && capture->Capped()) {
            std::printf("[live] the capture's bound was reached; stopping\n");
            break;
        }
    }
    // The stop is its own method, with the same parameters the start
    // sent (protocol-spec.md §7). A connection that already timed out is
    // not asked to stop: that would spend a second bound on a daemon that
    // has shown it is not answering.
    if (st.timed_out) {
        std::printf("[live] stop_stream skipped: the connection timed out\n");
    } else {
        Value stopped = StopStream(rpc, route, 1920, 1200, 50);
        if (stopped.kind == Value::Kind::Timeout) {
            std::fprintf(stderr, "%s\n",
                         LiveTimeoutMessage(host, port, route, MethodFor(route, "ob_rpc_stop_stream"), timeout_ms)
                             .c_str());
            st.timed_out = true;
        } else {
            st.stopped = stopped.kind != Value::Kind::Error;
            if (stopped.kind == Value::Kind::Error) {
                std::fprintf(stderr, "[live] stop_stream failed: %s\n", stopped.error.c_str());
            }
        }
    }
    std::printf("[live] done: get_frame=%llu data=%llu empty=%llu error=%llu; %s\n",
                (unsigned long long)st.get_calls, (unsigned long long)st.data, (unsigned long long)st.empty,
                (unsigned long long)st.errors, st.timed_out ? "timed out, stream not stopped" : "stream stopped");
    DumpTally(tally);
    if (st.timed_out) return kExitLiveTimeout;
    return st.data > 0 ? 0 : kExitLiveFailed;
}

// Prints the exact method names and bytes each call would send, so the
// logging path is exercisable without the daemon (the row's "what can be
// tested locally"). The names carry the interface suffix: the daemon has
// no method without one.
int DryRunLive() {
    int failures = 0;
    auto show = [&](const std::string& what, const Bytes& b) {
        std::printf("[dry-run-live] %-42s %zu B: %s\n", what.c_str(), b.size(), HexHead(b, 24).c_str());
        if (b.empty()) ++failures;
    };
    RouteTable routes;
    for (const InterfaceRoute& route : routes.routes()) {
        std::printf("[dry-run-live] %s (suffix %s, port %u):\n", route.name.c_str(), route.suffix.c_str(),
                    (unsigned)route.port);
        show(MethodFor(route, "ob_rpc_start_stream") + " params", StreamParams(route, 1920, 1200, 50));
        show(MethodFor(route, "ob_rpc_stop_stream") + " params", StreamParams(route, 1920, 1200, 50));
        Packer getf;
        getf.array(0);
        show(MethodFor(route, "ob_rpc_get_frame") + " params", getf.out);
    }
    for (uint32_t prop : {3u, 152u, 153u, 154u, 155u}) {
        Bytes vendor = BuildVendorRequest(kOpSetProperty, (uint16_t)(prop & 0xffff), SetPropertyPayload(prop, 0));
        show("ob_rpc_send_data vendor (prop " + std::to_string(prop) + ")", vendor);
    }
    std::printf("[dry-run-live] %s (%d failure(s))\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// --replay: send recon's captured request frames, unchanged, in order
//
// Input format (text, one frame per line):
//   [+<ms> ]<hex bytes>      optional '+<ms>' prefix; the space after the
//                            prefix is required (it separates it from the hex
//                            payload)
//   # comment / blank lines are ignored
//
// The '+<ms>' prefix is an ABSOLUTE capture timestamp — milliseconds since
// the capture started — NOT a delay before that frame. (See
// docs/recon/findings/streaming-replay-payloads.md, whose table gives the
// same instants as t=25.xxx s.) The replay therefore paces on the
// INTER-ARRIVAL delta between consecutive timestamps; sleeping a timestamp
// itself would wait the whole capture offset before every frame.
//
// A frame with no '+<ms>' prefix carries no timestamp and is sent with no
// wait.
//
// Bytes are the raw msgpack-RPC request frame as captured (e.g. the 0x94…
// [0, msgid, method, params] frame). We do not re-encode; this is the vendor's
// own bytes.
// ---------------------------------------------------------------------------

// A capture timestamp is bounded to a sane range (~11 days) so a malformed
// prefix cannot overflow the accumulator.
constexpr int64_t kMaxStampMs = 1000000000LL;

// The wait before the FIRST frame is capped. The first stamp is measured
// from the start of the capture, which is recon's clock and not a deadline
// the daemon shares — so waiting it out in full would stall the replay
// before it sends anything. The inter-frame deltas from the second frame on
// are used as captured.
constexpr int64_t kFirstFrameWaitCapMs = 250;

// How long a single reply read may take on the replay path (row #117).
// The daemon answers in well under this; the bound exists so a missing
// reply fails loudly, naming the frame, instead of hanging forever.
constexpr int kDefaultReplyTimeoutMs = 2000;

bool ParseReplayFile(const std::string& path, std::vector<Bytes>* frames, std::vector<int64_t>* stamps,
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
        int64_t stamp = -1;  // -1: this frame carries no timestamp
        std::string hex = line;
        if (hex[0] == '+') {
            // Split on the first whitespace: "+<ms>" is the timestamp, the
            // rest is the hex payload. Scanning digits across the space was
            // the bug (hex bytes 94/00… are also decimal characters).
            size_t sp = hex.find_first_of(" \t");
            if (sp == std::string::npos) {
                *err = "line " + std::to_string(lineno) + ": '+<ms>' must be followed by whitespace and hex";
                return false;
            }
            std::string prefix = hex.substr(1, sp - 1);
            if (prefix.empty()) { *err = "line " + std::to_string(lineno) + ": empty timestamp"; return false; }
            int64_t value = 0;
            for (char c : prefix) {
                if (!std::isdigit((unsigned char)c)) {
                    *err = "line " + std::to_string(lineno) + ": non-digit in timestamp";
                    return false;
                }
                value = value * 10 + (c - '0');
                if (value > kMaxStampMs) {
                    *err = "line " + std::to_string(lineno) + ": timestamp out of range";
                    return false;
                }
            }
            stamp = value;
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
        stamps->push_back(stamp);
    }
    return true;
}

// The inter-arrival deltas between the capture's absolute timestamps: the
// first frame's delta is measured from the capture start (t=0), each later
// frame's from the frame before it. A timestamp that goes backwards (the
// capture file groups frames by call type, not by time) clamps to 0 — a
// replay never waits a negative time — and is counted in *backward_steps.
std::vector<int64_t> ToInterFrameDeltas(const std::vector<int64_t>& stamps, int* backward_steps) {
    std::vector<int64_t> deltas;
    deltas.reserve(stamps.size());
    int64_t prev = 0;
    for (size_t i = 0; i < stamps.size(); ++i) {
        if (stamps[i] < 0) { deltas.push_back(0); continue; }  // no timestamp: no wait
        int64_t d = stamps[i] - prev;
        if (d < 0) {
            d = 0;
            if (backward_steps) ++(*backward_steps);
        }
        deltas.push_back(d);
        prev = stamps[i];
    }
    return deltas;
}

// The wait before the first frame: its delta is measured from the capture's
// t=0 and is capped (see kFirstFrameWaitCapMs).
int64_t FirstFrameWaitMs(int64_t first_delta_ms) {
    if (first_delta_ms > kFirstFrameWaitCapMs) return kFirstFrameWaitCapMs;
    return first_delta_ms < 0 ? 0 : first_delta_ms;
}

// The method name of a captured request frame. The frame is a msgpack
// array [0, msgid, method, params]; this reads the method out of it
// WITHOUT re-encoding — the bytes the caller sends are untouched.
//
// The method is a msgpack string in the third slot. Rather than decode
// the whole frame (whose params can hold arbitrary binary), this walks
// the array header and the two leading scalars and then reads the string
// header. Returns false if the frame is not shaped like a request.
bool MethodNameOfFrame(const Bytes& frame, std::string* method) {
    size_t i = 0;
    if (frame.size() < 2) return false;
    uint8_t tag = frame[i];
    if ((tag & 0xf0) != 0x90) return false;  // not a msgpack array
    size_t count = tag & 0x0f;
    if (count < 3) return false;  // need [0, msgid, method, ...]
    ++i;
    // Skip element 0 (the request marker) and element 1 (the msgid).
    for (int e = 0; e < 2; ++e) {
        if (i >= frame.size()) return false;
        uint8_t t = frame[i];
        if (t <= 0x7f) { i += 1; }
        else if (t >= 0xe0) { i += 1; }
        else if (t == 0xcc) { i += 2; }
        else if (t == 0xcd) { i += 3; }
        else if (t == 0xce) { i += 5; }
        else if (t == 0xcf) { i += 9; }
        else return false;
        if (i > frame.size()) return false;
    }
    if (i >= frame.size()) return false;
    uint8_t t = frame[i];
    size_t len = 0;
    size_t hdr = 0;
    if (t >= 0xa0 && t <= 0xbf) { len = t & 0x1f; hdr = 1; }
    else if (t == 0xd9) { if (i + 1 >= frame.size()) return false; len = frame[i + 1]; hdr = 2; }
    else if (t == 0xda) {
        if (i + 2 >= frame.size()) return false;
        len = ((size_t)frame[i + 1] << 8) | frame[i + 2];
        hdr = 3;
    } else {
        return false;  // not a string
    }
    if (i + hdr + len > frame.size()) return false;
    method->assign(reinterpret_cast<const char*>(frame.data() + i + hdr), len);
    return true;
}

// The interface suffix of a method name: the trailing "0-2-1<index>" the
// device and interface share (protocol-spec.md §1.1). Returns false when
// the name carries no such suffix.
bool InterfaceSuffixOfMethod(const std::string& method, std::string* suffix) {
    // The suffix is the trailing run of digits and dashes that ends the
    // name; the method part before it is letters/underscores. A name that
    // is nothing but the token counts as carrying it.
    size_t end = method.size();
    size_t start = end;
    while (start > 0) {
        char c = method[start - 1];
        if ((c >= '0' && c <= '9') || c == '-') start--;
        else break;
    }
    if (start == end) return false;  // no trailing digits at all
    *suffix = method.substr(start, end - start);
    return true;
}

// The route for a method's interface suffix, or nullptr when the suffix is
// not one this client knows how to reach.
const InterfaceRoute* RouteForSuffix(const RouteTable& table, const std::string& suffix) {
    int i = table.IndexOfSuffix(suffix);
    return i < 0 ? nullptr : &table.routes()[(size_t)i];
}

// The route a frame goes to, read from the method name in the frame. On
// failure *err explains why, naming the frame.
bool RouteForFrame(const RouteTable& table, const Bytes& frame, size_t frame_index,
                   const InterfaceRoute** route, std::string* suffix_out, std::string* err) {
    std::string method;
    if (!MethodNameOfFrame(frame, &method)) {
        *err = "frame " + std::to_string(frame_index) +
               ": not a msgpack-RPC request frame (cannot read the method name)";
        return false;
    }
    std::string suffix;
    if (!InterfaceSuffixOfMethod(method, &suffix)) {
        *err = "frame " + std::to_string(frame_index) + ": method '" + method +
               "' has no interface suffix";
        return false;
    }
    const InterfaceRoute* r = RouteForSuffix(table, suffix);
    if (!r) {
        *err = "frame " + std::to_string(frame_index) + ": unknown interface suffix '" + suffix +
               "' (known: " + table.KnownSuffixes() + ")";
        return false;
    }
    *route = r;
    *suffix_out = suffix;
    return true;
}

// Regression for the row #105 parser bug: '+25429 94000d…' — the space after
// the timestamp must delimit it, or the digit scanner eats 94000 (hex chars
// that are also decimal digits).
int ReplayParserSelftest() {
    const char* line = "+25429 94000db66f625f7270635f6765745f6672616d65302d322d313290\n";
    char path[] = "/tmp/raptor_replay_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { std::printf("[replay-selftest] SKIP (mkstemp failed)\n"); return 0; }
    ssize_t ignored = write(fd, line, std::strlen(line));
    (void)ignored;
    close(fd);
    std::vector<Bytes> frames;
    std::vector<int64_t> stamps;
    std::string err;
    bool ok = ParseReplayFile(path, &frames, &stamps, &err);
    unlink(path);
    bool pass = ok && frames.size() == 1 && stamps.size() == 1 && stamps[0] == 25429 && !frames[0].empty();
    std::printf("[replay-selftest] delay-then-hex-digits %s%s%s\n", pass ? "PASS" : "FAIL",
                pass ? "" : ": ", pass ? "" : err.c_str());
    return pass ? 0 : 1;
}

// Row #117: the '+<ms>' prefix is an ABSOLUTE capture timestamp, so the
// replay's pacing is the inter-arrival DELTA between consecutive stamps —
// not the stamp itself. Sleeping the stamp would wait the whole capture
// offset before every frame (row #105's replay sent 5 frames in 2+ minutes
// and looked hung).
//
// Pins the conversion on the capture's own first three stamps (see
// docs/recon/findings/streaming-replay-payloads.md): '+25259', '+25261',
// '+25281' are the capture's first three instants, so the waits before them
// are 25259 ms (from t=0), then 2 ms, then 20 ms.
int ReplayDeltaSelftest() {
    const char* body =
        "+25259 940001d9206f625f7365745f69725f6672616d655f6c696768745f74797065302d322d31329206cdffff\n"
        "+25261 9400cc8cd9206f625f7365745f69725f6672616d655f6c696768745f74797065302d322d31309207cdffff\n"
        "+25281 9400cc9fb66f625f7270635f73656e645f64617461302d322d313092b0474d04000200af009d0000000000000040\n";
    char path[] = "/tmp/raptor_delta_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { std::printf("[replay-selftest] SKIP (mkstemp failed)\n"); return 0; }
    ssize_t ignored = write(fd, body, std::strlen(body));
    (void)ignored;
    close(fd);

    std::vector<Bytes> frames;
    std::vector<int64_t> stamps;
    std::string err;
    bool ok = ParseReplayFile(path, &frames, &stamps, &err);
    unlink(path);
    int failures = 0;
    auto check = [&](const char* name, bool pass) {
        std::printf("[replay-selftest] %-52s %s\n", name, pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    };

    check("parses three frames with absolute stamps",
          ok && frames.size() == 3 && stamps.size() == 3);
    if (!ok || stamps.size() != 3) {
        std::printf("[replay-selftest] %s\n", err.c_str());
        return 1;
    }
    check("stamps are the absolute capture timestamps",
          stamps[0] == 25259 && stamps[1] == 25261 && stamps[2] == 25281);

    int backward = 0;
    std::vector<int64_t> deltas = ToInterFrameDeltas(stamps, &backward);
    check("deltas are 25259, 2, 20 ms (not 25259 three times)",
          deltas.size() == 3 && deltas[0] == 25259 && deltas[1] == 2 && deltas[2] == 20);
    check("no backward steps in a monotonic file", backward == 0);

    // The first frame's wait comes from the capture's t=0, which the daemon
    // does not share: it is capped.
    check("first-frame wait is capped", FirstFrameWaitMs(deltas[0]) == kFirstFrameWaitCapMs);
    check("a small first delta is not inflated", FirstFrameWaitMs(7) == 7);

    // A stamp that goes backwards (the capture file groups frames by call
    // type, not by time) must clamp to 0, never go negative.
    std::vector<int64_t> unordered_stamps = {25259, 25300, 25200, 25250};
    int backward2 = 0;
    std::vector<int64_t> d2 = ToInterFrameDeltas(unordered_stamps, &backward2);
    check("a backward stamp clamps to 0 ms",
          d2.size() == 4 && d2[0] == 25259 && d2[1] == 41 && d2[2] == 0 && d2[3] == 50);
    check("backward steps are counted", backward2 == 1);

    // A frame with no timestamp waits 0 and does not move the cursor.
    std::vector<int64_t> sparse = {100, -1, 130};
    int backward3 = 0;
    std::vector<int64_t> d3 = ToInterFrameDeltas(sparse, &backward3);
    check("a frame with no timestamp waits 0 ms",
          d3.size() == 3 && d3[0] == 100 && d3[1] == 0 && d3[2] == 30);

    std::printf("[replay-selftest] %s (%d failure(s))\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}

// Row #120: the interface suffix parse and the suffix→port routing table,
// no device. The capture file mixes interfaces (13 IR, 23 depth — see
// docs/recon/findings/streaming-replay-payloads.md §6), so a frame must
// go out on its own interface's port.
//
// The frames here are the capture's own bytes, so the test pins the real
// method names: "ob_rpc_get_frame0-2-12" (IR) and
// "ob_rpc_get_frame0-2-10" (depth).
int ReplayInterfaceSelftest() {
    int failures = 0;
    auto check = [&](const char* name, bool pass) {
        std::printf("[replay-selftest] %-52s %s\n", name, pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    };

    // The two captured IR frames: the same request on the two interfaces.
    const char* ir_hex = "940005b66f625f7270635f6765745f6672616d65302d322d313290";
    const char* depth_hex = "9400cca3b66f625f7270635f6765745f6672616d65302d322d313090";
    auto hexbytes = [](const char* h) { return FromHex(h); };
    Bytes ir = hexbytes(ir_hex);
    Bytes depth = hexbytes(depth_hex);
    check("the captured frames parse to bytes", ir.size() == 27 && depth.size() == 28);

    // The method name is read out of the frame, without re-encoding it.
    std::string method;
    check("reads the IR method name", MethodNameOfFrame(ir, &method) && method == "ob_rpc_get_frame0-2-12");
    check("reads the depth method name",
          MethodNameOfFrame(depth, &method) && method == "ob_rpc_get_frame0-2-10");

    // The frames are untouched by the parse.
    check("parsing does not alter the frame bytes", ir == hexbytes(ir_hex) && depth == hexbytes(depth_hex));

    // The suffix is the trailing device+interface token.
    std::string suffix;
    check("extracts the IR suffix",
          InterfaceSuffixOfMethod("ob_rpc_get_frame0-2-12", &suffix) && suffix == "0-2-12");
    check("extracts the depth suffix",
          InterfaceSuffixOfMethod("ob_rpc_get_frame0-2-10", &suffix) && suffix == "0-2-10");
    check("a name with no suffix yields none",
          !InterfaceSuffixOfMethod("ob_get_device_enumeration_info", &suffix));
    check("a name that is only the token still carries it",
          InterfaceSuffixOfMethod("0-2-12", &suffix) && suffix == "0-2-12");

    // The routing table: the observed suffix→port map.
    RouteTable routes;
    const InterfaceRoute* route = nullptr;
    std::string got;
    std::string err;
    check("the IR frame routes to the IR port",
          RouteForFrame(routes, ir, 1, &route, &got, &err) && route->port == 10013 &&
              route->name == "IR");
    check("the depth frame routes to the depth port",
          RouteForFrame(routes, depth, 1, &route, &got, &err) && route->port == 10010 &&
              route->name == "depth");

    // An unknown suffix is an error naming the frame, not a silent default.
    Bytes unknown = hexbytes("940005b66f625f7270635f6765745f6672616d65302d322d323390");
    check("an unknown suffix is rejected", !RouteForFrame(routes, unknown, 7, &route, &got, &err));
    check("the error names the frame and the suffix",
          err.find("frame 7") != std::string::npos && err.find("0-2-23") != std::string::npos);
    check("a frame with no suffix is rejected",
          !RouteForFrame(routes, hexbytes("940005b66f625f7265706c7920696e666f0"), 3, &route, &got, &err));
    check("a frame that is not a request is rejected",
          !RouteForFrame(routes, Bytes{0xc4, 0x01, 0x00}, 2, &route, &got, &err));

    // The ports are overridable.
    check("--ir-port overrides the IR port", routes.SetPortByName("IR", 20013));
    check("--depth-port overrides the depth port", routes.SetPortByName("depth", 20010));
    check("the overridden IR port is used",
          RouteForFrame(routes, ir, 1, &route, &got, &err) && route->port == 20013);
    check("the overridden depth port is used",
          RouteForFrame(routes, depth, 1, &route, &got, &err) && route->port == 20010);
    check("an unknown interface name is rejected", !routes.SetPortByName("RGB", 10012));

    std::printf("[replay-selftest] %s (%d failure(s))\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}

// Row #122: a reply is the msgpack-RPC envelope [1, msgid, nil, result], so
// the payload is at index 3 — the frame paths must unwrap before they look
// at it, or every reply reads as the envelope array.
//
// The shapes here are the ones the daemon sends (protocol-spec.md §1.1/§1.3):
// a success carries nil in the error slot and the result at index 3, and an
// error carries the error in the error slot.
int ReplyResultSelftest() {
    int failures = 0;
    auto check = [&](const char* name, bool pass) {
        std::printf("[replay-selftest] %-52s %s\n", name, pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    };

    // [1, msgid, nil, bin(3)] — a successful reply carrying a payload.
    Bytes payload = {0x0a, 0x0b, 0x0c};
    Packer ok_p;
    ok_p.array(4);
    ok_p.uint(1);
    ok_p.uint(7);
    ok_p.nil();
    ok_p.bin(payload);
    Bytes ok_bytes = ok_p.out;
    Reader ok_reader(ok_bytes.data(), ok_bytes.size());
    Value ok_envelope = ok_reader.parse();

    check("the synthetic reply parses as an envelope", ok_reader.ok() && ok_envelope.kind == Value::Kind::Array);
    check("the envelope itself is not a bin", ok_envelope.kind != Value::Kind::Bin);

    Value ok_result = Result(ok_envelope);
    check("an envelope with a payload yields the payload",
          ok_result.kind == Value::Kind::Bin && ok_result.bin.size() == 3);

    // [1, msgid, "boom", nil] — an error reply: the error is in the error
    // slot, and the result slot is nil.
    Packer err_p;
    err_p.array(4);
    err_p.uint(1);
    err_p.uint(7);
    err_p.str("boom");
    err_p.nil();
    Bytes err_bytes = err_p.out;
    Reader err_reader(err_bytes.data(), err_bytes.size());
    Value err_envelope = err_reader.parse();
    Value err_result = Result(err_envelope);
    check("an envelope with an error slot is an error", err_result.kind == Value::Kind::Error);
    check("the error text is carried through", err_result.error == "boom");

    // A bare payload (not an envelope) is returned unchanged, so a peer that
    // answers without the envelope still works.
    Value bare = ok_result;
    check("a bare payload is returned unchanged", Result(bare).kind == Value::Kind::Bin);

    // A reply that is not a response envelope at all is left alone.
    Packer other_p;
    other_p.array(4);
    other_p.uint(0);  // not the response marker
    other_p.uint(7);
    other_p.nil();
    other_p.bin(payload);
    Bytes other_bytes = other_p.out;
    Reader other_reader(other_bytes.data(), other_bytes.size());
    Value other_envelope = other_reader.parse();
    check("a non-response array is left alone", Result(other_envelope).kind == Value::Kind::Array);

    // A timeout is not an envelope, so it must survive the unwrap: the
    // replay path reports it as a timeout, not as a payload.
    Value timeout;
    timeout.kind = Value::Kind::Timeout;
    check("a timeout survives the unwrap", Result(timeout).kind == Value::Kind::Timeout);

    std::printf("[replay-selftest] %s (%d failure(s))\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Row #127: the reply-structure dump.
//
// A loopback stand-in for the daemon's reply side: it answers each
// msgpack-RPC request with the next canned result, echoing the request's
// msgid. It lives in this process, so nothing outlives the test.
// ---------------------------------------------------------------------------
class MockReplyServer {
public:
    explicit MockReplyServer(std::vector<Bytes> results) : results_(std::move(results)) {}
    ~MockReplyServer() { Stop(); }

    bool Start() {
        if (results_.empty()) return false;
        lfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (lfd_ < 0) return false;
        int one = 1;
        setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;  // the kernel picks a free port
        if (bind(lfd_, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(lfd_, 4) != 0) {
            close(lfd_);
            lfd_ = -1;
            return false;
        }
        socklen_t alen = sizeof(addr);
        getsockname(lfd_, (sockaddr*)&addr, &alen);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] { Serve(); });
        return true;
    }

    uint16_t port() const { return port_; }

    void Stop() {
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
        if (lfd_ >= 0) {
            close(lfd_);
            lfd_ = -1;
        }
    }

private:
    // The reply for one request: the msgpack-RPC envelope around the next
    // canned result, carrying the request's own msgid.
    Bytes ReplyFor(uint64_t msgid) {
        Packer p;
        p.array(4);
        p.uint(1);
        p.uint(msgid);
        p.nil();
        const Bytes& r = results_[served_ % results_.size()];
        ++served_;
        p.raw(r.data(), r.size());
        return p.out;
    }

    static bool WriteAll(int fd, const Bytes& b) {
        size_t sent = 0;
        while (sent < b.size()) {
            ssize_t n = send(fd, b.data() + sent, b.size() - sent, 0);
            if (n <= 0) return false;
            sent += (size_t)n;
        }
        return true;
    }

    void ServeConnection(int cfd) {
        Bytes buf;
        uint8_t tmp[4096];
        while (!stop_.load()) {
            // Answer every complete request in the buffer.
            for (;;) {
                Reader r(buf.data(), buf.size());
                Value req = r.parse();
                if (!r.ok()) break;  // incomplete: more bytes are needed
                uint64_t msgid = 0;
                if (req.kind == Value::Kind::Array && req.array.size() >= 2 &&
                    req.array[1].kind == Value::Kind::UInt) {
                    msgid = req.array[1].u;
                }
                if (!WriteAll(cfd, ReplyFor(msgid))) return;
                buf.erase(buf.begin(), buf.begin() + static_cast<long>(r.consumed()));
            }
            struct pollfd p = {cfd, POLLIN, 0};
            int pr = poll(&p, 1, 50);
            if (pr < 0) {
                if (errno == EINTR) continue;
                return;
            }
            if (pr == 0) continue;
            ssize_t n = recv(cfd, tmp, sizeof(tmp), 0);
            if (n <= 0) return;  // the client hung up
            buf.insert(buf.end(), tmp, tmp + n);
        }
    }

    void Serve() {
        while (!stop_.load()) {
            struct pollfd p = {lfd_, POLLIN, 0};
            int pr = poll(&p, 1, 50);
            if (pr <= 0) continue;
            int cfd = accept(lfd_, nullptr, nullptr);
            if (cfd < 0) continue;
            ServeConnection(cfd);
            close(cfd);
        }
    }

    std::vector<Bytes> results_;
    int lfd_ = -1;
    uint16_t port_ = 0;
    size_t served_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

// A msgpack-RPC request frame for a frame pull, with the msgid the test
// wants the reply to carry.
Bytes GetFrameRequest(uint64_t msgid) {
    Packer p;
    p.array(4);
    p.uint(0);
    p.uint(msgid);
    p.str("ob_rpc_get_frame");
    p.array(0);
    return p.out;
}

// Row #127: the reply-structure dump.
//
// Three reply shapes are pinned, byte for byte: the recorded "no frame"
// shape (an 11-element array whose bin-shaped slots are all empty), the
// same array with a payload in one of those slots, and a top-level bin
// result. Each shape's dump line is asserted, and the same three lines
// are then produced end to end against the loopback mock.
int ReplyDumpSelftest() {
    int failures = 0;
    auto check = [&](const char* name, bool pass) {
        std::printf("[dump-selftest] %-52s %s\n", name, pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    };
    FrameTally tally;

    // The expected element lists, written out by hand so the assertion
    // does not lean on the formatter it checks.
    const char* kElemsEmpty =
        "array(11) [0]=UInt(0) [1]=UInt(0) [2]=UInt(0) [3]=UInt(255) "
        "[4]=Bin(0) head=[] [5]=UInt(0) [6]=Bin(0) head=[] [7]=UInt(0) "
        "[8]=Bin(0) head=[] [9]=UInt(2731083288) [10]=Int(-1)";
    const char* kElemsWithPayload =
        "array(11) [0]=UInt(0) [1]=UInt(0) [2]=UInt(0) [3]=UInt(255) "
        "[4]=Bin(3) head=[0a 0b 0c] [5]=UInt(0) [6]=Bin(0) head=[] [7]=UInt(0) "
        "[8]=Bin(0) head=[] [9]=UInt(2731083288) [10]=Int(-1)";
    auto expect_reply = [](const char* kind, uint64_t msgid, const char* result_text) {
        return std::string("[dump] reply method=ob_rpc_get_frame kind=") + kind +
               " msgid=" + std::to_string(msgid) + " envelope=Array result=" + result_text;
    };
    auto expect_raw = [](size_t len, const char* head) {
        return std::string("[dump] get_frame raw len=") + std::to_string(len) + " head=[" + head + "]";
    };

    // The recorded reply head (see docs/recon/findings/streaming-replay-payloads.md
    // §5): the whole message, envelope and result.
    const Bytes recorded = FromHex("940105c09b000000ccffc40000c40000c400cea2c90618ff");
    Reader rec_reader(recorded.data(), recorded.size());
    Value rec_envelope = rec_reader.parse();
    check("the recorded reply parses as an envelope", rec_reader.ok() && rec_envelope.kind == Value::Kind::Array);
    check("the recorded reply's result is an array", Result(rec_envelope).kind == Value::Kind::Array);
    check("the recorded reply's result has 11 elements", Result(rec_envelope).array.size() == 11);

    // The array with its bin-shaped slots empty — the recorded shape.
    Packer empty_p;
    empty_p.array(11);
    empty_p.uint(0); empty_p.uint(0); empty_p.uint(0); empty_p.uint(255);
    empty_p.bin(Bytes{});
    empty_p.uint(0); empty_p.bin(Bytes{});
    empty_p.uint(0); empty_p.bin(Bytes{});
    empty_p.uint(2731083288); empty_p.integer(-1);
    const Bytes empty_result = empty_p.out;

    // The recorded message is the envelope around that result.
    Packer env_p;
    env_p.array(4);
    env_p.uint(1);
    env_p.uint(5);
    env_p.nil();
    env_p.raw(empty_result.data(), empty_result.size());
    check("the recorded message is the envelope around the empty-bin array", env_p.out == recorded);

    const std::string expect_a = expect_reply("Array", 5, kElemsEmpty) + "\n" +
                                 expect_raw(24, "94 01 05 c0 9b 00 00 00 cc ff c4 00 00 c4 00 00 c4 00 "
                                               "ce a2 c9 06 18 ff");
    check("the recorded reply dumps as an 11-element array",
          FormatReplyDump("ob_rpc_get_frame", rec_envelope, recorded) == expect_a);

    // The same array with a payload in the first bin-shaped slot.
    Packer payload_p;
    payload_p.array(11);
    payload_p.uint(0); payload_p.uint(0); payload_p.uint(0); payload_p.uint(255);
    payload_p.bin(Bytes{0x0a, 0x0b, 0x0c});
    payload_p.uint(0); payload_p.bin(Bytes{});
    payload_p.uint(0); payload_p.bin(Bytes{});
    payload_p.uint(2731083288); payload_p.integer(-1);
    const Bytes payload_result = payload_p.out;

    // A top-level bin result.
    Packer bin_p;
    bin_p.bin(Bytes{0x0a, 0x0b, 0x0c});
    const Bytes bin_result = bin_p.out;

    // The same three shapes, end to end over a loopback socket: the mock
    // echoes the request's msgid, so the first reply reproduces the
    // recorded message byte for byte.
    MockReplyServer server({empty_result, payload_result, bin_result});
    if (!server.Start()) {
        std::printf("[dump-selftest] SKIP (the loopback mock could not start)\n");
        return 0;
    }
    RpcClient rpc;
    std::string sink;
    g_dump_sink = &sink;
    g_dump_replies = true;
    bool connected = rpc.connect("127.0.0.1", server.port());
    check("connects to the loopback mock", connected);
    if (connected) {
        for (uint64_t msgid = 5; msgid <= 7; ++msgid) {
            rpc.sendRaw(GetFrameRequest(msgid));
            Value envelope = rpc.readReply();
            DumpReply("ob_rpc_get_frame", envelope, rpc.lastRaw());
            // The parsed summary, as the runtime paths print it.
            Value result = Result(envelope);
            FrameArray fa = ParseFrameArray(result);
            DumpFrameSummary(fa, result);
            tally.Add(server.port(), fa);
        }
    }
    g_dump_sink = nullptr;
    g_dump_replies = false;
    server.Stop();

    std::string expect_mock;
    expect_mock += expect_reply("Array", 5, kElemsEmpty) + "\n" +
                   expect_raw(24, "94 01 05 c0 9b 00 00 00 cc ff c4 00 00 c4 00 00 c4 00 "
                                 "ce a2 c9 06 18 ff") + "\n" +
                   "[dump] GET_FRAME -> empty\n";
    expect_mock += expect_reply("Array", 6, kElemsWithPayload) + "\n" +
                   expect_raw(27, "94 01 06 c0 9b 00 00 00 cc ff c4 03 0a 0b 0c 00 c4 00 00 c4 00 "
                                 "ce a2 c9 06 18 ff") + "\n" +
                   "[dump] GET_FRAME -> band(3)\n";
    expect_mock += expect_reply("Bin", 7, "Bin(3) head=[0a 0b 0c]") + "\n" +
                   expect_raw(9, "94 01 07 c0 c4 03 0a 0b 0c") + "\n" +
                   "[dump] GET_FRAME -> Bin(3) head=[0a 0b 0c]\n";
    check("the mock run's dump is byte-exact", sink == expect_mock);
    if (sink != expect_mock) {
        std::printf("[dump-selftest] got:\n%s", sink.c_str());
        std::printf("[dump-selftest] want:\n%s", expect_mock.c_str());
    }

    // The tally: one empty reply, one with a band, one that is not the
    // array form at all.
    const std::string expect_tally = "[dump] tally: port " + std::to_string(server.port()) +
                                     " data=1 empty=1 other=1 (data share 50.0%)";
    check("the tally counts data, empty and other per port", tally.Lines() == expect_tally);
    if (tally.Lines() != expect_tally) {
        std::printf("[dump-selftest] got:  %s\n", tally.Lines().c_str());
        std::printf("[dump-selftest] want: %s\n", expect_tally.c_str());
    }

    std::printf("[dump-selftest] the mock run's dump:\n%s", sink.c_str());
    std::printf("[dump-selftest] %s (%d failure(s))\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Row #157: the live path's method names.
//
// A loopback stand-in for the daemon's ONE interface. It answers the
// methods that interface has and refuses every other name the way the
// daemon refuses it — "could not find function '<name>' with argument
// count <n>" — so a client that sends a bare name is caught here and not
// on the hardware.
// ---------------------------------------------------------------------------

// The frame the mock serves: a short, recognisable band.
const Bytes kMockBand = {0xde, 0xad, 0xbe, 0xef};

// The vendor's ack for a SET_PROPERTY: the RespHeader of protocol-spec
// §2.1 (magic 0x4252, errorCode 0).
Bytes MockVendorAck(uint16_t opcode, uint16_t request_id) {
    Bytes b;
    b.push_back(0x52); b.push_back(0x42);  // magic LE 0x4252
    b.push_back(0x53); b.push_back(0x00);  // size in half-words
    b.push_back(opcode & 0xff); b.push_back(opcode >> 8);
    b.push_back(request_id & 0xff); b.push_back(request_id >> 8);
    b.push_back(0x00); b.push_back(0x00);  // errorCode
    return b;
}

class MockInterfaceDaemon {
public:
    // One call the mock saw.
    struct Call {
        std::string method;
        Value params;
    };

    explicit MockInterfaceDaemon(const InterfaceRoute& route) : route_(route) {}
    ~MockInterfaceDaemon() { Stop(); }

    bool Start() {
        lfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (lfd_ < 0) return false;
        int one = 1;
        setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;  // the kernel picks a free port
        if (bind(lfd_, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(lfd_, 4) != 0) {
            close(lfd_);
            lfd_ = -1;
            return false;
        }
        socklen_t alen = sizeof(addr);
        getsockname(lfd_, (sockaddr*)&addr, &alen);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] { Serve(); });
        return true;
    }

    uint16_t port() const { return port_; }

    // Row #161: answer only the first n calls, then read and never reply —
    // the daemon that accepts the connection and stalls. The call is still
    // recorded, so the selftest can see what the client sent.
    void set_silent_after(size_t n) { silent_after_ = n; }

    void Stop() {
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
        if (lfd_ >= 0) {
            close(lfd_);
            lfd_ = -1;
        }
    }

    // The calls the mock saw, in order.
    std::vector<Call> calls() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return calls_;
    }
    // The method names, in order.
    std::vector<std::string> Methods() const {
        std::vector<std::string> out;
        for (const Call& c : calls()) out.push_back(c.method);
        return out;
    }
    // How many times a method was called.
    size_t CountOf(const std::string& method) const {
        size_t n = 0;
        for (const Call& c : calls()) {
            if (c.method == method) ++n;
        }
        return n;
    }
    // The parameters of the first call to a method.
    bool ParamsOf(const std::string& method, Value* params) const {
        for (const Call& c : calls()) {
            if (c.method == method) {
                *params = c.params;
                return true;
            }
        }
        return false;
    }

private:
    // The reply for one request. The mock knows the methods of ONE
    // interface; every other name is refused the way the daemon refuses
    // it, with the method name and the argument count in the message.
    Bytes ReplyFor(const std::string& method, const Value& params, uint64_t msgid) {
        Packer p;
        p.array(4);
        p.uint(1);
        p.uint(msgid);
        const std::string start = "ob_rpc_start_stream" + route_.suffix;
        const std::string stop = "ob_rpc_stop_stream" + route_.suffix;
        const std::string get_frame = "ob_rpc_get_frame" + route_.suffix;
        const std::string send_data = "ob_rpc_send_data" + route_.suffix;
        if (method == start || method == stop) {
            p.nil();
            p.boolean(true);  // the daemon's start/stop result
        } else if (method == get_frame) {
            p.nil();
            Bytes result = FrameArrayResult(kMockBand, Bytes{}, Bytes{}, -1);
            p.raw(result.data(), result.size());
        } else if (method == send_data) {
            p.nil();
            Bytes ack = MockVendorAck(kOpSetProperty, 1);
            p.bin(ack);
        } else {
            // The daemon's own words for a method it does not have.
            size_t argc = (params.kind == Value::Kind::Array) ? params.array.size() : 0;
            p.str("could not find function '" + method + "' with argument count " + std::to_string(argc));
            p.nil();
        }
        return p.out;
    }

    static bool WriteAll(int fd, const Bytes& b) {
        size_t sent = 0;
        while (sent < b.size()) {
            ssize_t n = send(fd, b.data() + sent, b.size() - sent, 0);
            if (n <= 0) return false;
            sent += (size_t)n;
        }
        return true;
    }

    void ServeConnection(int cfd) {
        Bytes buf;
        uint8_t tmp[4096];
        while (!stop_.load()) {
            for (;;) {
                Reader r(buf.data(), buf.size());
                Value req = r.parse();
                if (!r.ok()) break;  // incomplete: more bytes are needed
                uint64_t msgid = 0;
                std::string method;
                Value params;
                if (req.kind == Value::Kind::Array && req.array.size() >= 4) {
                    if (req.array[1].kind == Value::Kind::UInt) msgid = req.array[1].u;
                    if (req.array[2].kind == Value::Kind::Str) method = req.array[2].s;
                    params = req.array[3];
                }
                size_t seq = 0;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    calls_.push_back(Call{method, params});
                    seq = calls_.size();
                }
                if (seq <= silent_after_ && !WriteAll(cfd, ReplyFor(method, params, msgid))) return;
                buf.erase(buf.begin(), buf.begin() + static_cast<long>(r.consumed()));
            }
            struct pollfd p = {cfd, POLLIN, 0};
            int pr = poll(&p, 1, 50);
            if (pr < 0) {
                if (errno == EINTR) continue;
                return;
            }
            if (pr == 0) continue;
            ssize_t n = recv(cfd, tmp, sizeof(tmp), 0);
            if (n <= 0) return;  // the client hung up
            buf.insert(buf.end(), tmp, tmp + n);
        }
    }

    void Serve() {
        while (!stop_.load()) {
            struct pollfd p = {lfd_, POLLIN, 0};
            int pr = poll(&p, 1, 50);
            if (pr <= 0) continue;
            int cfd = accept(lfd_, nullptr, nullptr);
            if (cfd < 0) continue;
            ServeConnection(cfd);
            close(cfd);
        }
    }

    InterfaceRoute route_;
    std::vector<Call> calls_;
    mutable std::mutex mutex_;
    int lfd_ = -1;
    uint16_t port_ = 0;
    size_t silent_after_ = static_cast<size_t>(-1);
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

// Row #157: the live path's method names, its call sequence and the
// reply unwrap — no device.
//
// The daemon's methods carry the interface suffix (protocol-spec.md
// §1.1), so a name without one is a name the daemon does not have. The
// mock serves ONE interface and refuses every other name, exactly as the
// daemon refuses one; the client must therefore address the IR port as
// "…0-2-12" and the depth port as "…0-2-10".
int LiveSuffixSelftest() {
    int failures = 0;
    auto check = [&](const char* name, bool pass) {
        std::printf("[live-selftest] %-56s %s\n", name, pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    };

    // The port picks the interface, and the interface carries the suffix.
    RouteTable routes;
    const InterfaceRoute* ir = RouteForPort(routes, 10013);
    const InterfaceRoute* depth = RouteForPort(routes, 10010);
    check("the IR port resolves to the IR interface", ir && ir->suffix == "0-2-12" && ir->name == "IR");
    check("the depth port resolves to the depth interface", depth && depth->suffix == "0-2-10" && depth->name == "depth");
    check("the IR interface's stream type is 6", ir && ir->stream_type == 6);
    check("the depth interface's stream type is 7", depth && depth->stream_type == 7);

    // A port that is not an interface port is an error naming the map.
    check("a port that is not an interface port resolves to nothing", RouteForPort(routes, 10011) == nullptr);
    const std::string unknown = UnknownPortMessage(routes, 10011);
    check("the unknown-port error names the port and the map",
          unknown.find("10011") != std::string::npos && unknown.find("0-2-12") != std::string::npos &&
              unknown.find("0-2-10") != std::string::npos);

    // The method names are the base name plus the interface's suffix.
    if (ir) {
        check("the IR method names carry the IR suffix",
              MethodFor(*ir, "ob_rpc_start_stream") == "ob_rpc_start_stream0-2-12" &&
                  MethodFor(*ir, "ob_rpc_get_frame") == "ob_rpc_get_frame0-2-12" &&
                  MethodFor(*ir, "ob_rpc_send_data") == "ob_rpc_send_data0-2-12" &&
                  MethodFor(*ir, "ob_rpc_stop_stream") == "ob_rpc_stop_stream0-2-12");
    }
    if (depth) {
        check("the depth method names carry the depth suffix",
              MethodFor(*depth, "ob_rpc_start_stream") == "ob_rpc_start_stream0-2-10" &&
                  MethodFor(*depth, "ob_rpc_get_frame") == "ob_rpc_get_frame0-2-10" &&
                  MethodFor(*depth, "ob_rpc_send_data") == "ob_rpc_send_data0-2-10" &&
                  MethodFor(*depth, "ob_rpc_stop_stream") == "ob_rpc_stop_stream0-2-10");
    }

    // One live run per interface, against the mock that serves it.
    struct Run {
        const char* label;
        const InterfaceRoute* route;
        const char* suffix;
        uint32_t stream_type;
    };
    const Run runs[] = {
        {"IR", ir, "0-2-12", 6},
        {"depth", depth, "0-2-10", 7},
    };
    for (const Run& run : runs) {
        if (!run.route) continue;
        MockInterfaceDaemon daemon(*run.route);
        if (!daemon.Start()) {
            std::printf("[live-selftest] SKIP (the loopback mock could not start)\n");
            return 0;
        }
        // The mock's port takes the interface's place in the table, so
        // the live path resolves it exactly as it resolves a real port.
        RouteTable live_routes;
        live_routes.SetPortByName(run.route->name, daemon.port());
        const InterfaceRoute* live_route = RouteForPort(live_routes, daemon.port());
        check(std::string("the mock's port resolves to the interface's route").c_str(),
              live_route && live_route->suffix == run.suffix);

        // The frame the live path saves.
        char path[] = "/tmp/raptor_live_XXXXXX";
        int fd = mkstemp(path);
        if (fd < 0) {
            std::printf("[live-selftest] SKIP (mkstemp failed)\n");
            return 0;
        }
        close(fd);

        LiveStats stats;
        int rc = Live("127.0.0.1", daemon.port(), *live_route, 5, path, 0, 3, false, nullptr, &stats,
                      kDefaultLiveTimeoutMs);
        daemon.Stop();

        const std::string tag = std::string(run.label) + ": ";
        check((tag + "the live run succeeds").c_str(), rc == 0);
        check((tag + "the run pulled the three frames it asked for").c_str(),
              stats.get_calls == 3 && stats.data == 3 && stats.empty == 0 && stats.errors == 0);
        check((tag + "the stream was started and stopped").c_str(), stats.started && stats.stopped);

        const std::vector<std::string> methods = daemon.Methods();
        const std::string start = "ob_rpc_start_stream" + std::string(run.suffix);
        const std::string stop = "ob_rpc_stop_stream" + std::string(run.suffix);
        const std::string get_frame = "ob_rpc_get_frame" + std::string(run.suffix);
        const std::string send_data = "ob_rpc_send_data" + std::string(run.suffix);
        check((tag + "the first call is the suffixed start_stream").c_str(),
              !methods.empty() && methods.front() == start);
        check((tag + "the last call is the suffixed stop_stream").c_str(),
              methods.size() >= 2 && methods.back() == stop);
        check((tag + "every call carries the interface suffix").c_str(), !methods.empty());
        bool all_suffixed = true;
        const std::string suffix(run.suffix);
        for (const std::string& m : methods) {
            if (m.size() < suffix.size() || m.compare(m.size() - suffix.size(), suffix.size(), suffix) != 0) {
                all_suffixed = false;
            }
        }
        check((tag + "no call uses a bare method name").c_str(), all_suffixed);
        check((tag + "the frame pulls are the suffixed get_frame").c_str(),
              daemon.CountOf(get_frame) == 3);
        check((tag + "the property sets are the suffixed send_data").c_str(),
              daemon.CountOf(send_data) == (size_t)(3 * kLivePropertySets));

        // The sequence: start, then each frame pull with its property
        // sets, then the stop.
        size_t i = 1;
        bool sequence = true;
        for (int n = 0; n < 3 && sequence; ++n) {
            if (i >= methods.size() || methods[i] != get_frame) { sequence = false; break; }
            ++i;
            for (int k = 0; k < kLivePropertySets; ++k) {
                if (i >= methods.size() || methods[i] != send_data) { sequence = false; break; }
                ++i;
            }
        }
        check((tag + "the sequence is start, frame pull with its property sets, stop").c_str(),
              sequence && i < methods.size() && methods[i] == stop && i + 1 == methods.size());

        // The parameters: the interface's own stream type, and the same
        // array for the stop.
        Value start_params;
        Value stop_params;
        bool have_start = daemon.ParamsOf(start, &start_params);
        bool have_stop = daemon.ParamsOf(stop, &stop_params);
        auto is_stream_params = [&](const Value& v, uint32_t stream_type) {
            if (v.kind != Value::Kind::Array || v.array.size() != 5) return false;
            for (size_t k = 0; k < v.array.size(); ++k) {
                if (v.array[k].kind != Value::Kind::UInt) return false;
            }
            return v.array[0].u == stream_type && v.array[1].u == 9 && v.array[2].u == 1920 &&
                   v.array[3].u == 1200 && v.array[4].u == 50;
        };
        check((tag + "the start carries [streamtype,9,1920,1200,50]").c_str(),
              have_start && is_stream_params(start_params, run.stream_type));
        check((tag + "the stop carries the same array as the start").c_str(),
              have_stop && is_stream_params(stop_params, run.stream_type));

        // The reply unwrap: the frame the mock served is the frame the
        // live path saved.
        std::ifstream in(path, std::ios::binary);
        Bytes saved((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        unlink(path);
        check((tag + "the saved frame is the band the mock served").c_str(), saved == kMockBand);
    }

    // The mock refuses a bare method name the way the daemon does — the
    // reply the 2026-10-20 hardware runs recorded.
    if (ir) {
        MockInterfaceDaemon daemon(*ir);
        if (daemon.Start()) {
            RpcClient rpc;
            if (rpc.connect("127.0.0.1", daemon.port())) {
                Packer p;
                p.array(5);
                p.uint(6); p.uint(9); p.uint(1920); p.uint(1200); p.uint(50);
                Value envelope = rpc.call("ob_rpc_start_stream", p.out);
                Value result = Result(envelope);
                check("a bare method name is refused with the daemon's message",
                      result.kind == Value::Kind::Error &&
                          result.error == "could not find function 'ob_rpc_start_stream' with argument count 5");
            }
            daemon.Stop();
        }
    }

    std::printf("[live-selftest] %s (%d failure(s))\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Row #161: the live path's bounded wait.
//
// The depth interface accepts the connection and then stops answering; the
// client must stop at its bound, name the endpoint, the interface and the
// unanswered method, and exit kExitLiveTimeout — never block on the socket.
// The peers here are loopback; no device and no thread outlives the test.
// ---------------------------------------------------------------------------

std::string ReadWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Redirects one descriptor to a temp file for the duration of a scope, and
// reads it back. Written() does NOT flush the C streams, so it reports only
// bytes the client had already flushed.
class FdRedirect {
public:
    ~FdRedirect() { Restore(); }
    bool Begin(int fd, const char* path_template) {
        // Anything already printed belongs to the real descriptor, not to
        // the capture: flush it before the redirect moves the fd.
        std::fflush(nullptr);
        fd_ = fd;
        saved_ = dup(fd);
        if (saved_ < 0) { fd_ = -1; return false; }
        std::string tmpl(path_template);
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        int tfd = mkstemp(buf.data());
        if (tfd < 0) { Restore(); return false; }
        path_ = buf.data();
        if (dup2(tfd, fd_) < 0) {
            close(tfd);
            unlink(path_.c_str());
            Restore();
            return false;
        }
        close(tfd);
        return true;
    }
    std::string Written() const { return path_.empty() ? std::string() : ReadWholeFile(path_); }
    std::string Finish() {
        if (fd_ < 0) return std::string();
        std::fflush(nullptr);
        std::string text = Written();
        Restore();
        unlink(path_.c_str());
        return text;
    }

private:
    void Restore() {
        if (fd_ >= 0 && saved_ >= 0) dup2(saved_, fd_);
        if (saved_ >= 0) { close(saved_); saved_ = -1; }
        fd_ = -1;
    }
    int fd_ = -1;
    int saved_ = -1;
    std::string path_;
};

int LiveTimeoutSelftest() {
    int failures = 0;
    auto check = [&](const std::string& name, bool pass) {
        std::printf("[live-timeout-selftest] %-62s %s\n", name.c_str(), pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    };
    auto elapsed_ms = [](std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
        return (long)std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };

    RouteTable routes;
    const InterfaceRoute* depth = RouteForPort(routes, 10010);
    if (!depth) {
        std::printf("[live-timeout-selftest] SKIP (the depth route is missing)\n");
        return 0;
    }
    const int bound = 300;

    // The wait primitive: an empty pipe expires on its bound, a readable one
    // returns at once.
    {
        int fds[2];
        bool ok = pipe(fds) == 0;
        check("a pipe for the wait test", ok);
        if (ok) {
            auto t0 = std::chrono::steady_clock::now();
            int pr = WaitEvent(fds[0], POLLIN, 200);
            auto t1 = std::chrono::steady_clock::now();
            long ms = elapsed_ms(t0, t1);
            check("a wait with nothing to read expires on its bound", pr == 0 && ms >= 150 && ms < 3000);
            ssize_t ignored = write(fds[1], "x", 1);
            (void)ignored;
            check("a wait on readable data returns at once", WaitEvent(fds[0], POLLIN, 200) == 1);
            close(fds[0]);
            close(fds[1]);
        }
    }

    // The line the run prints when its bound expires.
    check("the timeout line names the endpoint, interface, method and bound",
          LiveTimeoutMessage("127.0.0.1", 10010, *depth, "ob_rpc_start_stream0-2-10", 5000) ==
              "[live] TIMEOUT: 127.0.0.1:10010 (depth, suffix 0-2-10) ob_rpc_start_stream0-2-10: "
              "no complete reply within 5000 ms - the connection was established and the daemon did not answer");
    check("the connect timeout line names the endpoint and the bound",
          LiveConnectTimeoutMessage("127.0.0.1", 10010, 5000) ==
              "[live] TIMEOUT: 127.0.0.1:10010: no TCP connection within 5000 ms");

    // The two stalls: silent from the first byte (start_stream), and silent
    // after the start is accepted (get_frame). Both are the depth hang.
    struct Stall {
        const char* label;
        size_t silent_after;
        bool expect_started;
        const char* unanswered;
    };
    const Stall stalls[] = {
        {"a daemon that never answers is rejected at start_stream", 0, false, "ob_rpc_start_stream0-2-10"},
        {"a daemon that stops after start_stream is rejected at get_frame", 1, true, "ob_rpc_get_frame0-2-10"},
    };
    for (const Stall& stall : stalls) {
        const std::string label(stall.label);
        MockInterfaceDaemon daemon(*depth);
        daemon.set_silent_after(stall.silent_after);
        if (!daemon.Start()) {
            std::printf("[live-timeout-selftest] SKIP (the loopback mock could not start)\n");
            return 0;
        }
        RouteTable live_routes;
        live_routes.SetPortByName("depth", daemon.port());
        const InterfaceRoute* route = RouteForPort(live_routes, daemon.port());

        char out_path[] = "/tmp/raptor_lt_XXXXXX";
        int ofd = mkstemp(out_path);
        if (ofd < 0) {
            daemon.Stop();
            std::printf("[live-timeout-selftest] SKIP (mkstemp failed)\n");
            return 0;
        }
        close(ofd);

        FdRedirect out_cap;
        FdRedirect err_cap;
        bool captured = out_cap.Begin(STDOUT_FILENO, "/tmp/raptor_lt_out_XXXXXX") &&
                        err_cap.Begin(STDERR_FILENO, "/tmp/raptor_lt_err_XXXXXX");
        LiveStats st;
        auto t0 = std::chrono::steady_clock::now();
        int rc = Live("127.0.0.1", daemon.port(), *route, 5, out_path, 0, 0, false, nullptr, &st, bound);
        auto t1 = std::chrono::steady_clock::now();
        // Read the log BEFORE flushing anything: a "connected" line that is
        // still in the buffer would be absent, and the stall would be silent
        // again.
        const std::string early = out_cap.Written();
        const std::string out_text = out_cap.Finish();
        const std::string err_text = err_cap.Finish();
        long ms = elapsed_ms(t0, t1);
        daemon.Stop();
        unlink(out_path);

        check(label + ": the run exits with the timeout code", captured && rc == kExitLiveTimeout);
        check(label + ": the run stops at its bound (not a hang)", ms >= (bound * 3) / 4 && ms < 5000);
        check(label + ": the rejection names the unanswered method",
              err_text.find(stall.unanswered) != std::string::npos);
        check(label + ": the rejection names the interface and the bound",
              err_text.find("depth") != std::string::npos &&
                  err_text.find(std::to_string(bound) + " ms") != std::string::npos);
        check(label + ": the connected line reached the log before the stall",
              early.find("[live] connected") != std::string::npos);
        check(label + ": start_stream is recorded as accepted or not", st.started == stall.expect_started);
        check(label + ": a timed-out connection is not asked to stop_stream",
              daemon.CountOf("ob_rpc_stop_stream0-2-10") == 0);
        check(label + ": the run reports the timeout", st.timed_out);
        (void)out_text;
    }

    // A refused connect is not a timeout: it is a named failure, promptly.
    {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        uint16_t closed_port = 0;
        if (s >= 0 && bind(s, (sockaddr*)&a, sizeof(a)) == 0) {
            socklen_t alen = sizeof(a);
            getsockname(s, (sockaddr*)&a, &alen);
            closed_port = ntohs(a.sin_port);
        }
        if (s >= 0) close(s);
        if (closed_port) {
            FdRedirect err_cap;
            bool captured = err_cap.Begin(STDERR_FILENO, "/tmp/raptor_lt_err_XXXXXX");
            LiveStats st;
            auto t0 = std::chrono::steady_clock::now();
            int rc = Live("127.0.0.1", closed_port, *depth, 5, "/tmp/raptor_lt_refused.raw", 0, 0, false, nullptr,
                          &st, bound);
            auto t1 = std::chrono::steady_clock::now();
            const std::string err_text = err_cap.Finish();
            long ms = elapsed_ms(t0, t1);
            check("a refused connect is a failure (exit 1), not a timeout", captured && rc == kExitLiveFailed);
            check("the refused-connect message names the endpoint",
                  err_text.find("127.0.0.1:" + std::to_string(closed_port)) != std::string::npos);
            check("a refused connect fails promptly", ms < 2000);
        }
    }

    std::printf("[live-timeout-selftest] %s (%d failure(s))\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Row #140: the capture round-trip.
//
// The recorder writes the replies; the reader reads them back; the bytes
// must be identical. The manifest's hashes are checked against the files
// on disk, and a directory inside a repository must be refused.
// ---------------------------------------------------------------------------
int CaptureSelftest() {
    int failures = 0;
    auto check = [&](const char* name, bool pass) {
        std::printf("[capture-selftest] %-52s %s\n", name, pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    };

    // The manifest's hashes must be right, not merely self-consistent: the
    // standard SHA-256 vectors.
    {
        capture::Sha256 h;
        h.Update(std::string("abc"));
        check("sha256(\"abc\") matches the standard vector",
              h.HexDigest() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        capture::Sha256 empty;
        check("sha256(\"\") matches the standard vector",
              empty.HexDigest() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        // A long input, so the multi-block path is exercised.
        capture::Sha256 million;
        million.Update(std::string(1000000, 'a'));
        check("sha256 of a million 'a' matches the standard vector",
              million.HexDigest() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }

    // A temporary directory outside the repository.
    char dir_template[] = "/tmp/raptor_capture_XXXXXX";
    char* dir = mkdtemp(dir_template);
    if (!dir) {
        std::printf("[capture-selftest] SKIP (mkdtemp failed)\n");
        return 0;
    }
    const std::string bundle = dir;

    // The replies: the recorded no-data array, the same array with a band,
    // and a bare top-level bin — on two ports.
    const Bytes no_data = Envelope(5, FrameArrayResult(Bytes{}, Bytes{}, Bytes{}, -1));
    const Bytes with_band = Envelope(6, FrameArrayResult(Bytes{0x0a, 0x0b, 0x0c}, Bytes{}, Bytes{}, 2));
    Packer bin_p;
    bin_p.bin(Bytes{0x0a, 0x0b, 0x0c});
    const Bytes bare_bin = Envelope(7, bin_p.out);

    capture::Recorder recorder;
    std::string err;
    bool opened = recorder.Open(bundle, g_binary_path, 0, 0, &err);
    check("the capture opens outside the repository", opened);
    recorder.Record(10013, "ob_rpc_get_frame0-2-12", 5, no_data);
    recorder.Record(10013, "ob_rpc_get_frame0-2-12", 6, with_band);
    recorder.Record(10010, "ob_rpc_get_frame0-2-10", 7, bare_bin);
    const std::string manifest = recorder.Close("selftest-branch", "0123456789abcdef0123456789abcdef01234567");
    check("the manifest is written", !manifest.empty());

    // The manifest names the run and hashes every file.
    std::string text;
    {
        std::ifstream in(manifest, std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            text = ss.str();
        }
    }
    check("the manifest names the branch", text.find("\"branch\": \"selftest-branch\"") != std::string::npos);
    check("the manifest names the commit",
          text.find("\"commit\": \"0123456789abcdef0123456789abcdef01234567\"") != std::string::npos);
    check("the manifest carries the binary's sha256",
          text.find(capture::FileSha256(g_binary_path)) != std::string::npos);
    check("the manifest carries the run's start and end",
          text.find("\"run_start_unix_ms\"") != std::string::npos &&
              text.find("\"run_end_unix_ms\"") != std::string::npos);
    check("the manifest counts the records", text.find("\"record_count\": 3") != std::string::npos);
    for (const char* rel : {"replies/port-10010.bin", "replies/port-10013.bin"}) {
        const std::string file = bundle + "/" + rel;
        const std::string sha = capture::FileSha256(file);
        check((std::string("the manifest hashes ") + rel).c_str(), !sha.empty() && text.find(sha) != std::string::npos);
    }

    // Read the bundle back: the bytes must be identical.
    capture::Reader reader;
    bool opened_reader = reader.Open(bundle, &err);
    check("the reader opens the bundle", opened_reader);
    std::map<uint64_t, capture::Record> read;
    size_t read_count = 0;
    capture::Record r;
    while (reader.Next(&r)) {
        read[r.msgid] = r;
        ++read_count;
    }
    check("the reader read every record once", read_count == 3);
    check("the reader read every record", read.size() == 3);
    check("the reader found both port files", reader.files().size() == 2);
    check("the reader's file order is the name order",
          reader.files().size() == 2 && reader.files()[0] == "port-10010.bin" &&
              reader.files()[1] == "port-10013.bin");
    check("the no-data reply reads back byte for byte",
          read.count(5) == 1 && read[5].bytes == no_data);
    check("the band reply reads back byte for byte",
          read.count(6) == 1 && read[6].bytes == with_band);
    check("the bare bin reply reads back byte for byte",
          read.count(7) == 1 && read[7].bytes == bare_bin);
    check("the record keeps its port",
          read.count(5) == 1 && read[5].port == 10013 && read.count(7) == 1 && read[7].port == 10010);
    check("the record keeps its method",
          read.count(5) == 1 && read[5].method == "ob_rpc_get_frame0-2-12" &&
              read.count(7) == 1 && read[7].method == "ob_rpc_get_frame0-2-10");
    check("the reader stops at the end of the bundle", !reader.Next(&r));
    check("the reader reports no error", reader.error().empty());

    // A directory inside a repository is refused: the bundle is never
    // committed.
    const std::string fake_repo = bundle + "/fake-repo";
    mkdir(fake_repo.c_str(), 0755);
    mkdir((fake_repo + "/.git").c_str(), 0755);
    capture::Recorder inside;
    std::string inside_err;
    check("a capture inside a repository is refused",
          !inside.Open(fake_repo + "/cap", g_binary_path, 0, 0, &inside_err) &&
              inside_err.find("git repository") != std::string::npos);

    // A symlink is a way to aim a bundle at the repository, so the path is
    // resolved before it is judged.
    const std::string link = bundle + "/link-into-the-repo";
    if (symlink(fake_repo.c_str(), link.c_str()) == 0) {
        capture::Recorder via_link;
        std::string link_err;
        check("a capture through a symlink into a repository is refused",
              !via_link.Open(link + "/cap", g_binary_path, 0, 0, &link_err) &&
                  link_err.find("git repository") != std::string::npos);
    } else {
        std::printf("[capture-selftest] SKIP (the symlink could not be made)\n");
    }

    // The byte bound stops the recording.
    capture::Recorder bounded;
    if (bounded.Open(bundle + "/bounded", g_binary_path, 0, 64, &err)) {
        for (int i = 0; i < 20; ++i) bounded.Record(10013, "ob_rpc_get_frame0-2-12", (uint64_t)i, no_data);
        check("the byte bound stops the recording", bounded.Capped() && bounded.BytesCapHit());
        bounded.Close("selftest-branch", "0123456789abcdef0123456789abcdef01234567");
    } else {
        check("the byte bound stops the recording", false);
    }

    capture::RemoveTree(bundle);
    std::printf("[capture-selftest] %s (%d failure(s))\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}

// The message a stalled frame produces. It names the frame AND the
// interface the frame went to: the two connections are separate, so
// "which request went unanswered" is only half the answer.
std::string ReplayTimeoutMessage(size_t frame_index, const std::string& suffix, int timeout_ms) {    char buf[160];
    std::snprintf(buf, sizeof(buf), "[replay] frame %zu (%s): no reply within %d ms", frame_index,
                  suffix.c_str(), timeout_ms);
    return buf;
}

// The cadence the replay would use, as one summary line: how many frames,
// the span they cover, and the mean gap between them. This is the
// no-hardware check that the pacing is the captured one (row #117), and
// the per-interface split is what a run routes on (row #120).
void PrintCadence(const std::vector<int64_t>& stamps, const std::vector<int64_t>& deltas, int backward,
                  const RouteTable& routes, const std::vector<int>& frame_route) {
    // The capture file groups frames by call type, not by time, so the span
    // is max(stamp) - min(stamp), not last - first.
    int64_t lo = -1, hi = -1;
    for (int64_t s : stamps) {
        if (s < 0) continue;
        if (lo < 0 || s < lo) lo = s;
        if (hi < 0 || s > hi) hi = s;
    }
    int64_t span = (lo >= 0 && hi >= lo) ? (hi - lo) : 0;
    // The mean gap is over the inter-frame deltas (the first frame's wait is
    // from the capture's t=0 and is capped, so it is not part of the pace).
    int64_t sum = 0;
    size_t n = 0;
    for (size_t i = 1; i < deltas.size(); ++i) { sum += deltas[i]; ++n; }
    double mean = n ? (double)sum / (double)n : 0.0;
    std::printf("[replay] cadence: %zu frame(s), capture span %lld ms, mean gap %.2f ms, "
                "first-frame wait %lld ms%s\n",
                deltas.size(), (long long)span, mean,
                (long long)(deltas.empty() ? 0 : FirstFrameWaitMs(deltas[0])),
                backward ? " (backward stamps clamped to 0)" : "");

    // Per interface: how many frames it carries, and the span of its own
    // timestamps. The two are separate connections on the daemon.
    std::string split;
    for (size_t r = 0; r < routes.routes().size(); ++r) {
        size_t count = 0;
        int64_t rlo = -1, rhi = -1;
        for (size_t i = 0; i < frame_route.size(); ++i) {
            if (frame_route[i] != (int)r) continue;
            ++count;
            if (stamps[i] < 0) continue;
            if (rlo < 0 || stamps[i] < rlo) rlo = stamps[i];
            if (rhi < 0 || stamps[i] > rhi) rhi = stamps[i];
        }
        if (count == 0) continue;
        int64_t rspan = (rlo >= 0 && rhi >= rlo) ? (rhi - rlo) : 0;
        if (!split.empty()) split += " / ";
        split += routes.routes()[r].name + " " + std::to_string(count) + " (span " +
                 std::to_string(rspan) + " ms)";
    }
    std::printf("[replay] interfaces: %s\n", split.c_str());
}

int Replay(const std::string& host, const RouteTable& routes, const std::string& path, bool dry_run,
           bool log, int reply_timeout_ms, capture::Recorder* capture) {
    std::vector<Bytes> frames;
    std::vector<int64_t> stamps;
    std::string err;
    if (!ParseReplayFile(path, &frames, &stamps, &err)) {
        std::fprintf(stderr, "[replay] %s\n", err.c_str());
        return 2;
    }
    std::printf("[replay] %zu frame(s) from %s%s\n", frames.size(), path.c_str(),
                dry_run ? " (dry-run: not sent)" : "");
    if (frames.empty()) return 2;
    int backward = 0;
    std::vector<int64_t> deltas = ToInterFrameDeltas(stamps, &backward);

    // Route every frame before sending anything: an unknown interface
    // suffix is an error naming the frame, not a silent default to some
    // port. Doing this up front means a bad file fails before the first
    // byte goes out.
    std::vector<int> frame_route;
    std::vector<std::string> frame_suffix;
    std::vector<std::string> frame_method;
    frame_route.reserve(frames.size());
    frame_suffix.reserve(frames.size());
    frame_method.reserve(frames.size());
    for (size_t i = 0; i < frames.size(); ++i) {
        const InterfaceRoute* route = nullptr;
        std::string suffix;
        if (!RouteForFrame(routes, frames[i], i + 1, &route, &suffix, &err)) {
            std::fprintf(stderr, "[replay] %s\n", err.c_str());
            return 2;
        }
        frame_route.push_back(routes.IndexOfSuffix(route->suffix));
        frame_suffix.push_back(suffix);
        // The method name, for the reply dump.
        std::string method;
        frame_method.push_back(MethodNameOfFrame(frames[i], &method) ? method : suffix);
    }

    PrintCadence(stamps, deltas, backward, routes, frame_route);
    if (dry_run) {
        for (size_t i = 0; i < frames.size(); ++i) {
            int64_t wait = (i == 0) ? FirstFrameWaitMs(deltas[i]) : deltas[i];
            char ts[32];
            if (stamps[i] < 0) std::snprintf(ts, sizeof(ts), "none");
            else std::snprintf(ts, sizeof(ts), "%lldms", (long long)stamps[i]);
            const InterfaceRoute& r = routes.routes()[(size_t)frame_route[i]];
            std::printf("[replay] frame %zu: %s (%s) -> port %u, t=%s wait %lldms %zu B: %s\n", i + 1,
                        r.suffix.c_str(), r.name.c_str(), (unsigned)r.port, ts, (long long)wait,
                        frames[i].size(), HexHead(frames[i]).c_str());
        }
        return 0;
    }

    // One connection per interface actually present in the file. Each
    // connection is used in strict request/response order, so at most one
    // reply is ever outstanding on it.
    std::map<std::string, std::unique_ptr<RpcClient>> conns;
    for (size_t r = 0; r < routes.routes().size(); ++r) {
        bool present = false;
        for (int idx : frame_route) {
            if (idx == (int)r) { present = true; break; }
        }
        if (!present) continue;
        const InterfaceRoute& route = routes.routes()[r];
        auto rpc = std::unique_ptr<RpcClient>(new RpcClient());
        if (!rpc->connect(host, route.port)) {
            std::fprintf(stderr, "[replay] cannot connect to %s:%u for interface %s (%s) "
                         "(is RPCServer running?)\n",
                         host.c_str(), (unsigned)route.port, route.name.c_str(), route.suffix.c_str());
            return 1;
        }
        std::printf("[replay] connected %s:%u for interface %s (%s)\n", host.c_str(),
                     (unsigned)route.port, route.name.c_str(), route.suffix.c_str());
        rpc->set_capture(capture);
        conns[route.suffix] = std::move(rpc);
    }
    std::printf("[replay] sending unchanged request frames (reply timeout %d ms)\n", reply_timeout_ms);
    uint64_t bins = 0, empties = 0, errors = 0;
    FrameTally tally;
    for (size_t i = 0; i < frames.size(); ++i) {
        int64_t wait = (i == 0) ? FirstFrameWaitMs(deltas[i]) : deltas[i];
        if (wait > 0) std::this_thread::sleep_for(std::chrono::milliseconds(wait));
        RpcClient& rpc = *conns[frame_suffix[i]];
        if (!rpc.sendRaw(frames[i])) {
            std::fprintf(stderr, "[replay] frame %zu (%s): send failed\n", i + 1,
                         frame_suffix[i].c_str());
            ++errors;
            break;
        }
        // The reply is the msgpack-RPC envelope [1, msgid, nil, result]; the
        // payload is at index 3. Unwrap it before classifying, or every
        // frame reads as the envelope array ("empty") — the bug row #122
        // names. A Timeout is not an array, so it passes through unchanged.
        Value envelope = rpc.readReplyWithDeadline(reply_timeout_ms, frame_method[i]);
        DumpReply(frame_method[i], envelope, rpc.lastRaw());
        Value reply = Result(envelope);
        if (IsGetFrameMethod(frame_method[i])) {
            // The result is the 11-element array: the band's length, or the
            // no-data form.
            FrameArray fa = ParseFrameArray(reply);
            DumpFrameSummary(fa, reply);
            tally.Add(rpc.port(), fa);
        }
        if (reply.kind == Value::Kind::Bin) {
            ++bins;
            if (log) std::printf("[replay] frame %zu (%s) -> bin(%zu) head=[%s]\n", i + 1,
                                 frame_suffix[i].c_str(), reply.bin.size(), HexHead(reply.bin).c_str());
        } else if (reply.kind == Value::Kind::Timeout) {
            // Name the frame AND its interface: the point of the bound is
            // that a stall says WHICH request went unanswered and on which
            // of the daemon's interfaces.
            std::fprintf(stderr, "%s\n", ReplayTimeoutMessage(i + 1, frame_suffix[i], reply_timeout_ms).c_str());
            std::fflush(stderr);
            return 1;
        } else if (reply.kind == Value::Kind::Error) {
            ++errors;
            if (log) std::printf("[replay] frame %zu (%s) -> ERROR: %s\n", i + 1,
                                 frame_suffix[i].c_str(), reply.error.c_str());
        } else {
            ++empties;
            if (log) std::printf("[replay] frame %zu (%s) -> kind=%d\n", i + 1, frame_suffix[i].c_str(),
                                 (int)reply.kind);
        }
        std::fflush(stdout);
    }
    std::printf("[replay] done: bin=%llu empty=%llu error=%llu\n", (unsigned long long)bins,
                (unsigned long long)empties, (unsigned long long)errors);
    DumpTally(tally);
    return bins > 0 ? 0 : 1;
}

// Row #117: the bounded reply read, exercised with no device. A loopback
// listener accepts the connection and never answers, so the read must hit
// its deadline and report the frame it was waiting for — the case that
// produced row #105's silent hang.
//
// The listener runs in a thread and is shut down (and joined) before this
// returns: no stray process or thread outlives the test.
int ReplyTimeoutSelftest() {
    int failures = 0;
    auto check = [&](const char* name, bool pass) {
        std::printf("[replay-selftest] %-52s %s\n", name, pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    };

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) { std::printf("[replay-selftest] SKIP (socket failed)\n"); return 0; }
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // let the kernel pick a free port
    if (bind(lfd, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(lfd, 4) != 0) {
        close(lfd);
        std::printf("[replay-selftest] SKIP (bind/listen failed)\n");
        return 0;
    }
    socklen_t alen = sizeof(addr);
    getsockname(lfd, (sockaddr*)&addr, &alen);
    uint16_t port = ntohs(addr.sin_port);

    // The silent peer: accept, then never write a reply.
    std::atomic<bool> stop{false};
    std::thread peer([&] {
        int cfd = accept(lfd, nullptr, nullptr);
        if (cfd < 0) return;
        while (!stop.load()) {
            struct pollfd p = {cfd, POLLIN, 0};
            if (poll(&p, 1, 50) > 0) {
                uint8_t sink[4096];
                ssize_t n = recv(cfd, sink, sizeof(sink), 0);
                if (n <= 0) break;  // the client hung up
            }
        }
        close(cfd);
    });

    // A one-frame replay file, sent to the silent peer.
    const char* body = "+0 940001b16f625f7270635f6765745f6672616d6590\n";
    char path[] = "/tmp/raptor_timeout_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        stop.store(true);
        peer.join();
        close(lfd);
        std::printf("[replay-selftest] SKIP (mkstemp failed)\n");
        return 0;
    }
    ssize_t ignored = write(fd, body, std::strlen(body));
    (void)ignored;
    close(fd);

    RpcClient rpc;
    bool connected = rpc.connect("127.0.0.1", port);
    check("connects to the silent loopback peer", connected);

    bool got_timeout = false;
    int64_t elapsed_ms = 0;
    if (connected) {
        Bytes frame = {0x94, 0x00, 0x01, 0xb1, 'o', 'b', '_', 'r', 'p', 'c', '_', 'g',
                       'e', 't', '_', 'f', 'r', 'a', 'm', 'e', 0x90};
        rpc.sendRaw(frame);
        auto t0 = std::chrono::steady_clock::now();
        Value reply = rpc.readReplyWithDeadline(300);
        auto t1 = std::chrono::steady_clock::now();
        elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        got_timeout = (reply.kind == Value::Kind::Timeout);
        check("the bounded read reports a timeout", got_timeout);
        check("the timeout fires near the deadline (not instantly, not late)",
              elapsed_ms >= 250 && elapsed_ms < 3000);
    }

    unlink(path);
    stop.store(true);
    peer.join();
    close(lfd);

    // Row #120: a stall names the frame AND the interface it went to.
    check("the timeout message names the frame and its interface",
          ReplayTimeoutMessage(7, "0-2-10", 2000) ==
              "[replay] frame 7 (0-2-10): no reply within 2000 ms");
    check("the timeout message names the IR interface too",
          ReplayTimeoutMessage(1, "0-2-12", 1500) ==
              "[replay] frame 1 (0-2-12): no reply within 1500 ms");

    std::printf("[replay-selftest] %s (%d failure(s))\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    // The manifest names the binary that made a capture, so the path is
    // resolved once, here.
    {
        char resolved[4096];
        if (realpath(argv[0], resolved)) {
            g_binary_path = resolved;
        } else {
            g_binary_path = argv[0];
        }
    }
    if (argc >= 2 && std::string(argv[1]) == "--dry-run") {
        int rc = DryRun();
        if (rc) return rc;
        rc |= ReplayParserSelftest();
        rc |= ReplayDeltaSelftest();
        rc |= ReplayInterfaceSelftest();
        rc |= ReplyResultSelftest();
        return rc;
    }
    if (argc >= 2 && std::string(argv[1]) == "--selftest-timeout") return ReplyTimeoutSelftest();
    if (argc >= 2 && std::string(argv[1]) == "--selftest-dump") return ReplyDumpSelftest();
    if (argc >= 2 && std::string(argv[1]) == "--selftest-capture") return CaptureSelftest();
    if (argc >= 2 && std::string(argv[1]) == "--selftest-live") return LiveSuffixSelftest();
    if (argc >= 2 && std::string(argv[1]) == "--selftest-live-timeout") return LiveTimeoutSelftest();
    if (argc >= 2 && std::string(argv[1]) == "--dry-run-live") return DryRunLive();
    if (argc >= 2 && std::string(argv[1]) == "--discover") { GvcpDiscover(1000); return 0; }
    if (argc >= 2 && std::string(argv[1]) == "--capture-dump") {
        if (argc < 3) { std::fprintf(stderr, "usage: raptor_client --capture-dump <dir>\n"); return 2; }
        return CaptureDump(argv[2]);
    }
    if (argc >= 2 && std::string(argv[1]) == "--replay") {
        if (argc < 3) { std::fprintf(stderr, "usage: raptor_client --replay <file> [host] [--dry-run] [--quiet] [--reply-timeout-ms N] [--ir-port N] [--depth-port N] [--dump-replies] [--capture DIR]\n"); return 2; }
        std::string path = argv[2];
        std::string host = "127.0.0.1";
        bool dry_run = false, log = true;
        int reply_timeout_ms = kDefaultReplyTimeoutMs;
        std::string capture_dir;
        int capture_seconds = kDefaultCaptureSeconds;
        uint64_t capture_bytes = kDefaultCaptureBytes;
        RouteTable routes;
        // Positionals are host then port; anything starting with "--" is a
        // flag, so a flag may appear before them.
        std::vector<std::string> positionals;
        for (int i = 3; i < argc; ++i) {
            std::string a = argv[i];
            if (a.rfind("--", 0) == 0) {
                if (a == "--dry-run") dry_run = true;
                else if (a == "--quiet") log = false;
                else if (a == "--dump-replies") g_dump_replies = true;
                else if (a == "--capture" && i + 1 < argc) capture_dir = argv[++i];
                else if (a == "--capture-seconds" && i + 1 < argc) capture_seconds = std::atoi(argv[++i]);
                else if (a == "--capture-bytes" && i + 1 < argc) capture_bytes = std::strtoull(argv[++i], nullptr, 10);
                else if (a == "--reply-timeout-ms" && i + 1 < argc) reply_timeout_ms = std::atoi(argv[++i]);
                else if ((a == "--ir-port" || a == "--depth-port") && i + 1 < argc) {
                    int port = std::atoi(argv[++i]);
                    std::string name = (a == "--ir-port") ? "IR" : "depth";
                    if (port <= 0 || port > 65535) {
                        std::fprintf(stderr, "%s must be a valid port (got %s)\n", a.c_str(), argv[i]);
                        return 2;
                    }
                    if (!routes.SetPortByName(name, (uint16_t)port)) {
                        std::fprintf(stderr, "unknown interface: %s\n", name.c_str());
                        return 2;
                    }
                }
                else { std::fprintf(stderr, "unknown option: %s\n", a.c_str()); return 2; }
            } else {
                positionals.push_back(a);
            }
        }
        if (positionals.size() > 0) host = positionals[0];
        if (positionals.size() > 1) {
            std::fprintf(stderr, "the replay now routes per interface; a single port argument is "
                         "ambiguous — use --ir-port and --depth-port\n");
            return 2;
        }
        if (reply_timeout_ms <= 0) {
            std::fprintf(stderr, "--reply-timeout-ms must be positive (got %d)\n", reply_timeout_ms);
            return 2;
        }
        if (capture_seconds < 0) {
            std::fprintf(stderr, "--capture-seconds must not be negative (got %d)\n", capture_seconds);
            return 2;
        }
        CaptureSession capture;
        if (!capture_dir.empty() && !capture.Start(capture_dir, capture_seconds, capture_bytes)) return 2;
        int rc = Replay(host, routes, path, dry_run, log, reply_timeout_ms, capture.recorder());
        capture.Stop();
        return rc;
    }
    if (argc >= 2 && std::string(argv[1]) == "--live") {
        std::string host = "127.0.0.1";
        uint16_t port = 10013;
        int seconds = 5;
        std::string out = "/tmp/raptor_frame.raw";
        int pull_ms = 0;
        int max_frames = 0;
        int timeout_ms = kDefaultLiveTimeoutMs;
        bool log = true;
        std::string capture_dir;
        int capture_seconds = kDefaultCaptureSeconds;
        uint64_t capture_bytes = kDefaultCaptureBytes;
        RouteTable routes;
        // Positionals are host, port, seconds and the output path; a
        // flag may appear before them.
        std::vector<std::string> positionals;
        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            if (a.rfind("--", 0) == 0) {
                if (a == "--pull-ms" && i + 1 < argc) pull_ms = std::atoi(argv[++i]);
                else if (a == "--quiet") log = false;
                else if (a == "--reply-timeout-ms" && i + 1 < argc) timeout_ms = std::atoi(argv[++i]);
                else if (a == "--dump-replies") g_dump_replies = true;
                else if (a == "--frames" && i + 1 < argc) max_frames = std::atoi(argv[++i]);
                else if (a == "--capture" && i + 1 < argc) capture_dir = argv[++i];
                else if (a == "--capture-seconds" && i + 1 < argc) capture_seconds = std::atoi(argv[++i]);
                else if (a == "--capture-bytes" && i + 1 < argc) capture_bytes = std::strtoull(argv[++i], nullptr, 10);
                else if ((a == "--ir-port" || a == "--depth-port") && i + 1 < argc) {
                    int p = std::atoi(argv[++i]);
                    std::string name = (a == "--ir-port") ? "IR" : "depth";
                    if (p <= 0 || p > 65535) {
                        std::fprintf(stderr, "%s must be a valid port (got %s)\n", a.c_str(), argv[i]);
                        return 2;
                    }
                    if (!routes.SetPortByName(name, (uint16_t)p)) {
                        std::fprintf(stderr, "unknown interface: %s\n", name.c_str());
                        return 2;
                    }
                } else { std::fprintf(stderr, "unknown option: %s\n", a.c_str()); return 2; }
            } else {
                positionals.push_back(a);
            }
        }
        if (positionals.size() > 0) host = positionals[0];
        if (positionals.size() > 1) port = (uint16_t)std::atoi(positionals[1].c_str());
        if (positionals.size() > 2) seconds = std::atoi(positionals[2].c_str());
        if (positionals.size() > 3) out = positionals[3];
        if (positionals.size() > 4) {
            std::fprintf(stderr, "too many arguments: usage: raptor_client --live <host> [port] [seconds] [out.raw]\n");
            return 2;
        }
        if (seconds <= 0) {
            std::fprintf(stderr, "the live window must be positive (got %d)\n", seconds);
            return 2;
        }
        if (max_frames < 0) {
            std::fprintf(stderr, "--frames must not be negative (got %d)\n", max_frames);
            return 2;
        }
        if (timeout_ms <= 0) {
            std::fprintf(stderr, "--reply-timeout-ms must be positive (got %d)\n", timeout_ms);
            return 2;
        }
        if (capture_seconds < 0) {
            std::fprintf(stderr, "--capture-seconds must not be negative (got %d)\n", capture_seconds);
            return 2;
        }
        // The port picks the interface: the daemon's method names carry
        // the interface's suffix (protocol-spec.md §1.1), so a port that
        // is not an interface port is a mistake, not a default.
        const InterfaceRoute* route = RouteForPort(routes, port);
        if (!route) {
            std::fprintf(stderr, "[live] %s\n", UnknownPortMessage(routes, port).c_str());
            return 2;
        }
        CaptureSession capture;
        if (!capture_dir.empty() && !capture.Start(capture_dir, capture_seconds, capture_bytes)) return 2;
        int rc = Live(host, port, *route, seconds, out, pull_ms, max_frames, log, capture.recorder(), nullptr,
                      timeout_ms);
        capture.Stop();
        return rc;
    }
    std::printf(
        "raptor_client — Phase B (C1) client for the Creality RPCServer daemon\n"
        "Usage:\n"
        "  raptor_client --dry-run                  framing selftest, no device\n"
        "  raptor_client --dry-run-live             print the bytes each call would send\n"
        "  raptor_client --selftest-timeout         bounded reply read, no device\n"
        "  raptor_client --selftest-dump            reply-structure dump, no device\n"
        "  raptor_client --selftest-capture         capture round-trip, no device\n"
        "  raptor_client --selftest-live            live-path method names, no device\n"
        "  raptor_client --selftest-live-timeout    live-path bounded wait, no device\n"
        "  raptor_client --discover                 GVCP discovery only\n"
        "  raptor_client --capture-dump <dir>       read a capture bundle back\n"
        "  raptor_client --replay <file> [host] [--dry-run] [--quiet]\n"
        "      [--reply-timeout-ms N] [--ir-port N] [--depth-port N] [--dump-replies]\n"
        "      [--capture DIR] [--capture-seconds N] [--capture-bytes N]\n"
        "      sends recon's captured request frames unchanged, in order; file\n"
        "      format: one hex frame per line, optional '+<ms> ' prefix,\n"
        "      '#' comments/blank lines ignored.\n"
        "      The '+<ms>' prefix is an ABSOLUTE capture timestamp (milliseconds\n"
        "      since the capture started), not a per-frame delay, so the replay\n"
        "      paces on the inter-arrival deltas between consecutive stamps.\n"
        "      Each frame is routed by the interface suffix on its method name\n"
        "      to that interface's own port, one connection per interface:\n"
        "        0-2-12  IR     port 10013  (--ir-port)\n"
        "        0-2-10  depth  port 10010  (--depth-port)\n"
        "      --reply-timeout-ms bounds each reply read (default 2000); on\n"
        "      expiry the replay names the frame and its interface and exits\n"
        "      non-zero.\n"
        "  raptor_client --live <host> [port] [seconds] [out.raw] [--pull-ms N] [--quiet]\n"
        "      [--frames N] [--reply-timeout-ms N] [--dump-replies] [--capture DIR]\n"
        "      [--capture-seconds N] [--capture-bytes N] [--ir-port N] [--depth-port N]\n"
        "      ONE interface per run: the port picks the interface, and every\n"
        "      method name on that connection carries the interface's suffix,\n"
        "      because the daemon has no method without one:\n"
        "        port 10013  IR     ob_rpc_start_stream0-2-12  (and get_frame,\n"
        "                                            send_data, stop_stream)\n"
        "        port 10010  depth  ob_rpc_start_stream0-2-10  (and get_frame,\n"
        "                                            send_data, stop_stream)\n"
        "      A port that is not an interface port is an error naming the map.\n"
        "      The stream's first parameter is the interface's stream type\n"
        "      (6 IR, 7 depth), not a start/stop command; the stop is its own\n"
        "      method (ob_rpc_stop_stream<suffix>) with the same parameters.\n"
        "      Both interfaces are two runs, one per port — the daemon serves\n"
        "      one interface per port, so a run cannot pull the other's frames.\n"
        "      --frames N stops the run after N frame pulls (the window ends\n"
        "      first if it is shorter); --pull-ms 0 = as fast as the daemon\n"
        "      answers.\n"
        "      --reply-timeout-ms N (default 5000) bounds the TCP connect and\n"
        "      every reply read. On expiry the run prints 'TIMEOUT: ...' naming\n"
        "      the endpoint, the interface and the unanswered method, then\n"
        "      exits 3: a daemon that accepts and stops answering is a\n"
        "      rejection, not a hang. Exit 1 = connected but served no frame;\n"
        "      exit 2 = usage error.\n"
        "      per-call logging is ON by default (--quiet disables it).\n"
        "\n"
        "  --dump-replies (OFF by default; the output above never changes)\n"
        "      prints one line per reply: the RPC kind, the msgid, and the result's\n"
        "      shape — for an array result its length and each element's kind, with\n"
        "      the byte length and a hex head (32 B) for any bin or string. For a\n"
        "      get_frame reply the raw reply bytes follow (hex head, 48 B), so the\n"
        "      envelope's own shape sits next to the unwrapped result. The lines\n"
        "      carry no clock and no address: the same run prints the same bytes.\n"
        "      A get_frame reply also gets its parsed summary — 'GET_FRAME ->\n"
        "      band(N)' for the frame's data, 'GET_FRAME -> empty' for the\n"
        "      no-data form — and the run ends with the per-port data/empty share.\n"
        "\n"
        "  --capture DIR (OFF by default)\n"
        "      records every reply, byte for byte, into DIR — a directory OUTSIDE\n"
        "      the repository (the tool refuses one inside it; the bundle is never\n"
        "      committed):\n"
        "        DIR/replies/port-<port>.bin   one file per port, length-prefixed\n"
        "                                    records: ts, port, method, msgid, byte\n"
        "                                    length, bytes (raw, unparsed)\n"
        "        DIR/manifest.json             the run's start/end, branch, commit,\n"
        "                                    the binary's sha256, and every file's\n"
        "                                    sha256 and size\n"
        "      --capture-seconds N (default 30) and --capture-bytes N (default 64 MiB)\n"
        "      bound the capture; 0 means no bound of that kind. The live run stops\n"
        "      when a bound is reached.\n");
    return 0;
}
