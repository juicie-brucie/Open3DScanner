#pragma once
// The localhost msgpack-RPC server (row #172).
//
// It serves the `ob_rpc_*` method set of protocol-spec.md §1 on the
// interface ports (§1.1's port map), with the method names carrying the
// interface suffix (#157's routing). Frames come from a FrameSource — the
// offline #132 bundle now, the live USB source when #169/#171 land.
//
// One poll loop serves every connection, so nothing here is shared across
// threads: the source is only ever touched from the loop.

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "openscanner/daemon_switch.h"
#include "frame_source.h"
#include "properties.h"

namespace shim {

struct ShimConfig {
    std::string host = "127.0.0.1";
    // The shim's own ports (daemon_switch.h): the vendor daemon keeps its
    // pair, so the two can listen at once and the switch picks one.
    uint16_t ir_port = openscanner::kShimIrPort;
    uint16_t depth_port = openscanner::kShimDepthPort;
    bool serve_ir = true;
    bool serve_depth = true;
    // One source per interface: the two #132 bundles are separate captures.
    FrameSource* ir_source = nullptr;
    FrameSource* depth_source = nullptr;
    bool verbose = false;
    int io_timeout_ms = 5000;
    size_t max_request_bytes = 1u << 20;
    std::function<void(const std::string&)> log;
};

struct ShimStats {
    uint64_t connections = 0;
    uint64_t requests = 0;
    uint64_t start_stream = 0;
    uint64_t stop_stream = 0;
    uint64_t get_frame = 0;
    uint64_t frames = 0;
    uint64_t empty = 0;
    uint64_t send_data = 0;
    uint64_t enumeration = 0;
    uint64_t unknown = 0;
    uint64_t bytes_out = 0;
    std::map<std::string, uint64_t> by_method;
};

class ShimServer {
public:
    explicit ShimServer(ShimConfig config);
    ~ShimServer();

    ShimServer(const ShimServer&) = delete;
    ShimServer& operator=(const ShimServer&) = delete;

    bool Start(std::string* err);
    void Stop();
    void Run();
    bool StartThread();
    void Join();

    ShimStats stats() const;

private:
    struct Listener {
        int fd = -1;
        Interface iface;
    };
    struct Connection {
        int fd = -1;
        Interface iface;
        Bytes in;
        bool started = false;
    };

    void AcceptReady(size_t listener_index);
    void ReadReady(size_t connection_index);
    void CloseConnection(size_t index);
    bool SendAll(int fd, const Bytes& bytes);
    Bytes Dispatch(const Interface& iface, const Value& request, bool* started);
    FrameSource* SourceFor(const Interface& iface) const;
    void Log(const std::string& line);

    ShimConfig config_;
    PropertyBank bank_;
    std::vector<Listener> listeners_;
    std::vector<Connection> connections_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    bool thread_started_ = false;
    mutable std::mutex stats_mutex_;
    ShimStats stats_;
    uint64_t get_frame_logged_ = 0;
    int64_t last_get_frame_log_ms_ = 0;
};

}  // namespace shim
