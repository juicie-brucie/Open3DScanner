#include "shim_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <utility>

namespace shim {
namespace {

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool EndsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The daemon's refusal for a method it does not have: the string the client's
// own mock reproduces from the hardware record (row #157).
std::string UnknownMethodMessage(const std::string& method, size_t argc) {
    return "could not find function '" + method + "' with argument count " + std::to_string(argc);
}

Bytes ReplyOk(uint64_t msgid, const Bytes& result) {
    Packer p;
    p.array(4);
    p.uint(1);
    p.uint(msgid);
    p.nil();
    p.raw(result);
    return p.out;
}

// rpclib's error form: [1, msgid, "message", nil]. The message sits in the
// error slot; the result is nil.
Bytes ReplyError(uint64_t msgid, const std::string& message) {
    Packer p;
    p.array(4);
    p.uint(1);
    p.uint(msgid);
    p.str(message);
    p.nil();
    return p.out;
}

Bytes BoolResult(bool value) {
    Packer p;
    p.boolean(value);
    return p.out;
}

Bytes UIntResult(uint64_t value) {
    Packer p;
    p.uint(value);
    return p.out;
}

// An array of four bin8(4) values, little-endian (protocol-spec §3.3 gives
// the values, not their byte order; the client does not read this method).
Bytes PuRangeResult() {
    const uint32_t values[4] = {1000, 15000, 100, 1000};
    Packer p;
    p.array(4);
    for (uint32_t v : values) {
        const uint8_t bytes[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
        p.bin(bytes, sizeof(bytes));
    }
    return p.out;
}

// protocol-spec §1.2: four entries, each the device identity and its interface
// port. The ports come from the config, so a test on ephemeral ports still
// reads a consistent map.
Bytes EnumerationResult(const ShimConfig& config) {
    struct Entry {
        uint32_t index;
        uint16_t port;
    };
    const Entry entries[4] = {{0, config.depth_port}, {6, 10011}, {4, 10012}, {2, config.ir_port}};
    Packer p;
    p.array(4);
    for (const Entry& entry : entries) {
        p.array(8);
        p.str("0-2-1");        // device id
        p.uint(0x2bc5);        // vendor id
        p.uint(0x0c03);        // product id
        p.str("CR0886100SN");  // serial
        p.str("Creality Raptor Pro");
        p.uint(entry.index);   // interface index
        p.str(config.host);
        p.uint(entry.port);
    }
    return p.out;
}

}  // namespace

ShimServer::ShimServer(ShimConfig config) : config_(std::move(config)) {}

ShimServer::~ShimServer() {
    Stop();
    Join();
}

void ShimServer::Log(const std::string& line) {
    if (config_.log) config_.log(line);
}

bool ShimServer::Start(std::string* err) {
    signal(SIGPIPE, SIG_IGN);
    struct Spec {
        bool serve;
        const Interface* iface;
    };
    const Spec specs[2] = {{config_.serve_ir, &IrInterface()}, {config_.serve_depth, &DepthInterface()}};
    for (const Spec& spec : specs) {
        if (!spec.serve) continue;
        Interface iface = *spec.iface;
        if (iface.name == "IR") iface.port = config_.ir_port;
        if (iface.name == "depth") iface.port = config_.depth_port;

        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            if (err) *err = std::string("socket: ") + std::strerror(errno);
            return false;
        }
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(iface.port);
        if (inet_pton(AF_INET, config_.host.c_str(), &addr.sin_addr) != 1) {
            if (err) *err = "not an IPv4 address: " + config_.host;
            close(fd);
            return false;
        }
        if (bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(fd, 8) != 0) {
            // The usual cause is the vendor daemon owning the port. Say which
            // port and leave it alone.
            if (err) {
                *err = "bind " + config_.host + ":" + std::to_string(iface.port) + ": " + std::strerror(errno);
            }
            close(fd);
            return false;
        }
        const int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        listeners_.push_back(Listener{fd, iface});
        Log("[shim] listening " + config_.host + ":" + std::to_string(iface.port) + " (" + iface.name +
            ", suffix " + iface.suffix + ")");
    }
    if (listeners_.empty()) {
        if (err) *err = "no interface enabled";
        return false;
    }
    stop_.store(false);
    return true;
}

void ShimServer::Stop() { stop_.store(true); }

bool ShimServer::StartThread() {
    if (thread_started_) return false;
    thread_ = std::thread([this] { Run(); });
    thread_started_ = true;
    return true;
}

void ShimServer::Join() {
    if (thread_started_ && thread_.joinable()) thread_.join();
    thread_started_ = false;
}

ShimStats ShimServer::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void ShimServer::Run() {
    while (!stop_.load()) {
        std::vector<pollfd> fds;
        fds.reserve(listeners_.size() + connections_.size());
        for (const Listener& listener : listeners_) fds.push_back(pollfd{listener.fd, POLLIN, 0});
        for (const Connection& connection : connections_) fds.push_back(pollfd{connection.fd, POLLIN, 0});

        const int ready = poll(fds.data(), (nfds_t)fds.size(), 200);
        if (ready < 0) {
            if (errno == EINTR) continue;
            Log(std::string("[shim] poll: ") + std::strerror(errno));
            break;
        }
        if (ready == 0) continue;

        // Listeners first, then connections; indices are stable inside one pass.
        for (size_t i = 0; i < listeners_.size(); ++i) {
            if (fds[i].revents & POLLIN) AcceptReady(i);
        }
        for (size_t i = 0; i < connections_.size(); ++i) {
            const short revents = fds[listeners_.size() + i].revents;
            if (revents & (POLLIN | POLLHUP | POLLERR)) ReadReady(i);
        }
    }
    for (Connection& connection : connections_) {
        if (connection.fd >= 0) close(connection.fd);
    }
    connections_.clear();
    for (Listener& listener : listeners_) {
        if (listener.fd >= 0) close(listener.fd);
    }
    listeners_.clear();
}

void ShimServer::AcceptReady(size_t listener_index) {
    for (;;) {
        const int fd = accept(listeners_[listener_index].fd, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
            Log(std::string("[shim] accept: ") + std::strerror(errno));
            return;
        }
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
        struct timeval tv = {};
        tv.tv_sec = config_.io_timeout_ms / 1000;
        tv.tv_usec = (config_.io_timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        const int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        Connection connection;
        connection.fd = fd;
        connection.iface = listeners_[listener_index].iface;
        connections_.push_back(std::move(connection));
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.connections;
        }
        Log("[shim] connection from a client on " + std::to_string(listeners_[listener_index].iface.port) + " (" +
            listeners_[listener_index].iface.name + ")");
    }
}

void ShimServer::ReadReady(size_t connection_index) {
    Connection& connection = connections_[connection_index];
    uint8_t buffer[65536];
    for (;;) {
        const ssize_t n = recv(connection.fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            connection.in.insert(connection.in.end(), buffer, buffer + n);
            if (connection.in.size() > config_.max_request_bytes) {
                Log("[shim] request over the " + std::to_string(config_.max_request_bytes) +
                    "-byte bound; closing the connection");
                CloseConnection(connection_index);
                return;
            }
            continue;
        }
        if (n == 0) {  // peer closed after its request (or mid-request)
            if (!connection.in.empty()) {
                Value request;
                const size_t used = ParseValue(connection.in.data(), connection.in.size(), &request);
                if (used) {
                    const Bytes reply = Dispatch(connection.iface, request, &connection.started);
                    if (!reply.empty()) SendAll(connection.fd, reply);
                }
            }
            CloseConnection(connection_index);
            return;
        }
        break;  // EAGAIN: the buffer holds whole messages to dispatch below
    }

    // Dispatch every complete message in the buffer.
    size_t consumed = 0;
    for (;;) {
        Value request;
        const size_t used = ParseValue(connection.in.data() + consumed, connection.in.size() - consumed, &request);
        if (!used) break;  // incomplete: wait for more bytes
        consumed += used;
        const Bytes reply = Dispatch(connection.iface, request, &connection.started);
        if (!reply.empty() && !SendAll(connection.fd, reply)) {
            CloseConnection(connection_index);
            return;
        }
    }
    if (consumed) connection.in.erase(connection.in.begin(), connection.in.begin() + (long)consumed);
}

void ShimServer::CloseConnection(size_t index) {
    if (index >= connections_.size()) return;
    if (connections_[index].fd >= 0) close(connections_[index].fd);
    connections_.erase(connections_.begin() + (long)index);
}

bool ShimServer::SendAll(int fd, const Bytes& bytes) {
    size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t n = send(fd, bytes.data() + sent, bytes.size() - sent, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR)) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // The send bound is this connection's SO_SNDTIMEO; a peer that
                // stops reading fails the write rather than holding the loop.
                struct pollfd p = {fd, POLLOUT, 0};
                if (poll(&p, 1, config_.io_timeout_ms) > 0) continue;
            }
            return false;
        }
        sent += (size_t)n;
    }
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.bytes_out += bytes.size();
    return true;
}

FrameSource* ShimServer::SourceFor(const Interface& iface) const {
    return iface.name == "IR" ? config_.ir_source : config_.depth_source;
}

Bytes ShimServer::Dispatch(const Interface& iface, const Value& request, bool* started) {
    if (!request.is_array(4) || request.array[0].kind != Value::Kind::kUInt || request.array[0].u != 0 ||
        request.array[2].kind != Value::Kind::kStr) {
        Log("[shim] refused a request that is not [0, msgid, method, params]");
        return ReplyError(0, "not a request: [0, msgid, method, params]");
    }
    const uint64_t msgid = request.array[1].kind == Value::Kind::kUInt ? request.array[1].u : 0;
    const std::string method = request.array[2].str;
    const Value& params = request.array[3];
    const size_t argc = params.kind == Value::Kind::kArray ? params.array.size() : 0;

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++stats_.requests;
        ++stats_.by_method[method];
    }

    // The enumerated methods carry no suffix and are accepted on any port.
    if (method == "ob_get_device_enumeration_info") {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++stats_.enumeration;
        Log("[shim] method=" + method + " msgid=" + std::to_string(msgid) + " -> 4 entries");
        return ReplyOk(msgid, EnumerationResult(config_));
    }

    // Every other method must carry THIS connection's interface suffix.
    const std::string base_stream = "ob_rpc_start_stream";
    const std::string base_stop = "ob_rpc_stop_stream";
    const std::string base_frame = "ob_rpc_get_frame";
    const std::string base_send = "ob_rpc_send_data";
    const std::string base_profiles = "ob_get_stream_profile_list";
    const std::string base_light = "ob_set_ir_frame_light_type";
    const std::string base_pu_get = "ob_rpc_get_pu";
    const std::string base_pu_range = "ob_rpc_get_pu_range";
    const std::string base_pu_set = "ob_rpc_set_pu";
    const std::string suffixed[] = {base_stream, base_stop,    base_frame,    base_send,    base_profiles,
                                    base_light,  base_pu_get,   base_pu_range, base_pu_set};
    bool known_base = false;
    for (const std::string& base : suffixed) {
        if (method == base + iface.suffix) {
            known_base = true;
            break;
        }
    }
    if (!known_base) {
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.unknown;
        }
        Log("[shim] method=" + method + " msgid=" + std::to_string(msgid) + " -> unknown (" +
            std::to_string(argc) + " args)");
        return ReplyError(msgid, UnknownMethodMessage(method, argc));
    }

    if (method == base_stream + iface.suffix) {
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.start_stream;
        }
        Log("[shim] method=" + method + " msgid=" + std::to_string(msgid) + " params=" + Describe(params) +
            " -> true");
        if (started) *started = true;
        return ReplyOk(msgid, BoolResult(true));
    }
    if (method == base_stop + iface.suffix) {
        if (FrameSource* source = SourceFor(iface)) source->Stop(iface);
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.stop_stream;
        }
        Log("[shim] method=" + method + " msgid=" + std::to_string(msgid) + " -> true");
        if (started) *started = false;
        return ReplyOk(msgid, BoolResult(true));
    }
    if (method == base_light + iface.suffix) {
        Log("[shim] method=" + method + " msgid=" + std::to_string(msgid) + " params=" + Describe(params) +
            " -> true");
        return ReplyOk(msgid, BoolResult(true));
    }
    if (method == base_profiles + iface.suffix) {
        // The profile the capture streamed (protocol-spec §1.2 / row #114).
        Packer p;
        p.array(1);
        p.array(5);
        p.uint(0);
        p.uint(9);
        p.uint(1920);
        p.uint(1200);
        p.uint(50);
        Log("[shim] method=" + method + " msgid=" + std::to_string(msgid) + " -> 1 profile");
        return ReplyOk(msgid, p.out);
    }
    if (method == base_pu_get + iface.suffix) {
        return ReplyOk(msgid, UIntResult(6000));
    }
    if (method == base_pu_range + iface.suffix) {
        return ReplyOk(msgid, PuRangeResult());
    }
    if (method == base_pu_set + iface.suffix) {
        return ReplyOk(msgid, BoolResult(true));
    }

    if (method == base_send + iface.suffix) {
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.send_data;
        }
        if (argc < 1 || params.array[0].kind != Value::Kind::kBin) {
            Log("[shim] method=" + method + " msgid=" + std::to_string(msgid) + " -> send_data without a vendor frame");
            return ReplyError(msgid, "send_data: first parameter is not a bin");
        }
        VendorRequest vendor;
        std::string why;
        if (!ParseVendorRequest(params.array[0].bin, &vendor, &why)) {
            Log("[shim] method=" + method + " -> " + why);
            return ReplyError(msgid, "send_data: " + why);
        }
        const VendorResponse response = HandleVendorRequest(vendor, &bank_);
        Log("[shim] method=" + method + " msgid=" + std::to_string(msgid) + " opcode=" +
            std::to_string(vendor.opcode) + " reqid=" + std::to_string(vendor.request_id) +
            " -> vendor error=" + std::to_string(response.error));
        Packer p;
        p.bin(BuildVendorResponse(response));
        return ReplyOk(msgid, p.out);
    }

    // get_frame
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++stats_.get_frame;
    }
    if (!started || !*started) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++stats_.empty;
        return ReplyOk(msgid, NoDataFrameArray());
    }
    FramePayload frame;
    std::string why;
    FrameSource* source = SourceFor(iface);
    if (!source) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++stats_.empty;
        return ReplyOk(msgid, NoDataFrameArray());
    }
    if (!source->Next(iface, &frame, &why)) {
        if (!why.empty()) {
            Log("[shim] get_frame source error: " + why);
            return ReplyError(msgid, "get_frame: " + why);
        }
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++stats_.empty;
        return ReplyOk(msgid, NoDataFrameArray());
    }
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++stats_.frames;
    }
    // Log the frame pulls without drowning the run: the first few, then at
    // most once a second.
    const int64_t now = NowMs();
    if (get_frame_logged_ < 3 || now - last_get_frame_log_ms_ >= 1000) {
        last_get_frame_log_ms_ = now;
        ++get_frame_logged_;
        Log("[shim] method=" + method + " msgid=" + std::to_string(msgid) + " -> band(" +
            std::to_string(frame.data.size()) + ") e1=" + std::to_string(frame.e1_us));
    }
    return ReplyOk(msgid, FrameArrayResult(frame));
}

}  // namespace shim
