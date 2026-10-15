#include "openscanner/ws_broadcast_server.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define OPENSCANNER_INVALID_SOCKET INVALID_SOCKET
#define OPENSCANNER_CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define OPENSCANNER_INVALID_SOCKET (-1)
#define OPENSCANNER_CLOSESOCK ::close
#endif

// ---------------------------------------------------------------------------
// SHA-1 (FIPS 180-4) and Base64 — only used to compute the Sec-WebSocket-
// Accept header during the RFC 6455 opening handshake. Implemented
// directly from the published specifications; not derived from any
// third-party source.
// ---------------------------------------------------------------------------
namespace {

struct Sha1Digest {
    uint8_t bytes[20];
};

uint32_t RotLeft(uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

Sha1Digest Sha1(const uint8_t* data, size_t len) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

    std::vector<uint8_t> msg(data, data + len);
    uint64_t bit_len = static_cast<uint64_t>(len) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0x00);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF));

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = RotLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = RotLeft(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = RotLeft(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    Sha1Digest digest;
    uint32_t h[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        digest.bytes[i * 4] = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
        digest.bytes[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
        digest.bytes[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
        digest.bytes[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
    }
    return digest;
}

std::string Base64Encode(const uint8_t* data, size_t len) {
    static const char* kTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back(kTable[(n >> 6) & 0x3F]);
        out.push_back(kTable[n & 0x3F]);
        i += 3;
    }
    size_t remaining = len - i;
    if (remaining == 1) {
        uint32_t n = data[i] << 16;
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back(kTable[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

constexpr const char* kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string ComputeAcceptKey(const std::string& client_key) {
    std::string combined = client_key + kWebSocketGuid;
    Sha1Digest digest = Sha1(reinterpret_cast<const uint8_t*>(combined.data()), combined.size());
    return Base64Encode(digest.bytes, sizeof(digest.bytes));
}

// Case-insensitive search for a header's value in a raw HTTP request.
bool FindHeaderValue(const std::string& request, const std::string& header_name, std::string* out_value) {
    std::string lower_request = request;
    std::string lower_header = header_name;
    std::transform(lower_request.begin(), lower_request.end(), lower_request.begin(), ::tolower);
    std::transform(lower_header.begin(), lower_header.end(), lower_header.begin(), ::tolower);

    size_t pos = lower_request.find(lower_header + ":");
    if (pos == std::string::npos) return false;
    size_t value_start = pos + header_name.size() + 1;
    while (value_start < request.size() && (request[value_start] == ' ' || request[value_start] == '\t')) {
        ++value_start;
    }
    size_t value_end = request.find("\r\n", value_start);
    if (value_end == std::string::npos) return false;
    *out_value = request.substr(value_start, value_end - value_start);
    return true;
}

void SendAll(socket_t sock, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
#if defined(_WIN32)
        int n = send(sock, reinterpret_cast<const char*>(data + sent), static_cast<int>(len - sent), 0);
#else
        ssize_t n = send(sock, data + sent, len - sent, 0);
#endif
        if (n <= 0) return;  // Peer gone; the reader thread will notice and prune this client.
        sent += static_cast<size_t>(n);
    }
}

// Encodes and sends one unmasked WebSocket frame (server->client frames are
// never masked per RFC 6455).
void SendFrame(socket_t sock, uint8_t opcode, const uint8_t* payload, size_t len) {
    std::vector<uint8_t> header;
    header.push_back(0x80 | opcode);  // FIN=1, opcode
    if (len <= 125) {
        header.push_back(static_cast<uint8_t>(len));
    } else if (len <= 0xFFFF) {
        header.push_back(126);
        header.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        header.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        header.push_back(127);
        for (int i = 7; i >= 0; --i) {
            header.push_back(static_cast<uint8_t>((static_cast<uint64_t>(len) >> (i * 8)) & 0xFF));
        }
    }
    SendAll(sock, header.data(), header.size());
    if (len > 0) SendAll(sock, payload, len);
}

}  // namespace

namespace openscanner {

struct WsBroadcastServer::Impl {
    std::atomic<bool> running{false};
    socket_t listen_socket = OPENSCANNER_INVALID_SOCKET;
    std::thread accept_thread;

    mutable std::mutex clients_mutex;
    std::vector<socket_t> clients;
    std::vector<std::thread> reader_threads;

    void AcceptLoop() {
        while (running.load()) {
            sockaddr_in client_addr = {};
#if defined(_WIN32)
            int addr_len = sizeof(client_addr);
#else
            socklen_t addr_len = sizeof(client_addr);
#endif
            socket_t client = accept(listen_socket, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
            if (client == OPENSCANNER_INVALID_SOCKET) {
                if (!running.load()) break;
                continue;
            }
            if (!PerformHandshake(client)) {
                OPENSCANNER_CLOSESOCK(client);
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                clients.push_back(client);
                reader_threads.emplace_back([this, client]() { WatchForDisconnect(client); });
            }
        }
    }

    bool PerformHandshake(socket_t client) {
        char buf[4096];
#if defined(_WIN32)
        int n = recv(client, buf, sizeof(buf) - 1, 0);
#else
        ssize_t n = recv(client, buf, sizeof(buf) - 1, 0);
#endif
        if (n <= 0) return false;
        std::string request(buf, static_cast<size_t>(n));

        std::string client_key;
        if (!FindHeaderValue(request, "Sec-WebSocket-Key", &client_key)) return false;

        std::string accept_key = ComputeAcceptKey(client_key);
        std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Accept: " +
                                accept_key + "\r\n\r\n";
        SendAll(client, reinterpret_cast<const uint8_t*>(response.data()), response.size());
        return true;
    }

    // We don't need anything a client sends us; this thread exists purely
    // to detect when a client goes away (recv returns 0/error) so we can
    // prune it from the broadcast list instead of accumulating dead sockets.
    void WatchForDisconnect(socket_t client) {
        char scratch[256];
        while (running.load()) {
#if defined(_WIN32)
            int n = recv(client, scratch, sizeof(scratch), 0);
#else
            ssize_t n = recv(client, scratch, sizeof(scratch), 0);
#endif
            if (n <= 0) break;
        }
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(std::remove(clients.begin(), clients.end(), client), clients.end());
        OPENSCANNER_CLOSESOCK(client);
    }
};

WsBroadcastServer::WsBroadcastServer() : impl_(std::make_unique<Impl>()) {}

WsBroadcastServer::~WsBroadcastServer() { Stop(); }

bool WsBroadcastServer::Start(uint16_t port) {
#if defined(_WIN32)
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return false;
#endif

    impl_->listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (impl_->listen_socket == OPENSCANNER_INVALID_SOCKET) return false;

    int reuse = 1;
    setsockopt(impl_->listen_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);

    if (bind(impl_->listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        OPENSCANNER_CLOSESOCK(impl_->listen_socket);
        impl_->listen_socket = OPENSCANNER_INVALID_SOCKET;
        return false;
    }
    if (listen(impl_->listen_socket, 8) != 0) {
        OPENSCANNER_CLOSESOCK(impl_->listen_socket);
        impl_->listen_socket = OPENSCANNER_INVALID_SOCKET;
        return false;
    }

    impl_->running = true;
    impl_->accept_thread = std::thread([this]() { impl_->AcceptLoop(); });
    return true;
}

void WsBroadcastServer::Stop() {
    if (!impl_->running.exchange(false)) return;

    if (impl_->listen_socket != OPENSCANNER_INVALID_SOCKET) {
        OPENSCANNER_CLOSESOCK(impl_->listen_socket);
        impl_->listen_socket = OPENSCANNER_INVALID_SOCKET;
    }
    if (impl_->accept_thread.joinable()) impl_->accept_thread.join();

    std::vector<socket_t> clients_copy;
    {
        std::lock_guard<std::mutex> lock(impl_->clients_mutex);
        clients_copy = impl_->clients;
    }
    for (socket_t client : clients_copy) {
        OPENSCANNER_CLOSESOCK(client);
    }
    for (auto& t : impl_->reader_threads) {
        if (t.joinable()) t.join();
    }
    impl_->reader_threads.clear();
    {
        std::lock_guard<std::mutex> lock(impl_->clients_mutex);
        impl_->clients.clear();
    }

#if defined(_WIN32)
    WSACleanup();
#endif
}

void WsBroadcastServer::BroadcastBinary(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(impl_->clients_mutex);
    for (socket_t client : impl_->clients) {
        SendFrame(client, 0x2 /* binary */, data, size);
    }
}

size_t WsBroadcastServer::ConnectedClientCount() const {
    std::lock_guard<std::mutex> lock(impl_->clients_mutex);
    return impl_->clients.size();
}

}  // namespace openscanner
