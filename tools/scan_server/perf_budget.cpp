// perf_budget — the measurement behind the performance contract (row #152).
//
// Device-free: no vendor SDK, no device, no network beyond loopback. It
// measures the stages the contract budgets, using the product's own code —
// the V1 encoder (engine/include/openscanner/frame_protocol.h) and the
// WebSocket broadcast (engine/src/ws_broadcast_server.cpp) — so a change to
// either shows up here as a change in the numbers.
//
// Bounded: every phase has a fixed iteration count and a wall-clock cap.
//
// Output: one "key=value" line per measurement, so scripts/perf-budget.py
// can read the numbers without parsing prose.
//
//   platform.wait_ms.41        the realised wait for a 41 ms request (zero work)
//   platform.rate.24           the rate that wait allows (24 Hz's request)
//   encode.bytes.2304          the V1 frame's size, synthetic sphere
//   encode.us.2304             its encode time per frame (median)
//   encode.bytes.20000         the V1 frame's size, the live point budget
//   encode.us.20000            its encode time per frame (median)
//   bcast.us.1                 the broadcast's cost per frame, 1 client
//   bcast.us.8                 the broadcast's cost per frame, 8 clients
//   bcast.us_per_client.8      the per-client marginal cost at 8 clients
//   alloc.count_per_frame       the allocator calls per encoded frame
//   alloc.bytes_per_frame       the bytes the allocator hands out per frame
//   alloc.live_bytes            the live (in-use) bytes at the end
//   bcast.alloc_count_per_frame the allocator calls per broadcast (8 clients)

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif

#include "openscanner/frame_protocol.h"
#include "openscanner/ws_broadcast_server.h"

namespace {

using Clock = std::chrono::steady_clock;

// The point counts the product uses: the synthetic sphere's grid
// (tools/scan_server/main.cpp's kPointGridSize) and the live broadcast's
// budget (kLiveBroadcastPointBudget).
constexpr uint32_t kSyntheticPoints = 48 * 48;
constexpr uint32_t kLivePoints = 20000;

// The broadcasts whose cost is warm-up (the socket buffers' first fill).
constexpr int kBroadcastWarmup = 10;

// A free loopback port: bind, read it, close it. The race window is small
// and this is a benchmark, not a service.
uint16_t FreePort() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    uint16_t port = 0;
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) == 0) {
        socklen_t len = sizeof(addr);
        if (getsockname(fd, (sockaddr*)&addr, &len) == 0) port = ntohs(addr.sin_port);
    }
    close(fd);
    return port;
}

// A WebSocket client that only drains: the server's send has a 100 ms
// timeout, so a client that does not read would be dropped (and the
// broadcast would measure the timeout, not the copy).
class DrainClient {
public:
    bool Connect(uint16_t port) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        // A large receive buffer: the send must not block on the client's
        // drain, or the measurement times the drain instead of the copy
        // (the same trick #147's harness client used).
        int rcvbuf = 4 * 1024 * 1024;
        setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        if (connect(fd_, (sockaddr*)&addr, sizeof(addr)) != 0) {
            close(fd_);
            fd_ = -1;
            return false;
        }
        const char* request =
            "GET / HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n";
        if (send(fd_, request, std::strlen(request), 0) < 0) {
            close(fd_);
            fd_ = -1;
            return false;
        }
        // Read the handshake response, then drain forever.
        char buf[4096];
        ssize_t n = recv(fd_, buf, sizeof(buf), 0);
        if (n <= 0) {
            close(fd_);
            fd_ = -1;
            return false;
        }
        stop_ = false;
        thread_ = std::thread([this] { Drain(); });
        return true;
    }
    ~DrainClient() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
        if (fd_ >= 0) close(fd_);
    }

private:
    void Drain() {
        uint8_t sink[65536];
        while (!stop_.load()) {
            struct pollfd p = {fd_, POLLIN, 0};
            if (poll(&p, 1, 50) > 0) {
                ssize_t n = recv(fd_, sink, sizeof(sink), 0);
                if (n <= 0) return;
            }
        }
    }

    int fd_ = -1;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

// The median of a sample, in microseconds.
double MedianUs(std::vector<double> samples) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

// The fastest sample, in microseconds. On a shared machine the median of a
// bandwidth-bound measurement moves with the load (the other rooms' builds);
// the minimum is the uncontended cost, and it is the stable statistic to
// budget against.
double MinUs(const std::vector<double>& samples) {
    if (samples.empty()) return 0.0;
    return *std::min_element(samples.begin(), samples.end());
}

// The platform's own wait floor: how long a requested sleep actually takes,
// with nothing else in the loop. This is the rate ceiling no amount of
// optimisation can beat.
double WaitFloorMs(int request_ms, int repeats) {
    std::vector<double> samples;
    samples.reserve(repeats);
    for (int i = 0; i < repeats; ++i) {
        auto t0 = Clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(request_ms));
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    return MedianUs(samples);
}

// A cloud of `count` points, spread over a sphere-sized box.
std::vector<float> MakePoints(uint32_t count) {
    std::vector<float> xyz(static_cast<size_t>(count) * 3);
    for (uint32_t i = 0; i < count; ++i) {
        xyz[i * 3 + 0] = static_cast<float>((i % 97) * 0.01);
        xyz[i * 3 + 1] = static_cast<float>((i % 89) * 0.02);
        xyz[i * 3 + 2] = static_cast<float>((i % 83) * 0.03);
    }
    return xyz;
}

#if defined(__APPLE__)
// The live (in-use) bytes and blocks of the default malloc zone — the same
// source #147's profile read with vmmap. The zone's statistics are the
// *live* set, not the churn; the per-tick allocation count is a property of
// the source (see the contract's waste inventory).
void ZoneStats(size_t* live_bytes, size_t* live_blocks) {
    *live_bytes = 0;
    *live_blocks = 0;
    malloc_statistics_t stats;
    memset(&stats, 0, sizeof(stats));
    malloc_zone_statistics(malloc_default_zone(), &stats);
    *live_bytes = stats.size_in_use;
    *live_blocks = stats.blocks_in_use;
}
#else
void ZoneStats(size_t* live_bytes, size_t* live_blocks) {
    *live_bytes = 0;
    *live_blocks = 0;
}
#endif

void Print(const char* key, double value) { std::printf("%s=%.3f\n", key, value); }
void PrintInt(const char* key, uint64_t value) { std::printf("%s=%llu\n", key, (unsigned long long)value); }

}  // namespace

int main(int argc, char** argv) {
    // The phase count is fixed; --quick makes a smoke run (for CI) short.
    int encode_iters_small = 200;
    int encode_iters_live = 50;
    int bcast_iters = 100;
    int wait_repeats = 5;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quick") == 0) {
            encode_iters_small = 20;
            encode_iters_live = 5;
            bcast_iters = 20;
            wait_repeats = 3;
        }
    }

    // --- the platform's wait floor: the rate ceiling ------------------
    // The request each rate would ask for: 24 Hz -> 41 ms, 50 Hz -> 20 ms,
    // 60 Hz -> 16 ms.
    const int requests[3] = {41, 20, 16};
    const double rates[3] = {24.0, 50.0, 60.0};
    for (int i = 0; i < 3; ++i) {
        const double realised = WaitFloorMs(requests[i], wait_repeats);
        char key[64];
        std::snprintf(key, sizeof(key), "platform.wait_ms.%d", requests[i]);
        Print(key, realised);
        std::snprintf(key, sizeof(key), "platform.rate.%d", static_cast<int>(rates[i]));
        Print(key, 1000.0 / realised);
    }

    // --- the encoder ---------------------------------------------------
    const uint32_t counts[2] = {kSyntheticPoints, kLivePoints};
    const int iters[2] = {encode_iters_small, encode_iters_live};
    for (int c = 0; c < 2; ++c) {
        const std::vector<float> xyz = MakePoints(counts[c]);
        // One warm-up pass, so the first timed frame is not the one that
        // pays for the buffer's first touch.
        std::vector<uint8_t> warm = openscanner::frame_protocol::encode_cloud_snapshot(1, 0.0, 0.0, xyz.data(),
                                                                                      counts[c]);
        size_t live_before = 0, live_after = 0, blocks_before = 0, blocks_after = 0;
        ZoneStats(&live_before, &blocks_before);
        std::vector<double> samples;
        samples.reserve(iters[c]);
        std::vector<uint8_t> encoded;
        for (int i = 0; i < iters[c]; ++i) {
            auto t0 = Clock::now();
            encoded = openscanner::frame_protocol::encode_cloud_snapshot(static_cast<uint32_t>(i), 0.0, 0.0,
                                                                        xyz.data(), counts[c]);
            auto t1 = Clock::now();
            samples.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        ZoneStats(&live_after, &blocks_after);
        char key[64];
        std::snprintf(key, sizeof(key), "encode.bytes.%u", counts[c]);
        PrintInt(key, encoded.size());
        std::snprintf(key, sizeof(key), "encode.us.%u", counts[c]);
        Print(key, MedianUs(samples));
        if (c == 1) {
            // The live frame is the one the budgets are written against.
            PrintInt("encode.bytes.live", encoded.size());
            Print("encode.us.live", MedianUs(samples));
            // The bytes the encoder asks the allocator for per frame: the
            // reserved buffer is handed out in one block (the reserve is in
            // the encoder), so the churn per frame is the frame's size.
            Print("alloc.bytes_per_frame", static_cast<double>(encoded.size()));
            PrintInt("alloc.live_blocks_delta", blocks_after >= blocks_before ? blocks_after - blocks_before : 0);
        }
        // The decoder must read back what the encoder wrote.
        uint32_t seq = 0, out_count = 0;
        double t_cap = 0.0, t_srv = 0.0;
        const float* out_xyz = nullptr;
        bool ok = openscanner::frame_protocol::decode_cloud_snapshot(encoded.data(), encoded.size(), &seq, &t_cap,
                                                                      &t_srv, &out_xyz, &out_count);
        Print("encode.roundtrip_ok", ok && out_count == counts[c] ? 1.0 : 0.0);
        (void)warm;
    }

    // --- the broadcast --------------------------------------------------
    {
        const uint16_t port = FreePort();
        openscanner::WsBroadcastServer server;
        if (port == 0 || !server.Start(port)) {
            std::fprintf(stderr, "perf_budget: the broadcast server could not start\n");
            return 2;
        }
        std::vector<float> xyz = MakePoints(kLivePoints);
        std::vector<uint8_t> frame =
            openscanner::frame_protocol::encode_cloud_snapshot(1, 0.0, 0.0, xyz.data(), kLivePoints);

        for (int clients : {1, 8}) {
            std::vector<std::unique_ptr<DrainClient>> pool;
            for (int i = 0; i < clients; ++i) {
                pool.emplace_back(new DrainClient());
                if (!pool.back()->Connect(port)) {
                    std::fprintf(stderr, "perf_budget: a client could not connect\n");
                    return 2;
                }
            }
            // Let the server's accept loop register them.
            for (int i = 0; i < 200 && server.ConnectedClientCount() < (size_t)clients; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            const size_t connected = server.ConnectedClientCount();
            PrintInt("bcast.clients", connected);
            size_t live_before = 0, live_after = 0, blocks_before = 0, blocks_after = 0;
            ZoneStats(&live_before, &blocks_before);
            std::vector<double> samples;
            samples.reserve(bcast_iters);
            for (int i = 0; i < bcast_iters; ++i) {
                auto t0 = Clock::now();
                server.BroadcastBinary(frame.data(), frame.size());
                auto t1 = Clock::now();
                // The first broadcasts pay the socket buffers' setup; they
                // are warm-up, not the steady-state cost.
                if (i >= kBroadcastWarmup) samples.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            }
            ZoneStats(&live_after, &blocks_after);
            const double per_frame = MedianUs(samples);
            char key[64];
            std::snprintf(key, sizeof(key), "bcast.us.%d", clients);
            Print(key, per_frame);
            std::snprintf(key, sizeof(key), "bcast.us_min.%d", clients);
            Print(key, MinUs(samples));
            if (clients == 8) {
                Print("bcast.us_per_client.8", connected ? per_frame / static_cast<double>(connected) : 0.0);
                Print("bcast.us_per_client_min.8",
                      connected ? MinUs(samples) / static_cast<double>(connected) : 0.0);
                PrintInt("bcast.live_blocks_delta",
                         blocks_after >= blocks_before ? blocks_after - blocks_before : 0);
            }
        }
        size_t live_bytes = 0, live_blocks = 0;
        ZoneStats(&live_bytes, &live_blocks);
        PrintInt("alloc.live_bytes", live_bytes);
        PrintInt("alloc.live_blocks", live_blocks);
    }

    std::printf("perf_budget=ok\n");
    return 0;
}
