// Row #163's device-free test: the per-client broadcast counters behind the
// WsBroadcastServer pimpl. A real server on a loopback port, real WebSocket
// clients, and the counters read back through the accessor — no vendor SDK
// and no device.
//
// It covers the three cases the row names: a fast client (every frame sent,
// times recorded), eight fast clients (the fan-out), and a stalled client
// that never reads (the 100 ms SO_SNDTIMEO fires: a drop and a send timeout,
// with the fast clients still being served).
//
// The last check is the text path (a status broadcast), because the counters
// sit in the shared send helper rather than in BroadcastBinary alone.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "openscanner/ws_broadcast_server.h"

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

#if !defined(_WIN32)

// A minimal WebSocket client: the RFC 6455 handshake, then a drain thread for
// the fast clients. The stalled client simply never starts one.
class WsClient {
public:
    ~WsClient() { StopDrain(); }

    bool Connect(uint16_t port, int receive_buffer_bytes) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        if (receive_buffer_bytes > 0) {
            setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &receive_buffer_bytes, sizeof(receive_buffer_bytes));
        }
        struct timeval timeout;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;

        const char* request =
            "GET / HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: MDEyMzQ1Njc4OWFiY2RlZg==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n";
        if (send(fd_, request, std::strlen(request), 0) != static_cast<ssize_t>(std::strlen(request))) {
            return false;
        }
        std::string response;
        char buffer[512];
        while (response.find("\r\n\r\n") == std::string::npos) {
            const ssize_t n = recv(fd_, buffer, sizeof(buffer), 0);
            if (n <= 0) return false;
            response.append(buffer, static_cast<size_t>(n));
        }
        return response.find("101") != std::string::npos;
    }

    void StartDrain() {
        draining_ = true;
        reader_ = std::thread([this]() {
            std::vector<char> buffer(65536);
            while (draining_) {
                const ssize_t n = recv(fd_, buffer.data(), buffer.size(), 0);
                if (n <= 0) break;
                bytes_read_ += static_cast<uint64_t>(n);
            }
        });
    }

    void StopDrain() {
        draining_ = false;
        if (fd_ >= 0) shutdown(fd_, SHUT_RDWR);
        if (reader_.joinable()) reader_.join();
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    uint64_t bytes_read() const { return bytes_read_; }

private:
    int fd_ = -1;
    std::atomic<bool> draining_{false};
    std::thread reader_;
    uint64_t bytes_read_ = 0;
};

// A cheap frame: the payload content is irrelevant to the counters.
std::vector<uint8_t> Payload(size_t bytes) { return std::vector<uint8_t>(bytes, 0x5a); }

void Run() {
    openscanner::WsBroadcastServer server;
    uint16_t port = 0;
    for (uint16_t attempt = 0; attempt < 12; ++attempt) {
        const uint16_t candidate = static_cast<uint16_t>(18780 + attempt);
        if (server.Start(candidate)) {
            port = candidate;
            break;
        }
    }
    Check(port != 0, "the server starts on a loopback port");
    if (port == 0) return;

    {
        const openscanner::WsBroadcastServer::BroadcastStats empty = server.GetBroadcastStats();
        Check(empty.broadcasts == 0 && empty.sends == 0 && empty.drops == 0 && empty.timeouts == 0 &&
                  empty.connected == 0,
              "an idle server reports zero counters");
        Check(empty.per_client.empty(), "an idle server reports no clients");
    }

    // --- one fast client: every frame sent, times recorded -----------------
    std::unique_ptr<WsClient> fast = std::make_unique<WsClient>();
    Check(fast->Connect(port, 1 << 20), "the fast client connects (1 client)");
    fast->StartDrain();

    const std::vector<uint8_t> frame = Payload(64 * 1024);
    for (int i = 0; i < 5; ++i) server.BroadcastBinary(frame.data(), frame.size());

    {
        const openscanner::WsBroadcastServer::BroadcastStats stats = server.GetBroadcastStats();
        Check(stats.connected == 1, "1-vs-N: one client connected");
        Check(stats.broadcasts == 5, "1-vs-N: five broadcasts counted");
        Check(stats.sends == 5, "1-vs-N: five per-client sends counted");
        Check(stats.per_client.size() == 1 && stats.per_client[0].sends == 5,
              "1-vs-N: the per-client counter matches");
        Check(stats.bytes == frame.size() * 5, "the payload bytes are counted");
        Check(stats.send_us_total > 0 && stats.send_us_last > 0,
              "the send time is measured (total and last)");
        Check(stats.send_us_max >= stats.send_us_last, "the max send time bounds the last");
        Check(stats.drops == 0 && stats.timeouts == 0, "a fast client drops nothing");
    }

    // --- eight fast clients: the fan-out -----------------------------------
    std::vector<std::unique_ptr<WsClient>> clients;
    clients.push_back(std::move(fast));
    for (int i = 1; i < 8; ++i) {
        auto client = std::make_unique<WsClient>();
        Check(client->Connect(port, 1 << 20), "a fast client connects (" + std::to_string(i + 1) + ")");
        client->StartDrain();
        clients.push_back(std::move(client));
    }
    const uint64_t before_fanout_sends = server.GetBroadcastStats().sends;
    for (int i = 0; i < 5; ++i) server.BroadcastBinary(frame.data(), frame.size());
    {
        const openscanner::WsBroadcastServer::BroadcastStats stats = server.GetBroadcastStats();
        Check(stats.connected == 8, "1-vs-8: eight clients connected");
        Check(stats.sends - before_fanout_sends == 40, "1-vs-8: five frames x eight clients = 40 sends");
        Check(stats.per_client.size() == 8, "1-vs-8: eight per-client entries");
        bool every_client = stats.per_client.size() == 8;
        for (const auto& client : stats.per_client) every_client = every_client && client.sends >= 5;
        Check(every_client, "1-vs-8: every client's counter advanced");
        Check(stats.drops == 0 && stats.timeouts == 0, "1-vs-8: no drops at eight fast clients");
    }

    // --- a stalled client: the send timeout, a drop, and the fast clients
    //     still being served ------------------------------------------------
    auto stalled = std::make_unique<WsClient>();
    Check(stalled->Connect(port, 4096), "the stalled client connects and never reads");
    const std::vector<uint8_t> big = Payload(192 * 1024);
    const uint64_t before_stall = server.GetBroadcastStats().sends;
    int broadcasts_with_stalled = 0;
    uint64_t drops = 0;
    for (int i = 0; i < 40 && drops == 0; ++i) {
        server.BroadcastBinary(big.data(), big.size());
        ++broadcasts_with_stalled;
        drops = server.GetBroadcastStats().drops;
    }
    {
        const openscanner::WsBroadcastServer::BroadcastStats stats = server.GetBroadcastStats();
        Check(stats.drops >= 1, "a stalled client is dropped");
        Check(stats.timeouts >= 1, "the drop is counted as a send timeout (SO_SNDTIMEO)");
        Check(stats.drops == stats.timeouts, "every drop here was a timeout");
        Check(stats.queue_depth_max >= 1, "the in-flight (queue) depth peak is recorded");
        Check(stats.connected == 8, "the stalled client is no longer counted as connected");
        Check(stats.sends - before_stall >= static_cast<uint64_t>(broadcasts_with_stalled) * 8,
              "the eight fast clients were still served through the stall");
        Check(stats.per_client.size() == 8, "only the stalled client left the per-client list");
        Check(clients[0]->bytes_read() > 0, "a fast client actually received bytes");
    }

    // --- the text path ------------------------------------------------------
    {
        const uint64_t before = server.GetBroadcastStats().broadcasts;
        server.BroadcastText("{\"type\":\"status\",\"probe\":true}");
        const openscanner::WsBroadcastServer::BroadcastStats stats = server.GetBroadcastStats();
        Check(stats.broadcasts == before + 1, "a text broadcast is counted like a binary one");
        Check(stats.sends >= 8, "the text broadcast reached the connected clients");
    }

    // --- a no-client broadcast is not counted as a broadcast ---------------
    for (auto& client : clients) client->StopDrain();
    {
        const openscanner::WsBroadcastServer::BroadcastStats before = server.GetBroadcastStats();
        // The reader threads notice the closed sockets asynchronously; wait for
        // the count to settle, then broadcast into the empty list.
        for (int i = 0; i < 100 && server.ConnectedClientCount() != 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        server.BroadcastBinary(frame.data(), frame.size());
        const openscanner::WsBroadcastServer::BroadcastStats after = server.GetBroadcastStats();
        if (before.connected == 0) {
            Check(after.broadcasts == before.broadcasts && after.sends == before.sends,
                  "a broadcast with no clients is not counted");
        } else {
            std::printf("note: clients had not all left; the no-client check was skipped\n");
        }
    }

    stalled->StopDrain();
    server.Stop();
}

#else
void Run() { std::printf("note: the broadcast counters' socket leg is not built on Windows\n"); }
#endif  // !_WIN32

}  // namespace

int main() {
    Run();
    std::printf("[ws-counters] %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
