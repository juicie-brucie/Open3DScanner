#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openscanner {

// A minimal, dependency-free WebSocket server for one purpose: broadcast
// binary frames (point-cloud data) to whichever UI clients are connected,
// as fast and simply as possible. Deliberately not a general WebSocket
// implementation — no TLS, no fragmentation, no client->server message
// handling beyond the opening handshake and close/ping frames. It listens
// on all interfaces (not just loopback) so the UI can be reached from
// another machine on the LAN; there is still no auth, so don't run it on
// an untrusted network.
class WsBroadcastServer {
public:
    WsBroadcastServer();
    ~WsBroadcastServer();

    WsBroadcastServer(const WsBroadcastServer&) = delete;
    WsBroadcastServer& operator=(const WsBroadcastServer&) = delete;

    // Starts listening on 0.0.0.0:port (all interfaces) and accepting
    // client connections on a background thread. Returns false if the
    // socket couldn't be bound (port already in use, etc.).
    bool Start(uint16_t port);

    // Stops accepting new connections, closes all client sockets, and
    // joins background threads. Safe to call even if Start() failed.
    void Stop();

    // Sends `data` (raw bytes, becomes one WebSocket binary frame) to
    // every currently-connected client. Thread-safe; cheap no-op if no
    // clients are connected.
    void BroadcastBinary(const uint8_t* data, size_t size);

    // Number of currently-connected clients. Thread-safe.
    size_t ConnectedClientCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace openscanner
